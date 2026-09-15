#!/bin/sh
# gitar kurulumu
#
#   curl -fsSL https://raw.githubusercontent.com/dethrandir/gitar/main/install.sh | sh
#
# Seçenekler (curl ile: ... | sh -s -- --no-deps):
#   --no-deps    Paket kurma, sadece gitar komutunu kur/güncelle
#   --no-setup   Kurulumdan sonra ses kartı seçme sihirbazını açma
#
# Ortam değişkenleri:
#   GITAR_BIN_DIR   Komutun kurulacağı klasör (varsayılan: ~/.local/bin)
#   GITAR_REF       İndirilecek dal/etiket (varsayılan: main)

set -eu

REPO="dethrandir/gitar"
REF="${GITAR_REF:-main}"
BIN_DIR="${GITAR_BIN_DIR:-$HOME/.local/bin}"
RAW="https://raw.githubusercontent.com/$REPO/$REF"

DEPS=1
SETUP=1
for arg in "$@"; do
    case "$arg" in
        --no-deps) DEPS=0 ;;
        --no-setup) SETUP=0 ;;
        -h|--help) sed -n '2,14p' "$0" 2>/dev/null | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) printf 'Bilinmeyen seçenek: %s\n' "$arg" >&2; exit 2 ;;
    esac
done

if [ -t 1 ]; then
    B=$(printf '\033[1m'); G=$(printf '\033[32m'); Y=$(printf '\033[33m'); R=$(printf '\033[31m'); N=$(printf '\033[0m')
else
    B=""; G=""; Y=""; R=""; N=""
fi
adim()  { printf '%s==>%s %s\n' "$B" "$N" "$*"; }
tamam() { printf '  %s✓%s %s\n' "$G" "$N" "$*"; }
uyari() { printf '  %s!%s %s\n' "$Y" "$N" "$*" >&2; }
hata()  { printf '%sHATA:%s %s\n' "$R" "$N" "$*" >&2; exit 1; }
var()   { command -v "$1" >/dev/null 2>&1; }

# curl | sh ile çalışınca stdin boru olur; soru sormak için terminali kullan
tty_var() { (: </dev/tty) 2>/dev/null; }

[ "$(id -u)" -ne 0 ] || hata "root olarak çalıştırma. gitar kullanıcı başına kurulur; gerekirse sudo'yu kendisi ister."
[ "$(uname -s)" = Linux ] || hata "gitar sadece Linux'ta çalışır (PipeWire gerekiyor)."

# ---------------------------------------------------------------- paketler

paket_yoneticisi() {
    for pm in dnf apt-get pacman zypper xbps-install; do
        var "$pm" && { echo "$pm"; return; }
    done
    echo ""
}

# komut adı -> paket adı (dağıtıma göre)
paket_adi() {
    case "$1:$2" in
        dnf:guitarix|apt-get:guitarix|pacman:guitarix|zypper:guitarix|xbps-install:guitarix) echo guitarix ;;
        dnf:pw-link)          echo pipewire-utils ;;
        apt-get:pw-link)      echo pipewire-bin ;;
        pacman:pw-link)       echo pipewire ;;
        zypper:pw-link)       echo pipewire-tools ;;
        xbps-install:pw-link) echo pipewire ;;
        dnf:pactl|apt-get:pactl|zypper:pactl) echo pulseaudio-utils ;;
        pacman:pactl)         echo libpulse ;;
        xbps-install:pactl)   echo pulseaudio-utils ;;
        dnf:pw-jack)          echo pipewire-jack-audio-connection-kit ;;
        apt-get:pw-jack)      echo pipewire-jack ;;
        pacman:pw-jack)       echo pipewire-jack ;;
        zypper:pw-jack)       echo pipewire-libjack-0_3 ;;
        xbps-install:pw-jack) echo libjack-pipewire ;;
        pacman:python3)       echo python ;;
        *:python3)            echo python3 ;;
        dnf:pgrep|pacman:pgrep|xbps-install:pgrep) echo procps-ng ;;
        *:pgrep)              echo procps ;;
        *) echo "" ;;
    esac
}

# Bir veya daha fazla paketi kurar; başarısızsa 1 döner
kur() {
    if tty_var; then girdi=/dev/tty; else girdi=/dev/null; fi
    case "$PM" in
        dnf)          $SUDO dnf install -y "$@" <"$girdi" ;;
        apt-get)      $SUDO apt-get install -y "$@" <"$girdi" ;;
        pacman)       if [ "$girdi" = /dev/tty ]; then $SUDO pacman -S --needed "$@" </dev/tty
                      else $SUDO pacman -S --needed --noconfirm "$@"; fi ;;
        zypper)       $SUDO zypper --non-interactive install "$@" <"$girdi" ;;
        xbps-install) $SUDO xbps-install -Sy "$@" <"$girdi" ;;
    esac
}

