from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer


class WasmDemoHandler(SimpleHTTPRequestHandler):
    extensions_map = {
        **SimpleHTTPRequestHandler.extensions_map,
        ".js": "text/javascript",
        ".mjs": "text/javascript",
        ".wasm": "application/wasm",
        ".data": "application/octet-stream",
    }

    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cache-Control", "no-cache")
        super().end_headers()


if __name__ == "__main__":
    address = ("127.0.0.1", 8082)
    with ThreadingHTTPServer(address, WasmDemoHandler) as server:
        print(f"Serving LBF WASM demo at http://{address[0]}:{address[1]}/")
        server.serve_forever()
