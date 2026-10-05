import argparse
import json
from pathlib import Path
import shutil
import tempfile

from package_jsdelivr import publish, replace_once
from package_rom import split_rom

TOOLS = Path(__file__).resolve().parent

REQUIRED_FILES = {
    'nectar.html', 'nectar.js', 'nectar.wasm', 'nectar-loader.js',
    'nectar-packs.js', 'nectar-actions.js', 'nectar.css', 'nectar-icon.png',
    'nectar-discs.js', 'nectar-disc-files.js', 'nectar-disc-store.js', 'nectar-disc-worker.js',
    'nectar-disc.js', 'nectar_disc.wasm', 'nectar-disc-licenses.txt', 'nectar-build.json'
}
OPTIONAL_FILES = {
    'nectar.data', 'nectar-assets.js', 'nectar-assets.js.metadata', 'nectar-assets.json',
    'nectar-pal.js', 'nectar-pal.wasm', 'nectar.worker.js', 'nectar-pal.worker.js',
    'nectar-pack-worker.js'
}


def package_dist(build, destination, replace=False, rom=None, part_bytes=19_000_000):
    build, destination = Path(build).resolve(), Path(destination).resolve()
    if build.is_relative_to(destination) or (rom is not None and Path(rom).resolve().is_relative_to(destination)):
        raise ValueError('the output folder must not contain the source build or rom')
    if not (build / 'nectar-build.json').is_file():
        raise ValueError('the web build manifest is missing')
    info = json.loads((build / 'nectar-build.json').read_text())
    if not isinstance(info.get('threaded'), bool):
        raise ValueError('the web build threading mode is missing')
    needed = REQUIRED_FILES
    if info.get('textureStorage') == 'opfs': needed = needed | {'nectar-pack-worker.js'}
    for name in needed:
        if not (build / name).is_file() or (build / name).is_symlink():
            raise ValueError('the build is missing a regular runtime file: ' + name)
    if destination.exists() and (not replace or not (destination / 'assets/nectar-dist.json').is_file()):
        raise ValueError('choose a new output folder, or use --replace for an existing clean distribution')
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.nectar-dist-', dir=destination.parent) as temporary:
        stage = Path(temporary) / 'dist'; assets = stage / 'assets'; assets.mkdir(parents=True)
        for name in sorted(REQUIRED_FILES | OPTIONAL_FILES):
            source = build / name
            if not source.is_file() or name in {'nectar.html', 'nectar.css'}:
                continue
            if rom is not None and name in {'nectar.data', 'nectar-assets.js', 'nectar-assets.js.metadata', 'nectar-assets.json'}:
                continue
            if source.is_symlink(): raise ValueError('symbolic links are not allowed in a distribution')
            shutil.copy2(source, assets / name)
        for script in info['runtimes'].values():
            if script not in ('nectar.js', 'nectar-pal.js') or not (assets / script).is_file() \
                    or not (assets / (script[:-3] + '.wasm')).is_file():
                raise ValueError('the build is missing a matching engine')
        if rom is not None:
            info['rom'] = split_rom(rom, assets, part_bytes)
            for name in ['nectar.data', 'nectar-assets.js', 'nectar-assets.js.metadata', 'nectar-assets.json']:
                (assets / name).unlink(missing_ok=True)
            (assets / 'nectar-build.json').write_text(json.dumps(info) + '\n')
        html = replace_once((TOOLS / 'shell.html').read_text(), '{{{ SCRIPT }}}',
                            '<script src="' + info['runtimes'][info['discKey']] + '"></script>')
        html = replace_once(html, '<head>', '<head>\n    <base href="./assets/">')
        html = replace_once(html, '<link rel="stylesheet" href="nectar.css">',
                            '<style>' + (build / 'nectar.css').read_text() + '</style>')
        names = ['packs', 'actions', 'disc-files', 'disc-store', 'discs', 'loader']
        scripts = []
        for name in names:
            text = (build / ('nectar-' + name + '.js')).read_text()
            if name == 'loader': text = text.replace('new URL(url, location.href)', 'new URL(url, document.baseURI)')
            scripts.append(text)
            if name != 'loader': html = replace_once(html, '<script src="nectar-' + name + '.js"></script>', '')
        html = replace_once(html, '<script src="nectar-loader.js"></script>', '<script src="nectar-ui.js"></script>')
        (assets / 'nectar-ui.js').write_text('\n'.join(scripts))
        for name in ['actions', 'discs', 'loader']:
            (assets / ('nectar-' + name + '.js')).unlink(missing_ok=True)
        (stage / 'index.html').write_text(html)
        server = (TOOLS / 'serve.py').read_text().replace('/nectar.html', '/index.html')
        server = replace_once(server, "args.directory / 'nectar-build.json'", "args.directory / 'assets/nectar-build.json'")
        (stage / 'serve.py').write_text(server)
        (assets / 'nectar-dist.json').write_text(json.dumps({'format': 'open-nectar-dist', 'version': 1}) + '\n')
        publish(stage, destination)
    return destination


def main():
    parser = argparse.ArgumentParser(description='export a clean web distribution')
    parser.add_argument('build', type=Path)
    parser.add_argument('destination', type=Path)
    parser.add_argument('--replace', action='store_true')
    parser.add_argument('--rom', type=Path)
    parser.add_argument('--part-bytes', type=int, default=19_000_000)
    args = parser.parse_args()
    try: output = package_dist(args.build, args.destination, args.replace, args.rom, args.part_bytes)
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.exit(1, 'packaging stopped: ' + str(error) + '\n')
    print('created ' + str(output / 'index.html'))


if __name__ == '__main__': main()
