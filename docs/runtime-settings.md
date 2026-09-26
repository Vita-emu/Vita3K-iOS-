# Runtime settings and the 768 MiB guest budget

## Scope

The default **768 MiB guest allocation limit** limits live guest pages, including
page-rounded allocations. It does not reserve/touch 768 MiB of physical RAM at
startup and it is **not a total-process RAM cap**. JIT regions, GPU resources,
shader compiler memory, driver allocations and UI images are outside this limit.
The emulator still needs its 4 GiB virtual address range for Vita addresses.
The allocator commits memory on demand, rejects requests exceeding the budget,
and returns budget when allocations are freed. A host commit failure rolls back
the allocation. Existing iOS page discard and safe GPU buffer trimming remain.
Games may fail if their allocations exceed the selected limit.

Settings → Memory offers 512/768/1024 MiB. The default and memory preset use
768 MiB. Values are loaded before emulator memory initialization and remain fixed
for the process. Close and reopen the app after changing them. No per-game
backend or guest-memory override is exposed.

## CPU backends

Settings → CPU & Execution selects the backend for the next process:

- **Dynarmic JIT** remains the default for games. CPU optimization and per-thread
  executable cache settings apply to this backend.
- **IR Interpreter (experimental)** uses Dynarmic's real ARM/Thumb translator
  and executes supported scalar IR in C++. It never constructs a JIT or prewarms
  executable regions, and does not require the JIT launch gate. It translates one
  guest instruction per step, retains no translated-block cache, and limits
  execution to 256 IR operations per instruction. Current instruction scratch
  storage is bounded; this does not bound the rest of the emulator.

The interpreter supports basic ARM/Thumb/Thumb-2 integer operations, condition
flags, branches, loads/stores, SVC and the emulator's WFI return sentinel.
It **does not implement the full ARMv7 instruction set**: floating point,
NEON, coprocessor and exclusive operations remain unsupported. It is a usable
experimental execution core, not a general replacement for JIT in commercial
games. Each instruction's IR and terminal are checked before executing it;
unsupported IR reports a CPU error without silently falling back to JIT or
skipping the instruction. The iOS frontend stops the session and presents the
first backend error. A memory fault can occur after earlier operations in a
multi-access instruction, so fault rollback is not promised.

The active backend is shown separately from the saved selection. Switching is
not performed on running guest threads. JIT-only settings are disabled/hidden
when the interpreter is selected. No GPU rendering path was replaced.

## Settings audit

The root screen links to categories rather than placing every control in one
long form. Existing global/per-game settings still pass through
`SettingsModel.save()` and the Objective-C bridge. Autosave, Done, disappearance
and backgrounding retain their existing persistence paths.

| Category / controls | Consumer and application time |
| --- | --- |
| CPU backend | UserDefaults → runtime tuning → `init_cpu`; app restart |
| JIT cache | Runtime tuning → `ios_jit_code_cache_size` and pool prewarm; restart |
| CPU optimizations | Config → Dynarmic configuration; next game launch |
| Guest RAM budget | Runtime tuning → `mem::init` and guest allocator; restart |
| Texture entries / upload trimming | Runtime tuning → texture/staging caches; restart |
| Resolution, V-Sync, accuracy, surface sync, memory mapping, anisotropy | Native settings → current config → renderer initialization/runtime settings; next launch is the safe application boundary |
| Shader disk cache / async compilation | Native settings → renderer atomics / pipeline cache; next launch |
| Shader workers / precompilation | Runtime tuning → pipeline worker policy / launch preparation; restart. Workers require async compilation; precompilation requires disk caching |
| NGS audio | Native settings → NGS configuration; next launch. SDL backend is informational |
| Interface sounds | DefaultsKey → `HomeSoundEffects`; immediate |
| Liquid Glass | DefaultsKey → in-game overlay; displayed only on iOS 26+ |
| Orientation | UserDefaults + bridge → native orientation lock; immediate |
| Library labels, artwork, compact view, sorting | DefaultsKey / AppStorage → cells, carousel and library state; immediate |
| Face buttons / virtual controls | Native controller bindings / existing controller options host |
| FPS, frame time, graph, RAM, battery, log, compact HUD | Defaults → native metrics and Swift overlay; in-game |
| Firmware/About | Read-only status, log sharing, bug-report and project links |
| Library/save import/export | Existing bridge actions; closes Settings before showing progress/pickers |
| Per-game reset | Removes the override and applies global values next launch |

Disk shader loading now checks the cache toggle and uses the same feature-specific
filename as shader generation. It cannot load an old accuracy variant by the
legacy filename. Turning cache reuse off still permits regenerated shaders to
be written for a later session.

## Verification

Portable policy/concurrency/allocator fixtures:

```sh
python3 -m unittest discover -s .ci/tests -v
```

Real Dynarmic frontend, interpreter, CPU adapter and IR/JIT comparison tests
(require pinned `external/dynarmic`, host Boost headers, CMake 3.22+, C++20):

```sh
cmake -S .ci/tests/ir_interpreter -B /tmp/tsubomi-ir-build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/tsubomi-ir-build -j 2
ctest --test-dir /tmp/tsubomi-ir-build --output-on-failure
```

The adapter fixture replaces only its logger and guest allocation setup; the
production CPU interface, state, memory pointer resolution and adapter compile
and execute. The factory is compiled with iOS selection enabled. Differential
tests compare 1,344 arithmetic/flag/shift cases against the real host JIT. This
is targeted coverage of the implemented subset, not ARM conformance certification.

**Still required on macOS/iPhone:** compile the full IPA; exercise every category
on iOS 16.7 and iOS 26; verify saved and active backend after restart; confirm JIT
launches still require permission and the interpreter does not; check per-game
save/reset and sheet dismissal; compare memory and FPS on actual titles. The
Linux sandbox cannot establish that every UI control works on an iPhone, that
retail games run on the experimental interpreter, or that total app RAM stays
below 768 MiB.
