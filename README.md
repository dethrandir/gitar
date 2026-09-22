# gitar

Route an electric guitar from your audio interface to your headphones — with
neural amp models, a local web UI, and no fuss.

```
Guitar ──► audio interface ──► gitar engine ──► headphones
                               (neural amp / cabinet / FX)
```

`gitar` started as a small Linux PipeWire helper that connected a guitar input to
a headphone output, optionally through the [Guitarix](https://guitarix.org/) amp
simulator. It is now being rebuilt as a cross-platform app: a native real-time
audio engine that hosts neural amp models, a Python control server, and a local
web UI.

> **Status:** `v2` is in active development. The control server, local web UI,
> and native engine (with NAM neural amp models) already work on Linux; Windows
> and release packaging are next. The stable, published release is the Linux
> bash/PipeWire tool documented below. See [`ROADMAP.md`](ROADMAP.md).

## Why

Your audio interface has a guitar input but only one headphone jack — or a broken
one. `gitar` routes the guitar signal to any output you like, so you can practise
through headphones instead of an amp. The v2 engine adds neural amp models, so
you can play through realistic, profiled amps, cabinets, and effects.

## Install

### Linux (stable release)

```sh
curl -fsSL https://raw.githubusercontent.com/dethrandir/gitar/main/install.sh | sh
```

The installer:

1. installs missing packages with your distribution's package manager (it may
   ask for your `sudo` password),
2. installs the `gitar` command to `~/.local/bin` (no root needed),
3. asks which audio interface, channel, and headphone output to use.

| Distribution | Package manager | Status |
|---|---|---|
| Fedora | `dnf` | tested |
| Debian, Ubuntu, Mint, Pop!_OS | `apt` | tested |
| Arch, Manjaro, EndeavourOS | `pacman` | tested |
| openSUSE | `zypper` | tested |
| Void | `xbps` | untested |

