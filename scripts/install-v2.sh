#!/bin/sh
# gitar v2 installer for Linux (preview).
#
#   curl -fsSL https://raw.githubusercontent.com/dethrandir/gitar/main/scripts/install-v2.sh | sh
#
# Options (with curl: ... | sh -s -- --no-python):
#   --version <tag>   Release tag to install (default: latest)
#   --bin-dir <dir>   Where to install gitar-engine
#                     (default: ${GITAR_BIN_DIR:-$HOME/.local/bin})
#   --no-python       Skip the Python control server
#   --no-engine       Skip the engine binary
#   -h, --help        Print this help and exit
#
# Environment:
#   GITAR_BIN_DIR   Default directory for the engine binary

set -eu

REPO="dethrandir/gitar"
API="https://api.github.com/repos/$REPO"
DOWNLOAD="https://github.com/$REPO/releases/download"

VERSION="latest"
BIN_DIR="${GITAR_BIN_DIR:-$HOME/.local/bin}"
NEED_ENGINE=1
NEED_PYTHON=1

usage() {
    cat <<'EOF'
Install the gitar v2 stack (native engine + Python control server) from a
GitHub release. No root rights are needed.

Usage:
  curl -fsSL https://raw.githubusercontent.com/dethrandir/gitar/main/scripts/install-v2.sh | sh [-- OPTIONS]

Options:
  --version <tag>   Release tag to install, e.g. v2.0.0 (default: latest)
  --bin-dir <dir>   Directory for the gitar-engine binary
                    (default: $GITAR_BIN_DIR or ~/.local/bin)
  --no-python       Do not install the Python control server
  --no-engine       Do not install the engine binary
  -h, --help        Print this help and exit

Environment:
  GITAR_BIN_DIR     Default directory for the engine binary
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
        --version)
            [ $# -ge 2 ] || { printf 'ERROR: --version requires a tag.\n' >&2; exit 2; }
            VERSION=$2
            shift 2
            ;;
        --bin-dir)
            [ $# -ge 2 ] || { printf 'ERROR: --bin-dir requires a directory.\n' >&2; exit 2; }
            BIN_DIR=$2
            shift 2
            ;;
        --no-python)
            NEED_PYTHON=0
            shift
            ;;
        --no-engine)
            NEED_ENGINE=0
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            printf 'Unknown option: %s\n' "$1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

if [ -t 1 ]; then
    B=$(printf '\033[1m'); G=$(printf '\033[32m'); Y=$(printf '\033[33m'); R=$(printf '\033[31m'); N=$(printf '\033[0m')
else
    B=""; G=""; Y=""; R=""; N=""
fi
step() { printf '%s==>%s %s\n' "$B" "$N" "$*"; }
ok()   { printf '  %s✓%s %s\n' "$G" "$N" "$*"; }
warn() { printf '  %s!%s %s\n' "$Y" "$N" "$*" >&2; }
fail() { printf '%sERROR:%s %s\n' "$R" "$N" "$*" >&2; exit 1; }
var()  { command -v "$1" >/dev/null 2>&1; }

[ "$(id -u)" -ne 0 ] || fail "Do not run as root; gitar installs per-user."
[ "$(uname -s)" = Linux ] || fail "This installer is for Linux; use scripts/install.ps1 on Windows."

WORK_DIR=$(mktemp -d "${TMPDIR:-/tmp}/gitar-v2.XXXXXX")
trap 'rm -rf "$WORK_DIR"' EXIT INT TERM

download() { # $1 url, $2 target
    if var curl; then
        curl -fsSL "$1" -o "$2"
    elif var wget; then
        wget -qO "$2" "$1"
    else
        fail "curl or wget is required to download files."
    fi
}

