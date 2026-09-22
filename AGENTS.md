# AGENTS.md

Guidance for automated agents working in this repository.

## What this project is

`gitar` routes an electric guitar from an audio interface to headphones and can
run it through neural amp models. Historically Linux-only (bash + PipeWire +
Guitarix); now being rebuilt as a cross-platform app:

- `engine/` — native C++17 real-time audio engine (`gitar-engine`).
- `src/gitar_server/` — Python control server (FastAPI) + `gitard` CLI. **Never**
  in the audio path.
- `web/` — static local web UI served by the control server.
- `gitar`, `install.sh`, `uninstall.sh` — legacy bash CLI and installers. Keep
  the published one-liner working; do not move or rename these at the repo root.

## Language and style

- Code, identifiers, comments, docs, commit messages: **English**.
- Comments only for non-obvious *why*. Do not narrate *what* the code does.
- Bash: `set -u`/`set -eu`, quote variables, `shellcheck`-clean.
- Python: type-annotated, `ruff` + `mypy --strict` clean, `src/` layout.
- C++: C++17, `clang-format` clean.

## Setup

```sh
python3 -m venv .venv
. .venv/bin/activate
python -m pip install -e ".[dev]"
```

## Gate commands (must pass before a commit)

Run the ones that apply to what changed:

```sh
# Shell
shellcheck install.sh uninstall.sh gitar scripts/*.sh

# Python
. .venv/bin/activate
ruff check .
ruff format --check .
mypy src tests
pytest

# Native engine (once engine/ exists)
cmake -S engine -B engine/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build engine/build
ctest --test-dir engine/build --output-on-failure
files=$(git ls-files engine | grep -E '\.(cpp|hpp|cc|h)$' | grep -v '^engine/third_party/' || true)
[ -z "$files" ] || clang-format --dry-run --Werror $files
```

## Rules for agents

- **Do not** run `git add` / `git commit` / `git push`. The orchestrator commits.
- **Do not** weaken or skip tests, loosen types, or add hacks to make a gate pass.
- **Do not** refactor outside the scope of the assigned unit.
- Every new behavior starts with a failing test where practical (TDD).
- Report evidence: exact commands run and their raw output.

## Roadmap

See `ROADMAP.md`. Check boxes only after the orchestrator independently verifies.
