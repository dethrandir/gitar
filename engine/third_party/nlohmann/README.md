# nlohmann/json

Vendored single-header JSON library, used by the engine control server only.

- Upstream: https://github.com/nlohmann/json
- Version: `3.11.3`
- File: `json.hpp`
- SHA-256: `9bea4c8066ef4a1c206b2be5a36302f8926f7fdc6087af5d20b417d0cf103ea6`
- License: MIT — see the header for the full text.

Include it as `#include "nlohmann/json.hpp"`. It is included by exactly one
translation unit (`src/control_server.cpp`) to keep compile times low.

To update: replace `json.hpp`, update the version and SHA-256 above.
