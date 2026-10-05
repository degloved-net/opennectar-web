import hashlib
from pathlib import Path


def split_rom(source, destination, part_bytes=19_000_000):
    source, destination = Path(source).resolve(), Path(destination)
    if not 1 <= part_bytes <= 20_000_000:
        raise ValueError('rom parts must contain between 1 and 20000000 bytes')
    if not source.is_file() or source.suffix.lower() not in ('.iso', '.gcm', '.rvz', '.wia', '.gcz', '.ciso', '.tgc', '.wbfs'):
        raise ValueError('provide a supported local disc image')
    before = source.stat()
    if not before.st_size:
        raise ValueError('the disc image is empty')
    manifest = {'name': source.name, 'bytes': before.st_size, 'parts': []}
    (destination / 'rom').mkdir(parents=True, exist_ok=True)
    with source.open('rb') as stream:
        while chunk := stream.read(part_bytes):
            name = 'rom/part-' + str(len(manifest['parts']) + 1).zfill(5) + '.bin'
            (destination / name).write_bytes(chunk)
            manifest['parts'].append({'path': name, 'bytes': len(chunk),
                                      'sha256': hashlib.sha256(chunk).hexdigest()})
    after = source.stat()
    if (before.st_size, before.st_mtime_ns) != (after.st_size, after.st_mtime_ns) \
            or sum(part['bytes'] for part in manifest['parts']) != before.st_size:
        raise ValueError('the source rom changed during packaging; try again')
    return manifest
