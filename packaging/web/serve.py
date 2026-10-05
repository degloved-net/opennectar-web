import argparse
import json
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, unquote, urlsplit
import webbrowser


class Handler(SimpleHTTPRequestHandler):
    extensions_map = {**SimpleHTTPRequestHandler.extensions_map, '.wasm': 'application/wasm'}
    threaded = True

    def end_headers(self):
        if self.threaded:
            self.send_header('Cross-Origin-Opener-Policy', 'same-origin')
            self.send_header('Cross-Origin-Embedder-Policy', 'require-corp')
            self.send_header('Cross-Origin-Resource-Policy', 'same-origin')
        url = urlsplit(self.path)
        versioned_data = url.path == '/nectar.data' and 'v' in parse_qs(url.query)
        self.send_header('Cache-Control', 'public, max-age=31536000, immutable' if versioned_data else 'no-cache')
        self.send_header('X-Content-Type-Options', 'nosniff')
        super().end_headers()

    def send_head(self):
        host = self.headers.get('Host', '').split(':', 1)[0].lower()
        if host not in ('localhost', '127.0.0.1'):
            self.send_error(403, 'local requests only')
            return None
        parts = unquote(urlsplit(self.path).path).replace('\\', '/').split('/')
        if any(part.startswith('.') for part in parts if part):
            self.send_error(403, 'hidden paths are not served')
            return None
        root = Path(self.directory).resolve()
        path = Path(self.translate_path(self.path)).resolve()
        if not path.is_relative_to(root):
            self.send_error(403, 'path is outside the build directory')
            return None
        if urlsplit(self.path).path == '/':
            self.path = '/nectar.html'
        return super().send_head()

    def list_directory(self, path):
        self.send_error(403, 'directory browsing is disabled')
        return None


def main(argv=None):
    parser = argparse.ArgumentParser(description='serve a local open nectar web build')
    parser.add_argument('--directory', type=Path, default=Path(__file__).resolve().parent)
    parser.add_argument('--port', type=int, default=8000)
    parser.add_argument('--open', action='store_true', dest='open_browser')
    args = parser.parse_args(argv)
    if not args.directory.is_dir():
        parser.error('the web build directory does not exist')
    if not 1 <= args.port <= 65535:
        parser.error('port must be between 1 and 65535')
    try:
        build = json.loads((args.directory / 'nectar-build.json').read_text(encoding='utf-8'))
        threaded = build.get('threaded', True)
        if not isinstance(threaded, bool):
            parser.error('the build threading mode is invalid')
    except FileNotFoundError:
        threaded = True
    except (OSError, ValueError, AttributeError) as error:
        parser.error(f'could not read the build manifest: {error}')
    build_handler = type('BuildHandler', (Handler,), {'threaded': threaded})
    handler = partial(build_handler, directory=str(args.directory.resolve()))
    try:
        server = ThreadingHTTPServer(('127.0.0.1', args.port), handler)
    except OSError as error:
        parser.exit(1, f'could not start the local server: {error}\n')
    address = f'http://127.0.0.1:{args.port}/nectar.html'
    print(f'open {address}\nkeep this window open while playing. press ctrl+c to stop.', flush=True)
    if args.open_browser:
        webbrowser.open(address)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == '__main__':
    main()
