"""Command-line entry point for the gitar control server."""

from __future__ import annotations

import argparse
import json
import threading
import webbrowser as webbrowser
from collections.abc import Callable

import uvicorn as uvicorn

from gitar_server import __version__
from gitar_server.api import create_app
from gitar_server.config import config_path, load_config

DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 7343
_BROWSER_DELAY_SECONDS = 1.0
_LOOPBACK_HOSTS = frozenset({"127.0.0.1", "localhost", "::1"})


def _schedule_open(delay: float, callback: Callable[[], None]) -> None:
    timer = threading.Timer(delay, callback)
    timer.daemon = True
    timer.start()


def _open_browser_when_ready(host: str, port: int) -> None:
    if host not in _LOOPBACK_HOSTS:
        return
    url = f"http://{host}:{port}"

    def _open() -> None:
        webbrowser.open(url)

    _schedule_open(_BROWSER_DELAY_SECONDS, _open)


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="gitard",
        description="Run and inspect the gitar control server.",
    )
    parser.add_argument(
        "-V",
        "--version",
        action="version",
        version=f"gitar {__version__}",
    )
    subparsers = parser.add_subparsers(dest="command")
    serve = subparsers.add_parser("serve", help="run the control server")
    serve.add_argument("--host", default=DEFAULT_HOST, help="bind address")
    serve.add_argument("--port", type=int, default=DEFAULT_PORT, help="bind port")
    serve.add_argument("--open", action="store_true", help="open the web UI in a browser")
    serve.add_argument("--reload", action="store_true", help="reload on source changes")
    subparsers.add_parser("version", help="print the gitar version")
    subparsers.add_parser("config", help="print the config file path and contents")
    return parser


def _serve(args: argparse.Namespace) -> int:
    if args.open:
        _open_browser_when_ready(args.host, args.port)
    if args.reload:
        uvicorn.run(
            "gitar_server.api:create_app",
            factory=True,
            host=args.host,
            port=args.port,
            reload=True,
        )
    else:
        uvicorn.run(create_app(), host=args.host, port=args.port)
    return 0


def _print_config() -> int:
    print(f"config file: {config_path()}")
    print(json.dumps(load_config().model_dump(), indent=2))
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = _build_parser()
    try:
        args = parser.parse_args(argv)
    except SystemExit as exc:
        code = exc.code
        return code if isinstance(code, int) else 1
    if args.command is None:
        args = parser.parse_args(["serve"])
    if args.command == "serve":
        return _serve(args)
    if args.command == "version":
        print(f"gitar {__version__}")
        return 0
    return _print_config()


if __name__ == "__main__":
    raise SystemExit(main())
