import argparse
import base64
import hashlib
import json
from pathlib import Path
import re
import shutil
import tempfile
from urllib.parse import urlsplit
from package_rom import split_rom

TOOLS = Path(__file__).resolve().parent


def inline(value):
    return re.sub(r'</script', r'<\\/script', value, flags=re.IGNORECASE)


def replace_once(text, old, new):
    if text.count(old) != 1:
        raise ValueError('the browser loader changed; update the launcher packager: ' + old)
    return text.replace(old, new)


def publish(stage, destination):
    backup = stage.parent / 'previous'
    if destination.exists(): destination.rename(backup)
    try: stage.rename(destination)
    except OSError:
        if backup.exists(): backup.rename(destination)
        raise


def cdn_root(root):
    url = urlsplit(root)
    if url.scheme != 'https' or url.netloc != 'cdn.jsdelivr.net' or url.query or url.fragment \
            or not re.fullmatch(r'/gh/[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+@[A-Za-z0-9_.-]+(?:/[A-Za-z0-9_.-]+)*', url.path):
        raise ValueError('provide a versioned https://cdn.jsdelivr.net/gh/owner/repo@commit/path URL')
    return root.rstrip('/')


def package_jsdelivr(build, destination, root, rom=None, part_bytes=19_000_000, replace=False, resource_roots=None):
    build, destination = Path(build).resolve(), Path(destination).resolve()
    if build.is_relative_to(destination) or (rom is not None and Path(rom).resolve().is_relative_to(destination)):
        raise ValueError('the output folder must not contain the build or source rom')
    root = cdn_root(root)
    if not 1 <= part_bytes <= 20_000_000:
        raise ValueError('rom parts must contain between 1 and 20000000 bytes')
    info = json.loads((build / 'nectar-build.json').read_text())
    if info.get('threaded') is not False:
        raise ValueError('the jsdelivr launcher requires a single-threaded build')
    info = {**info, 'textureStorage': 'indexeddb', 'distribution': 'jsdelivr'}
    names = {'nectar-disc.js', 'nectar_disc.wasm', 'nectar-disc-licenses.txt'}
    for script in info['runtimes'].values():
        if script not in ('nectar.js', 'nectar-pal.js'):
            raise ValueError('the engine list is invalid')
        names.update((script, script[:-3] + '.wasm'))
    identity = re.sub(r'@[^/]+', '', root.rstrip('/'))
    config = {'version': 1, 'root': root.rstrip('/'), 'build': info,
              'storage': 'nectar-jsdelivr-' + hashlib.sha256(identity.encode()).hexdigest()[:16],
              'files': {}, 'rom': None}
    if destination.exists() and (not replace or not (destination / 'cdn/nectar-jsdelivr.json').is_file()):
        raise ValueError('choose a new output folder, or use --replace for an existing jsdelivr package')
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.nectar-jsdelivr-', dir=destination.parent) as temporary:
        stage = Path(temporary) / 'package'; payload = stage / 'cdn'; payload.mkdir(parents=True)
        def describe(name, source=None, data=None):
            target = payload / name; target.parent.mkdir(parents=True, exist_ok=True)
            if source is not None:
                if source.is_symlink() or not source.is_file():
                    raise ValueError('the build is missing a regular runtime file: ' + name)
                shutil.copy2(source, target)
            else:
                target.write_bytes(data)
            size = target.stat().st_size
            if size > 20_000_000:
                raise ValueError('a runtime file exceeds the jsdelivr file limit: ' + name)
            with target.open('rb') as stream:
                checksum = hashlib.file_digest(stream, 'sha256').hexdigest()
            config['files'][name] = {'bytes': size, 'sha256': checksum}
        for name in sorted(names):
            describe(name, source=build / name)
        if rom is not None:
            split = split_rom(rom, payload, part_bytes)
            config['rom'] = {**split, 'parts': [part['path'] for part in split['parts']]}
            config['files'].update({part['path']: {'bytes': part['bytes'], 'sha256': part['sha256']}
                                    for part in split['parts']})
        for name, resource_root in (resource_roots or {}).items():
            if name not in config['files']:
                raise ValueError('the cdn resource is missing: ' + name)
            config['files'][name]['root'] = cdn_root(resource_root)
        loader = (TOOLS / 'loader.js').read_text()
        loader = replace_once(loader, 'this.module = globalThis.Module = this.createRuntime();',
                              'this.module = globalThis.Module = Nectar.Cdn.configureRuntime(this.createRuntime());')
        loader = replace_once(loader, "fetch('nectar-build.json', {cache: 'no-cache'})",
                              'Promise.resolve(new Response(JSON.stringify(Nectar.Cdn.config.build)))')
        loader = replace_once(loader, "fetch('nectar-assets.json', {cache: 'no-cache'})",
                              'Promise.resolve(new Response(null, {status: 404}))')
        loader = replace_once(loader, 'await this.loadScript(engine);', 'await Nectar.Cdn.loadEngine(this, engine);')
        loader = replace_once(loader, "navigator.locks.request(Nectar.game === 'pikmin2' ? 'nectar-pikmin2-game' : 'open-nectar-game',",
                              "navigator.locks.request(Nectar.Cdn.config.storage + '-game',")
        sources = {'packs': (TOOLS / 'packs.js').read_text(), 'discFiles': (TOOLS / 'disc-files.js').read_text(),
                   'discWorker': (TOOLS / 'jsdelivr/disc-worker.js').read_text(),
                   'packWorker': (TOOLS / 'jsdelivr/pack-worker.js').read_text()}
        html = replace_once((TOOLS / 'shell.html').read_text(), '{{{ SCRIPT }}}',
                            '<script src="' + info['runtimes'][info['discKey']] + '"></script>')
        html = replace_once(html, '<link rel="stylesheet" href="nectar.css">', '<style>' + (TOOLS / 'nectar.css').read_text() + '</style>')
        icon = base64.b64encode((build / 'nectar-icon.png').read_bytes()).decode()
        html = replace_once(html, 'href="nectar-icon.png"', 'href="data:image/png;base64,' + icon + '"')
        scripts = ['packs.js', 'actions.js', 'disc-files.js', 'disc-store.js', 'discs.js']
        body = '\n'.join((TOOLS / name).read_text() for name in scripts)
        body += '\nNectar.CdnConfig = ' + json.dumps(config, separators=(',', ':')) + ';\n'
        body += 'Nectar.CdnSources = ' + json.dumps(sources, separators=(',', ':')) + ';\n'
        body += (TOOLS / 'jsdelivr/storage.js').read_text() + '\n' + loader
        for name in scripts:
            html = replace_once(html, '<script src="nectar-' + name + '"></script>', '')
        html = replace_once(html, '<script src="nectar-loader.js"></script>', '<script>' + inline(body) + '</script>')
        (stage / 'index.html').write_text(html)
        (payload / 'nectar-jsdelivr.json').write_text(json.dumps(config, indent=2) + '\n')
        publish(stage, destination)
    return destination


def main():
    parser = argparse.ArgumentParser(description='package a separate local HTML launcher for jsdelivr')
    parser.add_argument('build', type=Path)
    parser.add_argument('destination', type=Path)
    parser.add_argument('--cdn-root', required=True)
    parser.add_argument('--rom', type=Path)
    parser.add_argument('--part-bytes', type=int, default=19_000_000)
    parser.add_argument('--replace', action='store_true')
    args = parser.parse_args()
    try:
        package = package_jsdelivr(args.build, args.destination, args.cdn_root, args.rom, args.part_bytes, args.replace)
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.exit(1, 'packaging stopped: ' + str(error) + '\n')
    print('created ' + str(package / 'index.html') + '; publish the contents of cdn at the configured URL')


if __name__ == '__main__': main()
