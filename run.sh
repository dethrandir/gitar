#!/bin/sh
# gitar v2 developer runner: build the engine, set up the venv, serve the web UI.
#
#   ./run.sh [--rebuild] [--no-open] [--no-engine] [-- <extra gitard args>]
#
# Options:
#   --rebuild     Remove engine/build and configure+build from scratch
#   --no-open     Do not open the web UI in a browser
#   --no-engine   Skip building the engine (use an existing binary)
#   -h, --help    Print this help and exit
#
# Environment:
#   GITAR_ENGINE_BIN  Engine binary to use; defaults to engine/build/gitar-engine

set -eu

ROOT=$(cd "$(dirname "$0")" && pwd)
ENGINE_BIN="$ROOT/engine/build/gitar-engine"
VENV="$ROOT/.venv"

usage() {
    cat <<'EOF'
Usage: ./run.sh [--rebuild] [--no-open] [--no-engine] [-- <extra gitard args>]

Build the v2 engine (if needed), set up the Python venv (if needed), and start
the web UI.

Options:
  --rebuild     Remove engine/build and configure+build from scratch
  --no-open     Do not open the web UI in a browser
  --no-engine   Skip building the engine
  -h, --help    Print this help and exit

Everything after -- is forwarded to 'gitard serve'.
EOF
}

step() { printf '==> %s\n' "$*"; }

need() {
    if ! command -v "$1" >/dev/null 2>&1; then
        printf 'ERROR: %s is required to build the engine but was not found on PATH.\n' "$1" >&2
        printf 'Install it, or pass --no-engine to use an existing engine binary.\n' >&2
        exit 1
    fi
}

REBUILD=0
NO_OPEN=0
NO_ENGINE=0

while [ $# -gt 0 ]; do
    case "$1" in
        --rebuild) REBUILD=1 ;;
        --no-open) NO_OPEN=1 ;;
        --no-engine) NO_ENGINE=1 ;;
        -h|--help) usage; exit 0 ;;
        --) shift; break ;;
        *)
            printf 'Unknown option: %s\n' "$1" >&2
            usage >&2
            exit 2
            ;;
    esac
    shift
done

if [ "$NO_ENGINE" -eq 0 ]; then
    if [ "$REBUILD" -eq 1 ]; then
        step "Removing engine/build for a clean rebuild"
        rm -rf "$ROOT/engine/build"
    fi
    if [ -f "$ENGINE_BIN" ]; then
        step "Engine already built: $ENGINE_BIN"
    else
        need cmake
        need ninja
        step "Configuring the engine (cmake, Release, Ninja)"
        cmake -S "$ROOT/engine" -B "$ROOT/engine/build" -G Ninja -DCMAKE_BUILD_TYPE=Release
        step "Building the engine"
        cmake --build "$ROOT/engine/build"
    fi
    if [ -z "${GITAR_ENGINE_BIN:-}" ]; then
        GITAR_ENGINE_BIN="$ENGINE_BIN"
        export GITAR_ENGINE_BIN
    fi
else
    step "Skipping the engine build (--no-engine)"
fi

if [ ! -d "$VENV" ]; then
    step "Creating the Python virtual environment (.venv)"
    python3 -m venv "$VENV"
fi

if [ ! -x "$VENV/bin/gitard" ]; then
    step "Installing the control server into .venv"
    "$VENV/bin/python" -m pip install --quiet --upgrade pip
    (cd "$ROOT" && "$VENV/bin/python" -m pip install --quiet -e ".[dev]")
else
    step "Control server already installed in .venv"
fi

step "Starting the web UI (gitard serve)"
if [ "$NO_OPEN" -eq 0 ]; then
    exec "$VENV/bin/gitard" serve --open "$@"
else
    exec "$VENV/bin/gitard" serve "$@"
fi
