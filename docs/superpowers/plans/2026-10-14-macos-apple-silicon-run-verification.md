# macOS Apple Silicon — Run Verification and Remaining Hardening Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn "the port builds and opens a window" into "the port is demonstrated to run windowed with an unlocked cursor, audibly or explicitly silently, on Apple Silicon", and close the testable defects the plan's final review deferred.

**Architecture:** A checked live run becomes the acceptance gate: `tools/macos_check.py` starts `sotc.py`, captures its log, and asserts on a fixed set of markers — including a new window-state line that `WindowSDL` logs from the *live* SDL state. Around that gate, three small tasks make the remaining run-path behaviour tested rather than assumed: audio module selection, launcher/preparation edge cases, and the vcpkg readiness decision that `build.sh` gets wrong after an interrupted install.

**Tech Stack:** Python 3 (unittest, `subprocess`), bash, C11, C++20, SDL3, CMake/Ninja, vcpkg `x64-osx`.

**Spec:** `docs/superpowers/specs/2026-10-14-macos-apple-silicon-design.md`

**Predecessor:** `docs/superpowers/plans/2026-10-14-macos-apple-silicon.md` (fully executed; its 6 tasks are committed and pushed). This plan picks up where its Review Focus and the final review's deferred minors left off — it does not re-plan the build.

## Global Constraints

- Target is Apple Silicon macOS running an **x86-64** Mach-O under Rosetta 2 (`-DCMAKE_OSX_ARCHITECTURES=x86_64`). A native arm64 build is out of scope.
- The window is created with `SDL_PROP_WINDOW_CREATE_METAL_BOOLEAN = true`; `VK_EXT_metal_surface` consumes `SDL_Metal_GetLayer(...)`.
- Windowed by default: with `BB_FULLSCREEN` unset or `'0'`, `(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) == 0`.
- The cursor is never locked or grabbed: `SDL_GetWindowRelativeMouseMode(window) == false` and `SDL_GetWindowMouseGrab(window) == false` at all times, including after a click or focus gain. `SOTC_MOUSE` defaults to `'0'`.
- Sonic firmware (`libSceUlt.sprx`, `libSceNgs2.sprx`) is user-supplied and never distributed; the port must run silently and say so when it is absent.
- Do not mass-reformat. Keep `-Wall -Wextra -Werror` clean for C, and keep `bash build.sh --test` (6 test binaries) plus `python3 -m unittest discover -s tests` green after every task.

## Review Focus

1. A display smaller than the configured output resolution: the window must still open **windowed** and scaled to 90% of the usable bounds, and the acceptance gate must still pass. — test belongs to Task 1.
2. No `~/Downloads/*CUSA08809*.pkg` and no `out/CUSA08809/eboot.bin`: `sotc.py` must fail with a message naming the setting to fix, never a Python traceback. — test belongs to Task 3.
3. KosmicKrisp missing and only `MoltenVK_icd.json` present: the run must fall back to MoltenVK and either reach "presenter ready" or fail loudly naming the driver it tried. — test belongs to Task 1.
4. A game that never closes its window: the acceptance gate must SIGTERM the process group and, if that is ignored, SIGKILL it, and return within the timeout instead of hanging. — test belongs to Task 1.
5. `SOTC_SYS_MODULES` set to a folder that exists but is missing one firmware file: the warning must name the missing file, and the game must still run silently instead of crashing. — test belongs to Task 2.

---

## File Structure

- `tools/macos_check.py` — new. The acceptance gate: marker table, log checker, live runner. One responsibility: decide whether a captured run is acceptable.
- `tests/test_macos_check.py` — new. Feeds synthetic logs to the checker; no GPU needed.
- `gpu/shim/window.cpp` — modified. One added log line reporting the live windowed/mouse state (the only way the gate can observe SDL state from outside the process).
- `sotc.py` — modified. `select_sys_modules()` extracted from `main()`; `extract_pkg()` stops `unlink()`ing a real directory.
- `tests/test_sotc_modules.py`, `tests/test_sotc_pkg.py` — new.
- `launcher/sotc_launcher_win.py` — modified. `game_environment()` mouse default, `shadps4_saves_roots()`, `platform_label()`.
- `tools/vcpkg_ready.sh` — new. Answers "is the vcpkg install complete?" by artifact, not by directory.
- `tests/test_vcpkg_ready.py` — new.
- `build.sh` — modified. Uses `tools/vcpkg_ready.sh` to decide `-DVCPKG_MANIFEST_INSTALL`.
- `README.md` — modified. A macOS "build, run, verify" section in both language halves.

