import json
import shutil
from pathlib import Path
import subprocess
import sys

metadata = json.loads(subprocess.check_output([
    'cargo', 'metadata', '--locked', '--format-version', '1',
    '--manifest-path', str(Path(__file__).with_name('Cargo.toml')),
], text=True))
texts = ['open nectar disc importer\n\ndisc format library: https://github.com/encounter/nod\n'
         'pinned revision: ebd80cac99b48a323200d84365e07a58cc27412d\n'
         'the decoder includes no game data. dependency notices follow.\n']
for package in sorted(metadata['packages'], key=lambda item: item['name']):
    if package['name'] == 'nectar-disc':
        continue
    root = Path(package['manifest_path']).parent
    files = sorted(path for path in root.rglob('*') if path.is_file()
                   and (path.name.upper().startswith(('LICENSE', 'COPYING')))
                   and path.stat().st_size < 256 * 1024)
    if not files:
        files = sorted(path for path in root.parent.glob('LICENSE*') if path.is_file())
    texts.append(f"\n{'=' * 72}\n{package['name']} {package['version']} — {package.get('license', '')}\n")
    for path in files:
        texts.append(f"\n{path.relative_to(root) if path.is_relative_to(root) else path.name}:\n{path.read_text(errors='replace')}\n")
emcc = shutil.which('emcc')
if not emcc:
    raise RuntimeError('activate Emscripten before collecting decoder notices')
cache = Path(subprocess.check_output(['em-config', 'CACHE'], text=True).strip())
zlib_sources = sorted((cache / 'ports/zlib').glob('zlib-*/zlib.h'))
if not zlib_sources:
    raise RuntimeError('the Emscripten zlib port license was not found')
header = zlib_sources[-1].read_text(encoding='utf-8')
texts.append('\n' + '=' * 72 + '\nzlib (emscripten port)\n' + header[:header.index('*/') + 2] + '\n')
Path(sys.argv[1]).write_text(''.join(texts), encoding='utf-8')
