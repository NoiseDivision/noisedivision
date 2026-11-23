#!/usr/bin/env python3
"""Preview the website offline: serves the docs/ folder on this computer and opens it in your browser.

    python preview.py          (or double-click preview.bat on Windows / preview.command on macOS)

Everything works as it will online (pages, menus, the before/after players), except the download
buttons, which point at the GitHub release. Stop it with Ctrl+C or by closing the window."""
import functools, http.server, pathlib, socketserver, threading, webbrowser

DOCS = pathlib.Path(__file__).resolve().parent / "docs"
Handler = functools.partial(http.server.SimpleHTTPRequestHandler, directory=str(DOCS))

for port in range(8000, 8020):
    try:
        server = socketserver.TCPServer(("127.0.0.1", port), Handler)
        break
    except OSError:
        continue
else:
    raise SystemExit("No free port between 8000 and 8019.")

url = f"http://127.0.0.1:{port}/"
print(f"Website preview at {url}  (Ctrl+C to stop)")
threading.Timer(0.5, lambda: webbrowser.open(url)).start()
try:
    server.serve_forever()
except KeyboardInterrupt:
    pass
