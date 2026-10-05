import hashlib
import json
from pathlib import Path
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from package_dist import package_dist, REQUIRED_FILES
from package_jsdelivr import package_jsdelivr

with tempfile.TemporaryDirectory(prefix='nectar-package-test-') as temporary:
    root = Path(temporary)
    build = root / 'build'; build.mkdir()
    for name in REQUIRED_FILES | {'nectar-pack-worker.js'}:
        (build / name).write_text('synthetic runtime: ' + name)
    info = {'version': 3, 'threaded': False, 'discKey': 'GPIE01:1', 'textureStorage': 'opfs',
            'runtimes': {'GPIE01:1': 'nectar.js'}}
    (build / 'nectar-build.json').write_text(json.dumps(info))
    original = {file.name: file.read_bytes() for file in build.iterdir()}
    clean = package_dist(build, root / 'clean')
    assert {file.name for file in clean.iterdir()} == {'index.html', 'assets', 'serve.py'}
    assert '<base href="./assets/">' in (clean / 'index.html').read_text()
    assert not (clean / 'nectar.html').exists()
    assert (clean / 'assets/nectar-pack-worker.js').read_bytes() == (build / 'nectar-pack-worker.js').read_bytes()
    assert 'assets/nectar-build.json' in (clean / 'serve.py').read_text()
    package_dist(build, clean, replace=True)

    rom = root / 'fixture.rvz'; rom.write_bytes(bytes(range(251)) * 3)
    for threaded in (False, True):
        info['threaded'] = threaded
        (build / 'nectar-build.json').write_text(json.dumps(info))
        split = package_dist(build, root / ('split-threaded' if threaded else 'split-single'), rom=rom, part_bytes=32)
        assert {file.name for file in split.iterdir()} == {'index.html', 'assets', 'serve.py'}
        bundled = json.loads((split / 'assets/nectar-build.json').read_text())
        assert bundled['threaded'] is threaded
        chunks = []
        for part in bundled['rom']['parts']:
            chunk = (split / 'assets' / part['path']).read_bytes()
            assert len(chunk) == part['bytes'] <= 32
            assert hashlib.sha256(chunk).hexdigest() == part['sha256']
            chunks.append(chunk)
        assert b''.join(chunks) == rom.read_bytes()
        previous = (split / 'index.html').read_bytes()
        try:
            package_dist(build, split, replace=True, rom=rom, part_bytes=0)
            raise AssertionError('invalid chunk size accepted')
        except ValueError: pass
        assert (split / 'index.html').read_bytes() == previous
    info['threaded'] = False
    (build / 'nectar-build.json').write_text(json.dumps(info))
    url = 'https://cdn.jsdelivr.net/gh/owner/repo@v1.0.0/nectar'
    cdn = package_jsdelivr(build, root / 'cdn-package', url, rom, part_bytes=32)
    assert {file.name for file in cdn.iterdir()} == {'index.html', 'cdn'}
    manifest = json.loads((cdn / 'cdn/nectar-jsdelivr.json').read_text())
    assembled = bytearray()
    for name in manifest['rom']['parts']:
        chunk = (cdn / 'cdn' / name).read_bytes()
        assert len(chunk) <= 32
        assert manifest['files'][name] == {'bytes': len(chunk), 'sha256': hashlib.sha256(chunk).hexdigest()}
        assembled.extend(chunk)
    assert assembled == rom.read_bytes()
    assert manifest['rom']['bytes'] == len(assembled)
    html = (cdn / 'index.html').read_text()
    assert '<script src="nectar-loader.js">' not in html and '<link rel="stylesheet"' not in html
    assert 'Nectar.Cdn.configureRuntime' in html and 'IndexedTextureLibrary' in html
    second = package_jsdelivr(build, root / 'second', url.replace('@v1.0.0', '@v1.0.1'))
    assert json.loads((second / 'cdn/nectar-jsdelivr.json').read_text())['storage'] == manifest['storage']
    assert json.loads((second / 'cdn/nectar-jsdelivr.json').read_text())['rom'] is None
    previous = (cdn / 'index.html').read_bytes()
    info['threaded'] = True
    (build / 'nectar-build.json').write_text(json.dumps(info))
    try:
        package_jsdelivr(build, cdn, url, rom, replace=True)
        raise AssertionError('threaded runtime accepted by the CDN launcher')
    except ValueError as error:
        assert 'single-threaded' in str(error)
    assert (cdn / 'index.html').read_bytes() == previous
    info['threaded'] = False
    (build / 'nectar-build.json').write_bytes(original['nectar-build.json'])
    assert {file.name: file.read_bytes() for file in build.iterdir()} == original
    unowned = root / 'unowned'; unowned.mkdir(); (unowned / 'keep.txt').write_text('keep')
    for package in (package_dist, lambda b, d, replace: package_jsdelivr(b, d, url, replace=replace)):
        try:
            package(build, unowned, replace=True)
            raise AssertionError('unowned output directory replaced')
        except ValueError: pass
    assert (unowned / 'keep.txt').read_text() == 'keep'
print('clean layouts, chunk verification, separate storage, and input preservation passed')
