"""HTTP/WebSocket control API for the gitar server."""

from .app import create_app

__all__ = ["create_app"]
