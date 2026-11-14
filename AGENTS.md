# AGENTS.md

Guidance for automated agents working in this repository.

## What this project is

`gitar` routes an electric guitar from an audio interface to headphones and can
run it through neural amp models. Historically Linux-only (bash + PipeWire +
Guitarix); now being rebuilt as a cross-platform app:

- `engine/` — native C++20 real-time audio engine (`gitar-engine`).
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
- C++: C++20, `clang-format` clean.

## Setup

```sh
python3 -m venv .venv
. .venv/bin/activate
python -m pip install -e ".[dev]"
```

## Versioning

The Python package version (`src/gitar_server/__init__.py`) and the engine
version (`engine/include/gitar/version.hpp`) must match; `tests/test_version.py`
enforces this. Bump both together with:

```sh
python scripts/bump_version.py <major|minor|patch>
python scripts/bump_version.py 2.1.0
```

Add a `CHANGELOG.md` entry for every version change. A version is not released
until its tag is pushed; see `## Releasing`.

## Gate commands (must pass before a commit)

Run the ones that apply to what changed:

```sh
# Shell
shellcheck install.sh uninstall.sh gitar run.sh scripts/install-v2.sh scripts/*.sh

# PowerShell (Windows): parse-only syntax gate. CI runs this on windows-latest
# for scripts/install.ps1, scripts/uninstall.ps1, and scripts/run.ps1; there is
# no PowerShell runtime on the Linux dev box, so it cannot be run locally here.

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
git ls-files engine | grep -E '\.(cpp|hpp|cc|h)$' | grep -v '^engine/third_party/' \
    | xargs -r clang-format --dry-run --Werror
```

## Rules for agents

- **You own the git flow**: run `git add` / `git commit` / `git push`, and create
  and push release tags, yourself. A finished change is committed evidence, not
  a working tree full of edits. Re-read `## Releasing` before you tag anything.
- **Do not** weaken or skip tests, loosen types, or add hacks to make a gate pass.
- **Do not** refactor outside the scope of the assigned unit.
- Every new behavior starts with a failing test where practical (TDD).
- Report evidence: exact commands run and their raw output.

## Releasing

A pushed tag is what publishes, so treat it as the last step, not a decoration.

1. Add the `CHANGELOG.md` entry and bump the Python and engine versions
   together with `python scripts/bump_version.py <version>`.
2. Run the gate commands above on the exact commit you are about to tag.
3. Push `main`, then tag and push the tag:

   ```sh
   git tag -a v2.1.4 -m v2.1.4
   git push origin main v2.1.4
   ```

4. `.github/workflows/release.yml` builds the engine for Linux and Windows plus
   the wheel and sdist, then opens the GitHub release with generated notes and
   attaches every artifact. Do not create the release by hand, and do not tag
   twice: watch the run (`gh run watch`) and confirm the assets landed
   (`gh release view <tag>`).
5. PyPI publishing stays opt-in through the `PYPI_PUBLISH` repository variable.

## Roadmap

See `ROADMAP.md`. Check boxes only after the orchestrator independently verifies.
