# Troubleshooting

## No sound at all

Run `gitar status` first. If nothing is connected, run `gitar amp`. Then try
`gitar direct`:

- If you hear sound in direct mode, the problem is in Guitarix.
- If you still hear nothing, check the gain knob and the cables.

If the interface's signal LED never lights up while playing, the signal is not
reaching the interface at all.

## Only one ear, or `gitar meter` says "too low"

The channel is probably wrong. Run `gitar setup` and try the other channel
(`capture_FL` / `capture_FR`).

## Crackling, popping, choppy audio

Set `GECIKME=256/48000` in the config file, then run `gitar stop` and `gitar amp`.

## "Guitar input not found"

Unplug and replug the interface. If you are using a different interface, run
`gitar setup` to select it again.

## Guitarix does not start

Read the log at `$XDG_RUNTIME_DIR/gitar-guitarix.log`. The most common cause is
the `pipewire-jack` package missing.

## `gitar` command not found after install

`~/.local/bin` is not on your `PATH`. Add it:

```sh
# bash
echo 'export PATH="$HOME/.local/bin:$PATH"' >> ~/.bashrc
# zsh
echo 'export PATH="$HOME/.local/bin:$PATH"' >> ~/.zshrc
# fish
fish_add_path ~/.local/bin
```