**As built (after the code review).** The interfaces below are the plan as written; these are the
deltas the review settled, so a reader does not follow a signature that no longer exists:

- `run()` returns a `Run(problems, log)` NamedTuple, not `(ok, log)`, and has no `env` parameter.
  `check_log()` takes `returncode=None`: a run the gate had to end is acceptable, one that ended
  itself before the deadline is not.
- `extract_pkg(pkg, out)` has no `tool` parameter: tests place a stub at `out/pkgtool/PkgTool.Core`,
  which is where the real search looks, so the seam is the filesystem rather than an argument.
- The gate's `assertion` pattern is case-insensitive (`[Aa]ssertion [Ff]ailed`) because the port
  writes `Assertion Failed!`.
- The windowed-mode clamp lives in `gpu/shim/window_scale.h` with `tests/test_window_scale.cpp`
  pinning it, so `window.cpp`'s marker line is no longer the only statement of that behaviour.

---

### Task 1: Live acceptance gate for windowed mode and the unlocked cursor

**Files:**
- Create: `tools/macos_check.py`
- Test: `tests/test_macos_check.py`
- Modify: `gpu/shim/window.cpp` (after `SDL_ShowCursor();`)

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `tools/macos_check.py`:
    - `MARKERS: tuple[tuple[str, str], ...]` — `(name, regex)` pairs.
    - `FORBIDDEN: tuple[tuple[str, str], ...]` — `(name, regex)` pairs.
    - `check_log(log: str) -> list[str]` — one entry per problem (`"missing: <name>"`, `"forbidden: <name>"`); `[]` means the run is acceptable.
    - `run(port: Path, seconds: float = 30.0, command: list[str] | None = None, env: dict | None = None) -> tuple[bool, str]` — `(ok, log)`; `command` defaults to `[sys.executable, 'sotc.py']`, which is what lets the process-group test drive a stub instead.
    - `main(argv: list[str] | None = None) -> int` — exit 0 when acceptable.
  - `window.cpp` logs exactly `Window <w>x<h> windowed: relative mouse off, grab off` (or `fullscreen:` / `mouse on` / `grab on`) once per window creation.

- [ ] **Step 1: Write the failing test**