On other distributions the installer still runs; you just need to install the
[requirements](#requirements) yourself.

Installer options:

```sh
# Only install/update the command, don't touch packages
curl -fsSL https://raw.githubusercontent.com/dethrandir/gitar/main/install.sh | sh -s -- --no-deps

# Install to a different directory
curl -fsSL https://raw.githubusercontent.com/dethrandir/gitar/main/install.sh | GITAR_BIN_DIR=~/bin sh
```

If you cloned the repo, `./install.sh` does the same job and uses the local file
instead of downloading.

### Windows

Windows support (WASAPI) is new and has not yet been tested on real hardware.
Install from a release with PowerShell 5.1+ (no administrator rights needed;
the installer only writes to your user profile):

```powershell
irm https://raw.githubusercontent.com/dethrandir/gitar/main/scripts/install.ps1 | iex
```

It downloads `gitar-engine.exe` and the control-server wheel from the latest
GitHub release: the engine is installed into `%LOCALAPPDATA%\gitar\bin` (added
to your user PATH), and the wheel is installed with `python -m pip install
--user`. If you cloned the repo, `./scripts/install.ps1` does the same. Options:

```powershell
./scripts/install.ps1 -Version v2.0.0   # install a specific tag
./scripts/install.ps1 -NoPython         # engine only
./scripts/install.ps1 -NoPath           # don't touch the user PATH
```

Then open a new terminal and start the server and web UI:

```powershell
gitard serve --open
```

To uninstall, remove `gitar-engine.exe` and `%APPDATA%\gitar`:

```powershell
./scripts/uninstall.ps1                 # add -KeepConfig to keep settings and models
```

The Python package itself is removed with `python -m pip uninstall gitar`.

## Usage

```sh
gitar amp         # start playing through the Guitarix amp
```

| Command | What it does |
|---|---|
| `gitar amp` | Guitar → Guitarix → headphones. Turn the knobs in the Guitarix window; it saves its settings on exit. |
| `gitar direct` | Guitar → headphones, no effects. Useful when tuning or debugging. |
| `gitar stop` | Disconnect the guitar and close Guitarix. |
| `gitar status` | Show the configuration and what is currently connected to what. |
| `gitar volume 70` | Set the headphone volume to 70% (maximum 150). |
| `gitar tone clean` | Load a clean tube-amp preset into Guitarix. |
| `gitar tone crunch` | Load a light (Vox-style) overdrive preset. |
| `gitar tone army` | Load a tight, muted-riff JCM-800 preset (Seven Nation Army). |
| `gitar meter` | Play for 10 seconds; tells you whether your input level is healthy. |
| `gitar setup` | Re-select the audio interface, channel, and headphone output. |
| `gitar update` | Download the latest version. |
| `gitar version` | Print the version. |

Turkish aliases also work: `amfi`, `duz`, `kapat`, `durum`, `ses`, `ton
temiz|crunch|army`, `olc`, `ayarla`, `guncelle`, `surum`.

### Setting the gain

The **gain** (INST / GAIN) knob on your interface sets how hard the guitar hits
the interface. How loud you hear it is set separately with `gitar volume`.

- The **clip** LED should stay off during normal playing. The occasional flash on
  the hardest pick is fine.
- A reading between **−18 and −6 dBFS** from `gitar meter` is healthy.
- If it is very quiet or silent, check the gain knob first.

## Configuration

`gitar setup` writes `~/.config/gitar/config`:

```sh
GIRIS=alsa_input.usb-Burr-Brown_from_TI_USB_Audio_CODEC-00.analog-stereo-input
KANAL=capture_FR
CIKIS=alsa_output.usb-C-Media_Electronics_Inc._USB_Audio_Device-00.analog-stereo
GECIKME=128/48000
```

| Variable | Meaning |
|---|---|
| `GIRIS` | The interface the guitar is plugged into. List them with `pactl list short sources`. |
| `KANAL` | The guitar channel on that interface. On two-input interfaces the first input is usually `capture_FL`, the second `capture_FR`. |
| `CIKIS` | The headphone output. List them with `pactl list short sinks`. |
| `GECIKME` | Buffer size / sample rate. `128/48000` is about 2.7 ms. Use `256/48000` if you hear crackling. |

The config keys are currently Turkish; the v2 control server migrates them to
English (`input`, `channel`, `output`, `latency`) while reading the old file.

`GITAR_RPC_PORT` changes the port `gitar tone` uses to talk to Guitarix
(default `7342`).

## Requirements

Linux with **PipeWire** as the sound server (the default on current Fedora,
Ubuntu, Debian, and Arch).

- `bash`, `python3`, `pgrep` (procps)
- PipeWire tools: `pw-link`, `pw-record`
- `pactl` (pulseaudio-utils / libpulse)
- `guitarix` — only for `amp` and `tone`
- `pw-jack` (pipewire-jack) — for Guitarix on systems whose JACK library is not
  routed to PipeWire

## Uninstall

```sh
curl -fsSL https://raw.githubusercontent.com/dethrandir/gitar/main/uninstall.sh | sh
```

This removes the `gitar` command and `~/.config/gitar`. Add
`| sh -s -- --keep-config` to keep your settings. Packages installed by the
installer and Guitarix's own settings (`~/.config/guitarix`) are left alone.

## Documentation

- [`docs/architecture.md`](docs/architecture.md) — how gitar is built (v2).
- [`docs/troubleshooting.md`](docs/troubleshooting.md) — common problems and fixes.
- [`ROADMAP.md`](ROADMAP.md) — the plan and its progress.

## Development

See [`AGENTS.md`](AGENTS.md) for the build/test commands and
[`CONTRIBUTING.md`](CONTRIBUTING.md) for the contribution workflow.

### Running the v2 engine and web UI (Linux, in progress)

```sh
# 1. Build the native engine (downloads pinned Eigen + NAM on first configure)
cmake -S engine -B engine/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build engine/build

# 2. Install the Python control server
python3 -m venv .venv && . .venv/bin/activate
python -m pip install -e ".[dev]"

# 3. Serve the web UI (spawns the engine on demand)
gitard serve --open
```

Then open <http://127.0.0.1:7343>. Put `.nam` models in
`~/.config/gitar/models/` (or set `GITAR_MODELS_DIR`) and select one in the
**Engine** panel.

## License

[MIT](LICENSE)
