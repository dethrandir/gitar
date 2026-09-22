"""Command-line entry point for the gitar control server."""

from gitar_server import __version__


def main() -> int:
    print(f"gitar server {__version__}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