```python
# tests/test_macos_check.py
"""The macOS acceptance gate's log checker, driven by captured logs (no GPU)."""
import sys
from pathlib import Path
import time
import unittest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))
from tools import macos_check

GOOD = """
Image at 0x1c900000 (below 2 GiB, as the game's 31-bit pointers need); loader at 0x4ea5d70
GPU [Frontend] <Info> Window 1280x720 windowed: relative mouse off, grab off
GPU: window and Vulkan presenter ready; SDK 0x04508101, 327 HLE symbols
Runtime: BPE heap guard on heap 0x1f6fc700 (2000 of 2000 frees)
"""


class CheckLog(unittest.TestCase):
    def test_accepts_a_complete_run(self):
        self.assertEqual(macos_check.check_log(GOOD), [])

    def test_accepts_a_scaled_window(self):
        # Review Focus 1: a smaller display scales the window; the marker must not fix a size.
        scaled = GOOD.replace('1280x720', '1152x648')
        self.assertEqual(macos_check.check_log(scaled), [])

    def test_reports_the_missing_window_line(self):
        self.assertEqual(macos_check.check_log(GOOD.replace('grab off', 'grab on')),
                         ['missing: windowed, unlocked cursor'])

    def test_reports_each_missing_marker(self):
        problems = macos_check.check_log('nothing useful here\n')
        self.assertEqual(problems, [f'missing: {name}' for name, _ in macos_check.MARKERS])

    def test_reports_a_forbidden_line(self):
        self.assertEqual(macos_check.check_log(GOOD + 'STOP: cannot reserve the guest range\n'),
                         ['forbidden: stopped'])

    def test_reports_a_traceback(self):
        self.assertEqual(macos_check.check_log(GOOD + 'Traceback (most recent call last):\n'),
                         ['forbidden: python traceback'])

    def test_run_kills_a_group_that_ignores_sigterm(self):  # Review Focus 4
        start = time.monotonic()
        ok, log = macos_check.run(ROOT, seconds=1.0,
                                  command=['bash', '-c', 'trap "" TERM; echo marker; sleep 30'])
        self.assertFalse(ok)
        self.assertIn('marker', log)
        self.assertLess(time.monotonic() - start, 20)


if __name__ == '__main__':
    unittest.main()
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `python3 -m unittest discover -s tests -p 'test_macos_check.py' -v`
Expected: FAIL with `ModuleNotFoundError: No module named 'tools.macos_check'`.

- [ ] **Step 3: Implement `tools/macos_check.py`**

`MARKERS` in this order: `('image below 2 GiB', r'Image at 0x[0-9a-f]+ \(below 2 GiB')`, `('windowed, unlocked cursor', r'Window \d+x\d+ windowed: relative mouse off, grab off')`, `('presenter ready', r'window and Vulkan presenter ready')`, `('heap guard', r'BPE heap guard on heap')`.
`FORBIDDEN`: `('stopped', r'STOP:')`, `('assertion', r'Assertion failed')`, `('python traceback', r'Traceback \(most recent call last\)')`.
`check_log` returns, in marker order, `f'missing: {name}'` for each marker with no `re.search`, then `f'forbidden: {name}'` for each forbidden pattern found.
`run(port, seconds=30.0, command=None, env=None)` spawns `command or [sys.executable, 'sotc.py']` with `cwd=port`, `stdout=PIPE`, `stderr=STDOUT`, `text=True`, `start_new_session=True`; reads lines until `seconds` elapse or the process exits; then `os.killpg(os.getpgid(proc.pid), signal.SIGTERM)`, `proc.wait(timeout=5)`, and on `TimeoutExpired` a `signal.SIGKILL` (Review Focus 4) followed by `proc.wait(timeout=5)`. Return `(not check_log(log), log)`. `main` prints the log's problems (or `macos_check: PASS`) and returns 0/1. `tools/` needs no `__init__.py` — it is already a namespace package, and the tests import it that way.

- [ ] **Step 4: Run the test to verify it passes**

Run: `python3 -m unittest discover -s tests -p 'test_macos_check.py' -v`
Expected: PASS, 7 tests.

- [ ] **Step 5: Run the gate against the live game to verify it fails on the missing window line**

Run: `python3 tools/macos_check.py --seconds 25`
Expected: FAIL naming `missing: windowed, unlocked cursor` (the other three markers appear).

- [ ] **Step 6: Add the live window-state line**

In `gpu/shim/window.cpp`, immediately after `SDL_ShowCursor();`, log the state SDL actually holds:

```cpp
LOG_INFO(Frontend, "Window {}x{} {}: relative mouse {}, grab {}", width_, height_,
         (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) ? "fullscreen" : "windowed",
         SDL_GetWindowRelativeMouseMode(window) ? "on" : "off",
         SDL_GetWindowMouseGrab(window) ? "on" : "off");
```

- [ ] **Step 7: Rebuild and re-run the gate**

Run: `bash build.sh` then `python3 tools/macos_check.py --seconds 25`
Expected: `macos_check: PASS`.

- [ ] **Step 8: Verify the MoltenVK fallback fails loudly, not silently (Review Focus 3)**

Run: `VK_DRIVER_FILES="$PWD/out/MoltenVK_icd.json" python3 tools/macos_check.py --seconds 25`
Expected: the gate exits within the timeout, its output contains no `Traceback`, and if it is not `macos_check: PASS` then the reported problems name the missing marker(s) and the log contains MoltenVK's own driver/extension error — not a hang.

- [ ] **Step 9: Commit**

```bash
git add tools/macos_check.py tests/test_macos_check.py gpu/shim/window.cpp
git commit -m "test(macos): add the live acceptance gate for windowed mode and the unlocked cursor"
```

---

### Task 2: Audio module selection is an explicit, tested outcome

**Files:**
- Modify: `sotc.py:171-179` (the `sys_modules` block in `main()`)
- Test: `tests/test_sotc_modules.py`
- Modify: `README.md` (Portuguese "Os arquivos que você precisa ter", English "Download and use")

**Interfaces:**
- Consumes: nothing.
- Produces: `sotc.select_sys_modules(folder: str | None) -> tuple[list[Path], list[str]]` — the module paths to link and the warnings to print. With `folder` unset it returns `([], ['SOTC_SYS_MODULES not set: no sound (see sotc.py)'])`; with every `SYS_MODULES` name present `(paths, [])`; with any missing `([], ['SOTC_SYS_MODULES: <names> not found in <folder>; no sound'])` naming only the missing files.

- [ ] **Step 1: Write the failing test**

```python
# tests/test_sotc_modules.py
"""sotc.py's sound-module selection: preset, missing file, and unset all have a stated result."""
import sys
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))
import sotc


