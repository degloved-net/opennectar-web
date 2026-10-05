import json
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time
from urllib.request import urlopen


server = Path(__file__).resolve().parents[1] / 'serve.py'
with tempfile.TemporaryDirectory(prefix='nectar-serve-test-') as temporary:
    folder = Path(temporary)
    (folder / 'nectar.html').write_text('<title>test</title>')
    (folder / 'nectar.wasm').write_bytes(b'\x00asm')
    manifest = folder / 'nectar-build.json'
    for mode in (False, True, None):
        if mode is None:
            manifest.unlink()
        else:
            manifest.write_text(json.dumps({'threaded': mode}))
        with socket.socket() as probe:
            probe.bind(('127.0.0.1', 0))
            port = probe.getsockname()[1]
        process = subprocess.Popen([sys.executable, str(server), '--directory', str(folder), '--port', str(port)],
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            for attempt in range(50):
                try:
                    response = urlopen(f'http://127.0.0.1:{port}/', timeout=1)
                    break
                except OSError:
                    if process.poll() is not None:
                        raise AssertionError(process.communicate())
                    time.sleep(0.05)
            else:
                raise AssertionError('server did not start')
            with response:
                assert response.status == 200
                headers = response.headers
                for key, value in (('Cross-Origin-Opener-Policy', 'same-origin'),
                                   ('Cross-Origin-Embedder-Policy', 'require-corp'),
                                   ('Cross-Origin-Resource-Policy', 'same-origin')):
                    assert headers.get(key) == (None if mode is False else value), (mode, key, headers)
            with urlopen(f'http://127.0.0.1:{port}/nectar.wasm', timeout=1) as response:
                assert response.headers.get_content_type() == 'application/wasm'
        finally:
            process.terminate()
            process.communicate(timeout=5)

    manifest.write_text(json.dumps({'threaded': 'false'}))
    result = subprocess.run([sys.executable, str(server), '--directory', str(folder)],
                            capture_output=True, text=True, timeout=5)
    assert result.returncode != 0 and 'threading mode is invalid' in result.stderr
print('web server mode tests passed')
