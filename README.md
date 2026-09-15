# gitar

Elektro gitarı USB ses kartından bilgisayarın kulaklık çıkışına bağlayan küçük bir Linux aracı. İstersen sesi önce [Guitarix](https://guitarix.org/) amfi simülatöründen geçirir.

```
Gitar ──► USB ses kartı ──► Guitarix (amfi + kabin + efekt) ──► kulaklık
```

Ses kartının kendi kulaklık çıkışını kullanamadığında (jak uymuyor, çıkış bozuk) ya da gitarı amfi sesiyle duymak istediğinde işe yarar. Arka planda PipeWire bağlantılarını (`pw-link`) kurup kaldırır. Kurduğu bağlantılar kalıcı değildir, sistem ayarlarına dokunmaz.

*English: a small PipeWire helper that routes an electric guitar from a USB audio interface to your headphones, optionally through the Guitarix amp simulator. The commands and messages are in Turkish; English aliases are listed below.*

## Kurulum

```sh
curl -fsSL https://raw.githubusercontent.com/dethrandir/gitar/main/install.sh | sh
```

Kurulum sırasıyla şunları yapar:

1. Eksik programları dağıtımının paket yöneticisiyle kurar. Bunun için `sudo` şifreni isteyebilir.
2. `gitar` komutunu `~/.local/bin` klasörüne koyar. Bu adım için root gerekmez.
3. Ses kartını, gitarın takılı olduğu kanalı ve kulaklık çıkışını sorar.

| Dağıtım | Paket yöneticisi | Durum |
|---|---|---|
| Fedora | `dnf` | test edildi |
| Debian, Ubuntu, Mint, Pop!_OS | `apt` | test edildi |
| Arch, Manjaro, EndeavourOS | `pacman` | test edildi |
| openSUSE | `zypper` | test edildi |
| Void | `xbps` | denenmedi |

Başka bir dağıtımda kurulum yine çalışır. Sadece eksik programları senin kurman gerekir: aşağıdaki [Gereksinimler](#gereksinimler) bölümüne bak.

Kurulum seçenekleri:

```sh
# Paketlere dokunmadan sadece komutu kur/güncelle
curl -fsSL https://raw.githubusercontent.com/dethrandir/gitar/main/install.sh | sh -s -- --no-deps

# Başka bir klasöre kur
curl -fsSL https://raw.githubusercontent.com/dethrandir/gitar/main/install.sh | GITAR_BIN_DIR=~/bin sh
```

Repoyu klonladıysan `./install.sh` de aynı işi yapar ve indirmek yerine yereldeki dosyayı kullanır.

## Kullanım

```sh
gitar amfi        # çalmaya başla (Guitarix amfisiyle)
```

| Komut | Ne yapar |
|---|---|
| `gitar amfi` | Gitar → Guitarix → kulaklık. Guitarix penceresinden düğmeleri çevirebilirsin; kapanınca ayarlarını kendisi kaydeder. |
| `gitar duz` | Gitar → kulaklık, efektsiz. Akort ederken ya da bir sorunu ayıklarken işe yarar. |
| `gitar kapat` | Gitar bağlantılarını keser, Guitarix'i kapatır. |
| `gitar durum` | Ayarları ve şu an neyin neye bağlı olduğunu gösterir. |
| `gitar ses 70` | Kulaklık sesini %70 yapar (en fazla 150). |
| `gitar ton temiz` | Guitarix'e temiz lambalı amfi ayarını yükler. |
| `gitar ton crunch` | Guitarix'e hafif bozuk (Vox tarzı) ayarı yükler. |
| `gitar olc` | 10 saniye çalarsın; giriş seviyenin iyi olup olmadığını söyler. |
| `gitar ayarla` | Ses kartını, kanalı ve kulaklığı yeniden seçer. |
| `gitar guncelle` | Son sürümü indirir. |

İngilizce karşılıklar da çalışır: `amp`, `direct`, `stop`, `status`, `volume`, `tone clean|crunch`, `meter`, `setup`, `update`, `version`.

### Gain ayarı

Ses kartındaki **gain** (INST / GAIN) düğmesi, gitarın karta ne kadar güçlü girdiğini belirler. Duyduğun sesin yüksekliği ise `gitar ses` ile ayarlanır.

- Normal çalarken ses kartındaki **clip** ışığı yanmamalı. En sert vuruşta arada bir yanması sorun değil.
- `gitar olc` çıktısında **−18 ile −6 dBFS** arası iyi bir seviyedir.
- Ses çok kısıksa ya da hiç gelmiyorsa ilk bakman gereken yer gain düğmesi.

## Ayarlar

`gitar ayarla` bu dosyayı oluşturur: `~/.config/gitar/config`

```sh
GIRIS=alsa_input.usb-Burr-Brown_from_TI_USB_Audio_CODEC-00.analog-stereo-input
KANAL=capture_FR
CIKIS=alsa_output.usb-C-Media_Electronics_Inc._USB_Audio_Device-00.analog-stereo
GECIKME=128/48000
```

| Değişken | Anlamı |
|---|---|
| `GIRIS` | Gitarın takılı olduğu ses kartı. Listeyi görmek için: `pactl list short sources` |
| `KANAL` | O karttaki gitar kanalı. İki girişli kartlarda genelde 1. giriş `capture_FL`, 2. giriş `capture_FR` olur. |
| `CIKIS` | Kulaklığın takılı olduğu çıkış. Listeyi görmek için: `pactl list short sinks` |
| `GECIKME` | Buffer boyutu / örnekleme hızı. `128/48000` yaklaşık 2.7 ms'dir. Cızırtı olursa `256/48000` yap. |

`GITAR_RPC_PORT` ortam değişkeni, `gitar ton` komutunun Guitarix'le konuştuğu portu değiştirir (varsayılan 7342).

## Gereksinimler

- Linux ve ses sunucusu olarak **PipeWire**. Güncel Fedora, Ubuntu, Debian ve Arch'ta varsayılan olarak geliyor.
- `bash`, `python3`, `pgrep` (procps)
- PipeWire araçları: `pw-link`, `pw-record`
- `pactl` (pulseaudio-utils / libpulse)
- `guitarix`, sadece `amfi` ve `ton` komutları için
- `pw-jack` (pipewire-jack). JACK kütüphanesi PipeWire'a yönlendirilmemiş sistemlerde Guitarix'in çalışması için gerekir.

## Kaldırma

```sh
curl -fsSL https://raw.githubusercontent.com/dethrandir/gitar/main/uninstall.sh | sh
```

Bu komut `gitar` komutunu ve `~/.config/gitar` klasörünü siler. Ayarlarını tutmak için komutu `| sh -s -- --keep-config` ile bitir. Kurulumun yüklediği paketler ve Guitarix'in kendi ayarları (`~/.config/guitarix`) olduğu gibi kalır.

## Sorun giderme

**Hiç ses yok.**
Önce `gitar durum` yaz. Bağlantı yoksa `gitar amfi` çalıştır. Sonra `gitar duz` dene:
- Düz modda duyuyorsan sorun Guitarix'te.
- Düz modda da duymuyorsan gain düğmesine ya da kablolara bak.

Ses kartının sinyal ışığı çalarken hiç yanmıyorsa ses karta hiç ulaşmıyordur.

**Tek kulaktan geliyor ya da `gitar olc` "çok düşük" diyor.**
Kanal yanlış seçilmiş olabilir. `gitar ayarla` ile diğer kanalı (`capture_FL` / `capture_FR`) dene.

**Cızırtı, çıtırtı, kesik kesik ses.**
Ayar dosyasında `GECIKME=256/48000` yap. Sonra `gitar kapat` ve `gitar amfi` çalıştır.

**"Gitar girişi bulunamadı".**
Ses kartını çıkarıp tak. Başka bir kart kullanıyorsan `gitar ayarla` ile yeniden seç.

**Guitarix açılmıyor.**
Logu oku: `$XDG_RUNTIME_DIR/gitar-guitarix.log`. Çoğu zaman sebep `pipewire-jack` paketinin eksik olmasıdır.

## Nasıl çalışıyor

- **`duz`:** gitar kanalını çıkışın sol ve sağ portlarına `pw-link` ile bağlar. Mono kartlarda da iki kulaktan duyulur.
- **`amfi`:** Guitarix'i `-J` ile (kendi kendine bağlanmadan) başlatır ve zinciri elle kurar: `giriş → gx_head_amp → gx_head_fx → çıkış`.
- **`ton`:** Guitarix'i birkaç saniyeliğine JSON-RPC portuyla açar, hazır ayarı yükler, sonra portsuz yeniden başlatır. Guitarix bu portu yalnızca bu bilgisayara kısıtlayamadığı, tüm ağ arayüzlerinde dinlediği için port hep açık bırakılmaz.
- **`olc`:** `pw-record` ile 10 saniye kayıt alır ve gitar kanalının en yüksek seviyesini dBFS olarak hesaplar.

## Lisans

[MIT](LICENSE)
