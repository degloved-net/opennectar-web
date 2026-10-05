import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def file_hash(path):
    with path.open('rb') as source:
        digest = hashlib.sha256()
        for chunk in iter(lambda: source.read(1024 * 1024), b''):
            digest.update(chunk)
        return digest.hexdigest()


def inspect_assets(directory):
    digest = hashlib.sha256()
    count = 0
    total = 0
    paths = sorted(directory.rglob('*'))
    movies = directory.parent / 'MovieData'
    if movies.is_dir():
        paths.extend(sorted(movies.rglob('*')))
    for path in paths:
        if path.is_symlink():
            raise ValueError(f'symbolic links are not supported: {path}')
        if not path.is_file():
            continue
        name = path.relative_to(directory.parent).as_posix()
        size = path.stat().st_size
        digest.update(name.encode('utf-8', 'surrogateescape') + b'\0')
        digest.update(str(size).encode() + b'\0')
        digest.update(file_hash(path).encode() + b'\n')
        total += size
        count += 1
    metadata = {'fingerprint': digest.hexdigest(), 'fileCount': count, 'bytes': total}
    marker = directory.parent / '.pikmin-assets'
    if marker.is_file():
        parts = marker.read_text().split()
        if len(parts) == 2 and parts[1].startswith('revision='):
            key = parts[0] + ':' + parts[1].split('=', 1)[1]
            if key in ('GPIE01:1', 'GPIP01:0'):
                metadata['discKey'] = key
    return metadata


def package_assets(directory, output, packager):
    if not (directory / 'parms/gamePrms.bin').is_file():
        raise ValueError('the asset directory must contain parms/gamePrms.bin')
    if not packager.is_file():
        raise ValueError('emscripten file_packager.py was not found')
    output.mkdir(parents=True, exist_ok=True)
    metadata = inspect_assets(directory)
    metadata['packager'] = file_hash(packager)
    state_path = output / 'nectar-assets.json'
    outputs = ['nectar.data', 'nectar-assets.js', 'nectar-assets.js.metadata']
    if state_path.is_file() and all((output / name).is_file() for name in outputs):
        try:
            old = json.loads(state_path.read_text())
        except ValueError:
            old = {}
        if all(old.get(key) == value for key, value in metadata.items()):
            if all(file_hash(output / name) == old.get('outputs', {}).get(name) for name in outputs):
                print('game assets unchanged; keeping the existing package')
                return old
    with tempfile.TemporaryDirectory(prefix='nectar-assets-', dir=output.parent) as work:
        staging = Path(work)
        command = [
            sys.executable, str(packager.resolve()), 'nectar.data',
            '--preload', f'{directory.resolve()}@/assets/dataDir',
            '--js-output=nectar-assets.js', '--separate-metadata', '--no-node', '--quiet'
        ]
        movies = directory.parent / 'MovieData'
        if movies.is_dir():
            command += ['--preload', f'{movies.resolve()}@/assets/MovieData']
        subprocess.run(command, cwd=staging, check=True)
        metadata['version'] = 1
        metadata['outputs'] = {name: file_hash(staging / name) for name in outputs}
        for name in outputs:
            os.replace(staging / name, output / name)
        staged_state = staging / 'nectar-assets.json'
        staged_state.write_text(json.dumps(metadata, indent=2) + '\n')
        os.replace(staged_state, state_path)
    print(f'packaged {metadata["fileCount"]} files; {metadata["bytes"]} bytes')
    return metadata


def main():
    parser = argparse.ArgumentParser(description='package private game data separately from the engine')
    parser.add_argument('directory', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--packager', type=Path, required=True)
    args = parser.parse_args()
    try:
        package_assets(args.directory, args.output, args.packager)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f'asset packaging failed: {error}\n')


if __name__ == '__main__':
    main()
