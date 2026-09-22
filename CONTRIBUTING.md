# Contributing

Thanks for helping improve `gitar`. This project is developed in English: code,
comments, documentation, and commit messages are all English.

## Setup

```sh
python3 -m venv .venv
. .venv/bin/activate
python -m pip install -e ".[dev]"
```

Once the native engine exists, configure it with CMake:

```sh
cmake -S engine -B engine/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build engine/build
```

## Before you commit

Run every gate that applies to your change (see [`AGENTS.md`](../AGENTS.md)):

```sh
shellcheck install.sh uninstall.sh gitar scripts/*.sh
. .venv/bin/activate
ruff check . && ruff format --check . && mypy src tests && pytest
cmake --build engine/build && ctest --test-dir engine/build --output-on-failure
```

## Workflow

1. Branch from `main`: `git checkout -b feat/short-description`.
2. Start with a failing test when the change is testable (TDD).
3. Keep each commit focused on one coherent unit of work.
4. Commit messages: short imperative subject in English, e.g.
   `server: add device discovery endpoint`, with a body explaining *why* when it
   is not obvious.
5. Open a pull request. Describe what changed, why, and paste the raw output of
   the gate commands.

## Style

- **Python:** typed, `ruff`-clean, `mypy --strict`-clean, `src/` layout.
- **C++:** C++17, `clang-format`-clean, real-time code must not allocate, lock,
  or perform I/O on the audio thread.
- **Bash:** `set -eu`, quoted variables, `shellcheck`-clean.
- **Web:** no build step required for users; vanilla HTML/CSS/JS.

## Reporting bugs

Include your OS, gitar version, the exact command you ran, and the full output.
For audio problems, also include `gitar status` and your config file.