class SelectSysModules(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.folder = Path(self.tmp.name)

    def test_unset_runs_silently_and_says_so(self):
        modules, warnings = sotc.select_sys_modules(None)
        self.assertEqual(modules, [])
        self.assertEqual(warnings, ['SOTC_SYS_MODULES not set: no sound (see sotc.py)'])

    def test_complete_folder_links_both_modules(self):
        for name in sotc.SYS_MODULES:
            (self.folder / name).write_bytes(b'')
        modules, warnings = sotc.select_sys_modules(str(self.folder))
        self.assertEqual([p.name for p in modules], list(sotc.SYS_MODULES))
        self.assertEqual(warnings, [])

    def test_missing_file_is_named_and_the_game_still_runs(self):  # Review Focus 5
        (self.folder / 'libSceUlt.sprx').write_bytes(b'')
        modules, warnings = sotc.select_sys_modules(str(self.folder))
        self.assertEqual(modules, [])
        self.assertEqual(len(warnings), 1)
        self.assertIn('libSceNgs2.sprx', warnings[0])
        self.assertNotIn('libSceUlt.sprx,', warnings[0] + ',')


if __name__ == '__main__':
    unittest.main()
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `python3 -m unittest discover -s tests -p 'test_sotc_modules.py' -v`
Expected: FAIL with `AttributeError: module 'sotc' has no attribute 'select_sys_modules'`.

- [ ] **Step 3: Implement `select_sys_modules` and use it in `main()`**

Add the function above `main()` in `sotc.py`; replace the inline block at `sotc.py:171-179` with a call that prints each warning to stderr and assigns `modules`.

- [ ] **Step 4: Run the test to verify it passes**

Run: `python3 -m unittest discover -s tests -p 'test_sotc_modules.py' -v`
Expected: PASS, 3 tests.

- [ ] **Step 5: Document the requirement in `README.md`**

In the Portuguese "Os arquivos que você precisa ter" and the English "Download and use" sections, state that the port is silent without the user's own decrypted `libSceUlt.sprx` and `libSceNgs2.sprx` in `SOTC_SYS_MODULES`, and that `python3 tools/macos_check.py` reports the run either way.

- [ ] **Step 6: Commit**

```bash
git add sotc.py tests/test_sotc_modules.py README.md
git commit -m "feat(macos): make sound-module selection a tested outcome and document the firmware requirement"
```

---

### Task 3: Launcher and preparation edge cases from the deferred minors

**Files:**
- Modify: `launcher/sotc_launcher_win.py:286-287` (`game_environment`), `:339-351` (`shadps4_saves`), `:569` (sidebar label)
- Modify: `sotc.py` (`extract_pkg`, and `main()`'s missing-game message)
- Test: `tests/test_launcher_platform.py` (extend), `tests/test_sotc_pkg.py` (new)

**Interfaces:**
- Consumes: `sotc.extract_pkg` (existing).
- Produces:
  - `game_environment(s)` reads `s.get('mouse', False)`.
  - `launcher.shadps4_saves_roots() -> list[Path]` — the roots `shadps4_saves()` walks, including `Path.home() / 'Library' / 'Application Support'` on macOS.
  - `launcher.platform_label() -> str` — the sidebar label, without the word `Windows` on POSIX.
  - `sotc.extract_pkg(pkg: Path, out: Path) -> Path` — the tool is discovered at `out/pkgtool/PkgTool.Core` (or the sibling checkout's copy); when `out/CUSA08809` exists as a real directory lacking `eboot.bin` it is removed with `shutil.rmtree` instead of `unlink()`.

- [ ] **Step 1: Write the failing tests**

```python
# tests/test_launcher_platform.py — add to PosixLauncher
    def test_mouse_defaults_off_when_unset(self):
        env = launcher.game_environment({'game_dir': '/tmp/game'})
        self.assertEqual(env['SOTC_MOUSE'], '0')

    def test_save_roots_include_macos_shadps4(self):
        self.assertIn(Path.home() / 'Library' / 'Application Support',
                      launcher.shadps4_saves_roots())

    def test_sidebar_label_is_not_windows_on_posix(self):
        self.assertNotIn('Windows', launcher.platform_label())
```

```python
# tests/test_sotc_pkg.py
"""extract_pkg must replace a stale real directory, not crash on it, and a missing game must be a
message rather than a traceback."""
import os
import sys
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))
import sotc


class ExtractPkg(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.out = Path(self.tmp.name)
        self.tool = self.out / 'PkgTool.Core'
        self.tool.write_text('#!/bin/sh\n'
                             'mkdir -p "$2/uroot" "$2/uroot/sce_sys"\n'
                             'printf x > "$2/uroot/eboot.bin"\n'
                             'printf x > "$2/uroot/sce_sys/param.sfo"\n')
        self.tool.chmod(0o755)

    def test_replaces_a_stale_real_directory(self):
        stale = self.out / 'CUSA08809'
        stale.mkdir()
        (stale / 'junk').write_text('left over')       # no eboot.bin: a partial extract
        target = sotc.extract_pkg(self.out / 'game.pkg', self.out, tool=self.tool)
        self.assertTrue((target / 'eboot.bin').is_file())
        self.assertFalse((target / 'junk').exists())

    def test_keeps_an_existing_complete_extract(self):
        good = self.out / 'CUSA08809'
        (good / 'sce_sys').mkdir(parents=True)
        (good / 'eboot.bin').write_text('x')
        (good / 'sce_sys' / 'param.sfo').write_text('x')
        self.assertEqual(sotc.extract_pkg(self.out / 'game.pkg', self.out, tool=self.tool), good)


class MissingGame(unittest.TestCase):
    def test_missing_game_is_a_message_not_a_traceback(self):  # Review Focus 2
        env = dict(os.environ, SOTC_GAME_DIR='/nonexistent/sotc-game')
        done = subprocess.run([sys.executable, str(ROOT / 'sotc.py')], cwd=ROOT, env=env,
                              capture_output=True, text=True)
        self.assertEqual(done.returncode, 1)
        self.assertIn('SOTC_GAME_DIR', done.stderr)
        self.assertNotIn('Traceback', done.stderr)


if __name__ == '__main__':
    unittest.main()
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `python3 tests/test_launcher_platform.py` and `python3 -m unittest discover -s tests -p 'test_sotc_pkg.py' -v`
Expected: the three new launcher tests FAIL (`SOTC_MOUSE` is `'1'`; `shadps4_saves_roots` and `platform_label` do not exist), and `ExtractPkg` FAILs (`TypeError: extract_pkg() got an unexpected keyword argument 'tool'`, then `IsADirectoryError` once the parameter exists). `MissingGame.test_missing_game_is_a_message_not_a_traceback` already PASSES — it pins Review Focus 2, which the current message already satisfies.

- [ ] **Step 3: Implement the mouse default (`launcher/sotc_launcher_win.py:286-287`)**

`game_environment`: `s.get('mouse', False)`, matching `APP_DEFAULTS['mouse']`.

- [ ] **Step 4: Implement the save roots (`launcher/sotc_launcher_win.py:339-351`)**

Extract `shadps4_saves_roots() -> list[Path]` returning the current `roots` list plus `Path.home() / 'Library' / 'Application Support'`; `shadps4_saves()` iterates it.

- [ ] **Step 5: Implement the platform label (`launcher/sotc_launcher_win.py:569`)**

Add `platform_label()` returning `'port nativo · Windows'` / `'native port · Windows'` only when `WINDOWS`, and the same text without the ` · Windows` suffix otherwise; `:569` calls it.

- [ ] **Step 6: Implement `extract_pkg(tool=...)` and the stale-directory case (`sotc.py`)**

Add the `tool: Path | None = None` parameter (default keeps the existing search), and where the target is removed, use `shutil.rmtree` when `out/CUSA08809` is a directory and `unlink()` when it is a symlink. Leave the missing-game branch as the clear `SOTC_GAME_DIR` message it already prints.

- [ ] **Step 7: Run the tests to verify they pass**

Run: `python3 tests/test_launcher_platform.py` and `python3 -m unittest discover -s tests -p 'test_sotc_pkg.py' -v`
Expected: PASS (12 and 3 tests).

- [ ] **Step 8: Commit**

```bash
git add launcher/sotc_launcher_win.py sotc.py tests/test_launcher_platform.py tests/test_sotc_pkg.py
git commit -m "fix(macos): launcher and preparation edge cases (mouse default, save roots, stale extract, label)"
```

---

### Task 4: vcpkg readiness by artifact, and the macOS verification docs

**Files:**
- Create: `tools/vcpkg_ready.sh`
- Test: `tests/test_vcpkg_ready.py`
- Modify: `build.sh:148-155` (the `vcpkg_flags` block)
- Modify: `README.md` ("Compilar / Building")

**Interfaces:**
- Consumes: `tools/macos_check.py` from Task 1.
- Produces: `tools/vcpkg_ready.sh <installed-triplet-dir>` — exit 0 only when `<dir>/lib/libSDL3.a` and `<dir>/lib/libZydis.a` both exist, else exit 1 with the missing names on stderr.

- [ ] **Step 1: Write the failing test**

```python
# tests/test_vcpkg_ready.py
"""build.sh must re-run vcpkg after an interrupted install, and skip a complete one."""
import subprocess
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
SCRIPT = ROOT / 'tools' / 'vcpkg_ready.sh'


class VcpkgReady(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.dir = Path(self.tmp.name)

    def ready(self):
        return subprocess.run(['bash', str(SCRIPT), str(self.dir)], capture_output=True, text=True)

    def test_missing_directory_is_not_ready(self):
        self.assertNotEqual(self.ready().returncode, 0)

    def test_partly_installed_is_not_ready(self):
        (self.dir / 'lib').mkdir()
        (self.dir / 'lib' / 'libSDL3.a').write_bytes(b'')
        done = self.ready()
        self.assertNotEqual(done.returncode, 0)
        self.assertIn('libZydis.a', done.stderr)

    def test_complete_install_is_ready(self):
        (self.dir / 'lib').mkdir()
        for name in ('libSDL3.a', 'libZydis.a'):
            (self.dir / 'lib' / name).write_bytes(b'')
        self.assertEqual(self.ready().returncode, 0)


if __name__ == '__main__':
    unittest.main()
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `python3 -m unittest discover -s tests -p 'test_vcpkg_ready.py' -v`
Expected: FAIL — `tools/vcpkg_ready.sh` does not exist (`returncode` 127).

- [ ] **Step 3: Implement `tools/vcpkg_ready.sh`**

The script takes the triplet directory as `$1`, checks `<dir>/lib/libSDL3.a` and `<dir>/lib/libZydis.a`, prints the missing names to stderr, and exits 0 only when both exist. `set -euo pipefail`; there must be exactly one `ok`/missing report, not a per-file early exit.

- [ ] **Step 4: Use it from `build.sh`**

Replace `if [[ -d out/vcpkg_installed/x64-osx ]]` with a check that the directory exists **and** `bash tools/vcpkg_ready.sh out/vcpkg_installed/x64-osx` succeeds. Keep copying from `bloodborne_pc` first, and keep `-DVCPKG_MANIFEST_DIR` (not `-DVCPKG_MANIFEST_INSTALL=OFF`) when the install is not ready.

- [ ] **Step 5: Run the test to verify it passes**

Run: `python3 -m unittest discover -s tests -p 'test_vcpkg_ready.py' -v`
Expected: PASS, 3 tests.

- [ ] **Step 6: Verify `build.sh` still builds and tests**

Run: `bash build.sh --test`
Expected: exit 0, all six test binaries PASS.

- [ ] **Step 7: Document build, run, and verify in `README.md`**

Under "Compilar / Building", add a macOS subsection: `bash build.sh --test`, `python3 sotc.py`, `tools/build_kosmickrisp.sh`, and `python3 tools/macos_check.py` (what it asserts, and that it is the acceptance gate).

- [ ] **Step 8: Run the whole suite and the gate**

Run: `bash build.sh --test && python3 -m unittest discover -s tests && python3 tools/macos_check.py --seconds 25`
Expected: build exit 0 with six PASSes; the Python suite `OK`; `macos_check: PASS`.

- [ ] **Step 9: Commit**

```bash
git add tools/vcpkg_ready.sh tests/test_vcpkg_ready.py build.sh README.md
git commit -m "fix(macos): decide vcpkg readiness by artifact and document the macOS build, run and verify path"
```

---

## Non-goals (still deferred, with the reason)

- **Native arm64 build.** The spec fixes x86-64 under Rosetta 2; a native port is a separate project (guest x86-64 code must still run).
- **Synthesising audio without the user's firmware.** `libSceNgs2`/`libSceUlt` are Sony firmware; the port's silent HLE fallback is the intended behaviour and Task 2 makes it explicit.
- **`runtime_memory.c` `pool()` reset on failure, and the unused `<cpuid.h>`.** Both are real but there is no failure-injection seam that does not add test-only code to production to prove them; leaving them deferred keeps every task in this plan verified by a test that failed first.
- **Gameplay-content correctness** (rendering, collision, save contents). Not automatable; the gate proves the process runs and presents a frame, not that the game is fun.