# Reads the tag from GitHub's release JSON; python3 parses it robustly, with a
# sed fallback for minimal systems.
json_tag_name() {
    if var python3 && python3 -c \
        'import json,sys; print(json.load(open(sys.argv[1], encoding="utf-8"))["tag_name"])' "$1"; then
        return 0
    fi
    sed -n 's/.*"tag_name"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$1" | head -n 1
}

wheel_url() {
    python3 - "$1" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as handle:
    release = json.load(handle)
for asset in release.get("assets", []):
    if asset.get("name", "").endswith(".whl"):
        print(asset["browser_download_url"])
        break
PY
}

printf '%sgitar v2 installer%s\n' "$B" "$N"

RELEASE_JSON="$WORK_DIR/release.json"

if [ "$VERSION" = latest ]; then
    step "Resolving the latest release"
    download "$API/releases/latest" "$RELEASE_JSON" || fail "Could not query the GitHub API at $API."
    TAG=$(json_tag_name "$RELEASE_JSON") || true
    [ -n "$TAG" ] || fail "Could not parse 'tag_name' from the release metadata."
else
    TAG="$VERSION"
    case "$TAG" in
        v*) ;;
        *) TAG="v$TAG" ;;
    esac
fi
ok "Release: $TAG"

if [ "$NEED_ENGINE" -eq 1 ]; then
    ASSET="gitar-engine-$TAG-linux-x64.tar.gz"
    ARCHIVE="$WORK_DIR/$ASSET"
    step "Downloading $ASSET"
    download "$DOWNLOAD/$TAG/$ASSET" "$ARCHIVE" || fail "Could not download $ASSET; is the tag correct?"

    step "Installing gitar-engine to $BIN_DIR"
    mkdir -p "$BIN_DIR"
    tar -xzf "$ARCHIVE" -C "$WORK_DIR" gitar-engine || fail "The archive did not contain gitar-engine."
    cp "$WORK_DIR/gitar-engine" "$BIN_DIR/gitar-engine"
    chmod 755 "$BIN_DIR/gitar-engine"
    ok "Installed $BIN_DIR/gitar-engine"

    if ! "$BIN_DIR/gitar-engine" version; then
        fail "The installed gitar-engine could not run."
    fi
else
    warn "Skipped the engine binary (--no-engine)"
fi

if [ "$NEED_PYTHON" -eq 1 ]; then
    if ! var python3; then
        warn "python3 was not found; skipping the Python control server."
        warn "Install Python 3.10+ and run: python3 -m pip install --user <wheel-url>"
    else
        if [ ! -f "$RELEASE_JSON" ]; then
            download "$API/releases/tags/$TAG" "$RELEASE_JSON" || true
        fi
        WHEEL=""
        if [ -f "$RELEASE_JSON" ]; then
            WHEEL=$(wheel_url "$RELEASE_JSON" 2>/dev/null) || true
        fi
        if [ -z "$WHEEL" ]; then
            warn "Release $TAG has no wheel asset; skipping the Python control server."
        else
            step "Installing the control server (pip --user)"
            if python3 -m pip install --user "$WHEEL"; then
                ok "Installed the gitar control server (gitard)."
            else
                warn "pip install failed; run it manually:"
                warn "  python3 -m pip install --user $WHEEL"
            fi
        fi
    fi
else
    warn "Skipped the Python control server (--no-python)"
fi

case ":$PATH:" in
    *":$BIN_DIR:"*) ;;
    *)
        warn "$BIN_DIR is not on your PATH. Add it to your shell profile:"
        warn "  bash/zsh:  echo 'export PATH=\"$BIN_DIR:\$PATH\"' >> ~/.bashrc   (zsh: ~/.zshrc)"
        warn "  fish:      fish_add_path $BIN_DIR"
        ;;
esac

printf '\n%sInstallation finished.%s\n' "$G" "$N"
printf '  Start the server and web UI:\n'
printf '    gitard serve --open\n'
if [ "$NEED_PYTHON" -eq 0 ]; then
    printf '  (the control server was skipped; install it later with pip)\n'
fi
