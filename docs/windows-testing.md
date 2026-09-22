# Windows manual test checklist

The engine and control server are built and unit-tested on Windows in CI, but
the end-to-end audio path has not been exercised on real Windows hardware yet.
Work through this list on a Windows machine and file anything that fails.

## Setup

- [ ] `scripts/install.ps1` runs to completion and installs `gitar-engine.exe`.
- [ ] A new terminal can run `gitar-engine version` and `gitar-engine devices`.
- [ ] `gitard version` and `gitard config` work.
- [ ] `gitard serve --open` opens the web UI at <http://127.0.0.1:7343>.

## Devices

- [ ] The **Engine** panel lists the audio interface as an input and the
      headphones/speakers as an output.
- [ ] The default devices are marked `(default)`.
- [ ] Selecting a non-default output works.

## Audio

- [ ] `Start` with the interface as input and headphones as output produces
      sound with `Direct` (no model) — i.e. the guitar is audible.
- [ ] Latency is acceptable at `period_frames = 128`; try `256` if it crackles.
- [ ] Input and output level bars move while playing.
- [ ] No xrun counters climb steadily after the first second.

## Neural models

- [ ] Put a `.nam` file in `%APPDATA%\gitar\models` (or set `GITAR_MODELS_DIR`)
      and it appears in the model list.
- [ ] Loading a model changes the tone; the model name shows in the status.
- [ ] `Clear` returns to the dry signal.
- [ ] The gain slider and the noise gate behave as expected.

## Teardown

- [ ] `Stop` stops audio and the `gitar-engine` process exits.
- [ ] Closing the server (`Ctrl+C` on `gitard serve`) also stops the engine.
- [ ] `scripts/uninstall.ps1` removes the binary and PATH entry, and (without
      `-KeepConfig`) the config directory.

## Report

For each failure include: Windows version, `gitar-engine version`, the exact
step, and the full output/error.
