#!/bin/sh
# gitar kaldırma
#
#   curl -fsSL https://raw.githubusercontent.com/dethrandir/gitar/main/uninstall.sh | sh
#
# Seçenekler (curl ile: ... | sh -s -- --keep-config):
#   --keep-config   Ses kartı ayarlarını (~/.config/gitar) silme
#
# Kurulumun yüklediği paketler (guitarix, PipeWire araçları) kaldırılmaz;
# başka programlar da kullanıyor olabilir.

set -eu

BIN_DIR="${GITAR_BIN_DIR:-$HOME/.local/bin}"
CONFIG_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/gitar"
KEEP_CONFIG=0

for arg in "$@"; do
    case "$arg" in
        --keep-config) KEEP_CONFIG=1 ;;
        -h|--help) sed -n '2,11p' "$0" 2>/dev/null | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) printf 'Bilinmeyen seçenek: %s\n' "$arg" >&2; exit 2 ;;
    esac
done

sil() {
    if [ -e "$1" ]; then
        rm -rf -- "$1"
        printf '  silindi: %s\n' "$1"
    fi
}

printf 'gitar kaldırılıyor\n'

# Açık bir gitar oturumu varsa bağlantıları kes (Guitarix de kapanır)
if [ -x "$BIN_DIR/gitar" ]; then
    "$BIN_DIR/gitar" kapat >/dev/null 2>&1 || true
fi

sil "$BIN_DIR/gitar"
if [ "$KEEP_CONFIG" -eq 0 ]; then
    sil "$CONFIG_DIR"
else
    printf '  korundu: %s\n' "$CONFIG_DIR"
fi
rm -f "${XDG_RUNTIME_DIR:-/tmp}/gitar-guitarix.log" "${XDG_RUNTIME_DIR:-/tmp}/gitar-olc.wav"

cat <<'EOF'

Tamam. Dokunulmayanlar:
  - Paketler (guitarix, pipewire araçları): paket yöneticinle kaldırabilirsin
  - Guitarix'in kendi ayarları: ~/.config/guitarix
EOF