bagimliliklari_kur() {
    adim "Gerekli programlar kontrol ediliyor"
    eksik=""
    for c in pw-link pw-record pactl python3 pgrep guitarix pw-jack; do
        if var "$c"; then
            tamam "$c"
        else
            eksik="$eksik $c"
        fi
    done
    [ -n "$eksik" ] || return 0

    PM=$(paket_yoneticisi)
    if [ -z "$PM" ]; then
        uyari "Paket yöneticin tanınmadı. Şunları kendin kur:$eksik"
        uyari "(PipeWire araçları: pw-link, pw-record; pactl; python3; guitarix)"
        return 0
    fi

    if var sudo; then SUDO=sudo
    elif var doas; then SUDO=doas
    else
        uyari "sudo/doas yok. Şunları kendin kur:$eksik"
        return 0
    fi

    paketler=""
    for c in $eksik; do
        p=$(paket_adi "$PM" "$c")
        [ "$c" = pw-record ] && p=$(paket_adi "$PM" pw-link)
        case " $paketler " in *" $p "*) ;; *) paketler="$paketler $p" ;; esac
    done
    # shellcheck disable=SC2086
    set -- $paketler

    adim "Paketler kuruluyor ($PM):$paketler"
    # Arch'ta -Sy ile yarım güncelleme yapmıyoruz; paket bulunamazsa önce sistem güncellenmeli
    [ "$PM" = apt-get ] && { $SUDO apt-get update </dev/null || true; }

    if ! kur "$@"; then
        [ "$PM" = pacman ] && uyari "Paket veritabanı eski olabilir: önce 'sudo pacman -Syu' çalıştırıp kurulumu tekrarla"
        uyari "Toplu kurulum başarısız oldu, paketler tek tek deneniyor"
        kurulamayan=""
        for p in "$@"; do kur "$p" || kurulamayan="$kurulamayan $p"; done
        [ -z "$kurulamayan" ] || uyari "Kurulamayan paketler:$kurulamayan"
    fi

    for c in $eksik; do
        if var "$c"; then tamam "$c"
        elif [ "$c" = pw-jack ]; then uyari "pw-jack yok; Guitarix JACK'e bağlanamazsa pipewire-jack paketini kur"
        else uyari "$c hâlâ eksik"
        fi
    done
}

pipewire_kontrol() {
    var pactl || return 0
    if LC_ALL=C pactl info 2>/dev/null | grep -q "on PipeWire"; then
        tamam "Ses sunucusu PipeWire"
    else
        uyari "Ses sunucusu PipeWire görünmüyor. gitar PipeWire olmadan çalışmaz."
        uyari "Dağıtımının belgelerinde \"PipeWire'a geçiş\" bölümüne bak."
    fi
}

# ---------------------------------------------------------------- komut

indir() { # $1 url, $2 hedef
    if var curl; then curl -fsSL "$1" -o "$2"
    elif var wget; then wget -qO "$2" "$1"
    else hata "curl ya da wget gerekli."
    fi
}

gitar_kur() {
    adim "gitar komutu kuruluyor: $BIN_DIR/gitar"
    mkdir -p "$BIN_DIR"
    gecici=$(mktemp "${TMPDIR:-/tmp}/gitar.XXXXXX")
    trap 'rm -f "$gecici"' EXIT

    kaynak=""
    case "$0" in
        */install.sh|install.sh)
            dizin=$(cd "$(dirname "$0")" && pwd)
            [ -f "$dizin/gitar" ] && kaynak="$dizin/gitar" ;;
    esac

    if [ -n "$kaynak" ]; then
        cp "$kaynak" "$gecici"
        tamam "Yerel dosyadan: $kaynak"
    else
        indir "$RAW/gitar" "$gecici" || hata "İndirilemedi: $RAW/gitar"
        tamam "İndirildi: $RAW/gitar"
    fi

    head -n1 "$gecici" | grep -q '^#!.*bash' || hata "İndirilen dosya beklenen script değil."
    mv "$gecici" "$BIN_DIR/gitar"
    chmod 755 "$BIN_DIR/gitar"
    tamam "$("$BIN_DIR/gitar" surum)"

    case ":$PATH:" in
        *":$BIN_DIR:"*) ;;
        *)
            uyari "$BIN_DIR PATH içinde değil. Kabuğunun ayar dosyasına ekle:"
            uyari "  bash/zsh:  echo 'export PATH=\"$BIN_DIR:\$PATH\"' >> ~/.bashrc   (zsh için ~/.zshrc)"
            uyari "  fish:      fish_add_path $BIN_DIR"
            ;;
    esac
}

# ---------------------------------------------------------------- akış

printf '%sgitar kurulumu%s\n' "$B" "$N"
[ "$DEPS" -eq 1 ] && bagimliliklari_kur
pipewire_kontrol
gitar_kur

ayar="${XDG_CONFIG_HOME:-$HOME/.config}/gitar/config"
if [ "$SETUP" -eq 1 ] && [ ! -f "$ayar" ] && tty_var; then
    adim "Ses kartı seçimi"
    "$BIN_DIR/gitar" ayarla </dev/tty || uyari "Sonra tekrar dene: gitar ayarla"
fi

printf '\n%sKurulum tamam.%s\n' "$G" "$N"
cat <<EOF
  gitar amfi      Guitarix amfisiyle çal
  gitar duz       Efektsiz çal
  gitar olc       Gain seviyeni ölç
  gitar           Tüm komutlar
EOF
