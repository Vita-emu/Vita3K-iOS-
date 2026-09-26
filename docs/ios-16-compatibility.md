# iOS 16.7 compatibility (iPhone 8 Plus)

## Project and scope

Tsubomi is a native iOS frontend for the Vita3K PlayStation Vita emulator.
The active `ios-upstream-core` configuration links the actual emulator,
SDL3, Dynarmic for CPU execution, and Vulkan through MoltenVK for graphics.
The older `VITA3K_BUILD_IOS` target and milestone documents describe a separate
bootstrap/diagnostic application; use **Build upstream-core iOS IPA** for games.

The deployment baseline is **iOS 16.7**, which permits iOS 16.7.16. Device
builds use `arm64`, not `arm64e`, and require Metal. This includes the intended
iPhone 8 Plus configuration. These source changes are not a claim that a
physical iPhone 8 Plus or any particular game has passed testing.

## Compatibility changes

- The root CMake configuration sets deployment before compiler/dependency
  detection. The application plist, device/simulator generators, CI, and vcpkg
  overlay triplets target the same baseline.
- SwiftUI state uses `ObservableObject`, `@Published`, and observed bindings
  available on iOS 16, including firmware progress, artwork refresh, settings,
  trophies, touch controls, and the performance overlay.
- iOS 26 retains Liquid Glass. Earlier releases use system materials and
  bordered buttons. Animation/symbol APIs introduced in iOS 17 are guarded.
- On iOS 16, landscape browsing uses the adaptive grid or list, with controller
  focus and pull-to-refresh. The snapping carousel remains available on iOS 17+.
- Before iOS 26, JIT checks the process capability and uses Oaknut's ordinary
  RWX code allocation. It does not prewarm the iOS 26 universal-JIT region pool
  or send that pool's debugger breakpoint. iOS 26 retains its debugger/pool
  requirements. No JIT permission is granted by lowering deployment.
- Packaging checks `MinimumOSVersion` and the arm64 Mach-O deployment commands
  of the executable and embedded libraries. Building with an iOS 26 SDK is
  compatible with an iOS 16.7 deployment target; these are different settings.

## Build

Use macOS with **Xcode 26**, CMake 3.22+, and the dependencies/patches pinned in
`.github/workflows/ios-upstream.yml`. Xcode 26 is still needed to compile the
availability-guarded Liquid Glass code. Linux cannot build the IPA.

For CI, select **Build upstream-core iOS IPA**, run it on the branch containing
these changes, and download `Vita3K-upstream-core-iOS-<commit>-unsigned`.
Extract the artifact ZIP to obtain `Vita3K-upstream-core-iOS-unsigned.ipa`.
The IPA is unsigned and needs your normal signing/sideloading process.

For a local Mac after preparing dependencies exactly as the workflow specifies:

```sh
bash gen-ios-upstream.sh
cmake --build build-ios-upstream --config Release --target Vita3KiOS
bash .ci/package-ios.sh --build-dir build-ios-upstream --configuration Release \
  --output artifacts/Vita3K-upstream-core-iOS-unsigned.ipa
```

Use a fresh build directory after changing deployment or vcpkg triplets. CI
includes triplet contents in its dependency cache key. The packaging check caps
the minimum at 16.7 so accidentally building only for a newer OS fails CI.

## Firmware setup and TrollStore Lite

Onboarding now requires **PSVUPDAT.PUP (main firmware)** followed by the
**PSP2UPDAT.PUP font package**. Readiness comes from the installed `vs0` and
`sa0` content. A separate pre-install package (`pd0`) is optional, matching the
desktop firmware check. Existing installed content is reused when reopening
onboarding; you do not need to delete the app or install the main PUP again.
The final button and game import/launch still require both main firmware and
fonts. Import results are also visible on the firmware page.

On iOS 16, tap **Enable JIT** in the library, then **TrollStore Lite / TrollStore**.
This opens the official `apple-magnifier://enable-jit?bundle-id=...` handler
using the installed app's actual bundle ID. Return to Tsubomi after enabling
JIT; its periodic capability check clears the banner. Opening the helper alone
does not mark JIT as active. If Apple's Magnifier opens instead, check that
TrollStore's URL Scheme setting is enabled. The helper must support your
jailbreak/iOS setup and have installed this app. No signing entitlement or
jailbreak is installed by this button.

The older-iOS probe also accepts a successful anonymous RWX mapping, matching
the emulator's code allocator, so an existing jailbreak capability does not
require a debugger flag. A rejected mapping leaves games blocked. iOS 26 keeps
its existing StikDebug handshake and does not use this mapping fallback.

Protocol references: [TrollStore URL scheme](https://github.com/opa334/TrollStore#url-scheme),
[Lite URL registration](https://github.com/opa334/TrollStore/blob/main/TrollStoreLite/Resources/Info.plist),
and [shared JIT handler](https://github.com/opa334/TrollStore/blob/main/TrollStore/TSSceneDelegate.m).

## Memory and performance tuning

- On devices reporting at most 3 GiB of physical RAM, each active guest thread
  now gets **8 MiB** of JIT code-cache capacity instead of 16 MiB. Unknown RAM
  and larger devices keep 16 MiB. The setting is fixed for the process and is
  shared with pool prewarming so pool allocations always match thread caches.
  At 24 active caches this reduces configured capacity from 384 to 192 MiB;
  it is not a measured 96 MiB reduction in physical footprint. The iOS 26
  writable alias shares backing pages with executable memory.
- Lazy JIT creation and cache release for one-shot dormant threads remain in
  place. Cyclic threads retain translated code across restarts. Smaller caches
  can require more recompilation in code-heavy games; actual FPS and memory
  must be compared on the same game scene. Safe CPU optimizations stay enabled
  according to the user's existing CPU setting.
- Artwork is downsampled during background decoding to at most **1024 pixels**
  on the longest side. Queued requests reuse an already decoded cover. This
  avoids repeatedly decoding large imported covers at their source resolution.
- The artwork cache tracks decoded byte cost and requests a **32 MiB** budget
  on devices with at most 3 GiB RAM, or 64 MiB otherwise. This is an advisory
  NSCache limit: currently displayed images can be retained separately by views.
  Existing memory-warning eviction remains in place.

To compare on iPhone 8 Plus, keep the same game, resolution, CPU settings and
JIT method. Record FPS/frametime and RAM after warm-up in the same scene, after
10 minutes, and after quitting/relaunching twice. Also scroll a library with
large covers. Check `tsubomi.log` for the selected per-thread JIT budget. No
on-device performance result is available from the Linux development sandbox.

## iPhone 8 Plus validation still required

1. Sign/install on iOS 16.7.16 and open the app without JIT. Onboarding, settings,
   and the library must open; a game launch must report missing JIT.
2. Import main firmware alone: Next must enable and lead to the font page.
   Import fonts: Get Started must become available even without `pd0`. Test
   fonts-first, a rejected PUP, and restart with existing installed content.
   Import your own game and license as needed.
3. Enable JIT using a debugger/enabler that supports your exact iOS version and
   signing setup. Verify the banner clears. On iOS 16, capability may persist
   after debugger detachment; the iOS 26 pool handshake must never run.
4. Start at 1× resolution. Try one title, then quit and launch it again. Check
   graphics, audio, saves, and whether memory pressure terminates the app.
5. Test portrait/landscape, list/grid, controller navigation, settings persistence,
   firmware/artwork refresh, trophies, and simultaneous touch buttons/sticks.
6. Test background/foreground transitions and launch after a full app restart
   (JIT may need enabling again). Also smoke-test iOS 26 to check the glass and
   universal-JIT paths.

The emulator reserves a 4 GiB *virtual* guest address space; that is not an
immediate 4 GiB physical allocation. Actual game memory, JIT caches, textures,
and graphics-driver support still limit older devices. Installation support
cannot guarantee full-speed emulation or that every Vita game boots. Retrieve
`Documents/Tsubomi/tsubomi.log` after a failed test and record the game ID,
settings, iOS version, and JIT method. Do not share game or firmware files.

## Checks available without Xcode

```sh
python3 -m unittest discover -s .ci/tests -v
```

These tests cover packaging policy using synthetic vtool output and mocked
Apple tools. They do not compile Swift/Objective-C++, validate a real IPA,
or establish device compatibility. Xcode build and physical-device checks
remain required before publishing a release as tested on iPhone 8 Plus.

## Jetsam, installs, text entry and interrupted caches

The supplied iPhone 8 Plus Jetsam report identifies `per-process-limit` at
about 2098 MiB resident (134271 pages of 16 KiB). This identifies a memory kill,
not which allocator caused it. This build reduces iOS retained texture slots
from 1024 to 512; GPU replacements continue to use the existing frame-delayed
destruction queue. This is an entry bound, not a byte cap or a bypass of iOS's
process limit. Some games may still exceed the available memory.

PKG/ZIP imports run at utility QoS. After installation the same worker forces
an app scan and builds the library snapshot, including recursive size counts
and trophies, before notifying the UIKit thread. PKG stages now show progress;
ZIP decompression remains streamed. The library should update without an app
switch, but this requires a device test with the affected package.

The native text editor supports both Vita IME dialogs and callback-based IME,
including Done, optional Cancel, multiline text and UTF-16 length limits.
Results are checked against the current IME generation; stale sheets cannot
write into a later request. Callback-based IME delivers text before Enter and
releases its lock before invoking guest callbacks (which may close the IME).
The guest UTF-16 work buffer is now allocated in bytes for 16-bit characters.

Cached SPIR-V is read with size/alignment/instruction-length checks; invalid
files regenerate instead of being passed to the GPU. Pipeline cache reads
check hash-count bounds and complete reads. On iOS, files over 64 MiB are
ignored to avoid an unbounded startup allocation. Pipeline cache saves write
a temporary file then rename it, preserving the previous file on write failure
or forced termination during the write. These changes address cache integrity;
they do not establish the cause of any particular game's graphical glitches.

Device checks still required: install a large PKG and ZIP without backgrounding,
confirm the library updates, type/save a name (including Thai or emoji), cancel
and reopen a dialog, quit while the keyboard is open, and compare memory and
textures in the affected game across a full app restart. Record title IDs,
settings, `tsubomi.log`, and screenshots for any remaining graphics problem.


## Settings, installation and lower-memory rendering

- Graphics/CPU edits save after a short debounce, on leaving Settings, and
  when the scene becomes inactive. Opening a per-game page without editing
  does not create an override; Reset continues to remove the override.
- Native settings preserve current fields not exposed in the UI. Configuration
  writes use a temporary file and rename so an interrupted write does not
  truncate the previous settings file.
- Shader cache now reaches the per-title runtime and Vulkan disk reader. Turning
  it off regenerates shaders on demand; regenerated shaders can still be saved
  for later use. Vulkan SPIR-V filenames include the GPU feature mask, and the
  cache index rejects truncated counts and is published through a temporary file.
- On devices reporting at most 3 GiB RAM, asynchronous pipeline compilation uses
  at most two workers (one on fewer than six logical cores), instead of forcing
  four. This bounds simultaneous compiler work; cold shader compilation may take
  longer. The low-memory JIT cache policy and cyclic-thread reuse are retained.
- **Lower memory preset (0.5×)** enables CPU optimizations and shader caching,
  selects 1× anisotropic filtering, and disables double-buffer mapping. It leaves
  accuracy, surface sync and audio choices intact. At a native 960×544 render
  size, 0.5× means 480×272 and one quarter as many pixels; total process RAM does
  not fall by the same ratio.
- PUP extraction and image joining copy 64 KiB at a time. SCE decryption reads
  metadata instead of retaining the complete input package and decrypts segments
  in place. Compressed segments still need decompression storage. ZIP imports
  report extraction progress; PUP imports report installation stages. Selecting
  firmware now names **PSVUPDAT.PUP** and **PSP2UPDAT.PUP** explicitly and reports
  which package is still missing. Download cloud files first and keep the app open.

30 FPS is a 33.33 ms frame budget, not a guaranteed result or a change to guest
VBlank timing. Compare the same title, save, camera and thermal state before/after
using the FPS/frametime/RAM overlay. Device tests must also cover swipe-dismissed
settings, relaunch, Reset per-game settings, both real PUP packages, PKG/ZIP
installation, and a second game launch after changing High accuracy. Host tests
exercise copy/decryption, cache-index bounds and settings propagation with
synthetic adapters; they do not replace an Xcode build or iPhone measurements.


## Runtime memory controls and large imports

`Settings → JIT & Memory` saves device-wide options in UserDefaults. Close and
reopen the app after changing them. They are loaded before JIT prewarming and
renderer creation; existing executable regions never change size mid-session.
No rebuild is needed to change these options after installing this version.

- **JIT cache per guest thread:** Automatic, 4, 8, 12, 16, 24 or 32 MiB. Automatic
  chooses 8 MiB on known devices with at most 3 GiB RAM, otherwise 16 MiB.
  This is translated-code capacity, not a RAM quota for the whole guest thread.
  Smaller caches may recompile more often. Guest thread/core counts are not
  overridden because game scheduling depends on them.
- **Shader compiler threads:** Automatic or 1–4, capped to detected host cores.
  This controls the Vulkan asynchronous compilation workers, not guest CPU
  threads. With async compilation disabled it does not create background workers.
- **Texture cache:** Automatic or 128/256/512 entries. Automatic uses 128 on
  known <=3 GiB devices, 512 otherwise. Entries differ in byte size, so this is
  not a total-RAM limit. Fewer entries can increase texture uploads/stutter.
- **Trim oversized upload buffers:** enabled by default. Once the existing GPU
  fence checks allow reuse, a buffer larger than 4 MiB can shrink if demand is
  at most one quarter of its capacity and 120 frames have passed since resizing.
  A 1 MiB floor on shrinking and the interval reduce allocation churn.
- **iPhone 8 Plus preset:** 8 MiB JIT caches, one shader compiler, 128 textures,
  buffer trimming, 0.5× resolution, CPU optimizations and shader caching enabled,
  anisotropic filtering off, double buffering off. Accuracy and surface-sync
  choices are retained. It is a starting point, not a measured FPS guarantee.

Surface-cache eviction now retires the old readback/blit allocations and format
conversion context as well as its image views. A FIFO barrier lets pending CPU
readbacks finish before the slot is reused; GPU allocations still retire through
the frame destruction queue. RGB24 readback reserves the four-component GPU row
before packing it back to guest memory. Upload fence-age checks avoid unsigned
underflow in the first frames of a game.

The long-lived SDL library/game loops now bound Objective-C temporary objects
with an autorelease pool per iteration. The nested UIKit run loop explicitly
flushes pending layer transactions, including import completion and firmware
readiness changes. These paths still require device verification for the reported
background/foreground workaround.

PKG progress at 80% marks the start of PFS decryption, which exposes no byte
progress. The UI now names that stage instead of displaying a frozen percentage;
it shows library indexing separately and only announces success after completion.
Large games can spend minutes in that stage. Firmware setup shows independent
system/font readiness, allows Back, and wraps the busy message. A version number
alone does not unlock Next; the required partition must contain installed files.

For a reproducible report, enable the FPS/RAM overlay, note the build commit,
record the same gameplay scene at 1/5/10 minutes, then export `tsubomi.log`.
While the overlay is visible it records a footprint/FPS sample every 30 seconds.
Report PKG versus ZIP and the last install-stage message. The compact HUD option
reduces readout size; its position remains editable in Virtual controls.
Local host fixtures validate policies, resource retirement and installer/cache
code. They do not measure Metal memory, Jetsam, SwiftUI updates, real PUP/PKG
installation, or Attack on Titan 2 FPS on an iPhone.

### Freed guest memory on iOS

Freed guest RAM now uses Darwin `MADV_FREE` instead of `MADV_DONTNEED`.
Darwin's DONTNEED deactivates pages without discarding their dirty contents;
FREE tells the kernel those contents are disposable. Only entirely free host
pages are discarded: a 16 KiB host page containing a live 4 KiB guest allocation
is retained, and reused guest allocations are still zero-initialized.
This allows reclamation after the game frees data; it cannot discard live game,
JIT or GPU data, and does not impose a total-process memory cap.

Automatic texture capacity and the iPhone 8 Plus preset now use 128 entries on
known devices with at most 3 GiB RAM. Existing explicit 256/512 selections stay
in effect; choose the preset again and restart Tsubomi to use the smaller cache.
Texture uploads may increase when revisiting a scene. No on-device reduction in
MB or sustained FPS has been measured for these changes yet.

### Small JIT caches and shader startup memory

The ARM64 device backend now exposes 4 and 8 MiB per guest thread; Automatic on
known <=3 GiB devices and the iPhone 8 Plus preset use 8 MiB. The x64 simulator
keeps at least 12 MiB for explicit small-cache selections. These are executable
code capacities, not limits on all RAM used by a thread. Thread counts remain
controlled by the guest game; shader compiler workers are a separate setting.
CPU optimizations remain enabled by the memory preset. iOS controls CPU/GPU
clock rates and scheduling; there is no switch that guarantees 100% utilization.

The pinned ARM64 Dynarmic backend flushes a code cache with less than 1 MiB left.
Its A32 prelude now checks that reserve. The portable regression emits the real
prelude with Oaknut at 4/8/12 MiB, but does not execute the generated instructions.
4 MiB may cause more recompilation and stutter than 8 MiB. Cache flushes now also
clear A32/A64 guest-address range metadata via virtual dispatch; previously the
range index survived every code-cache reset and could accumulate old entries.
This patch is included in the existing iOS dependency patch, with no workflow edit.

**Precompile cached shaders at launch** is off by default on iOS. It is saved
with the other JIT & Memory preferences and applied after restarting Tsubomi.
Disk shader cache remains enabled by the memory preset; shaders are compiled
when needed instead of precompiling all cached pairs at startup. Turn this on
if startup precompilation is preferable to possible first-use shader stutter.
The setting reduces eager compilation, not a game's eventual live working set.

Neither a 4 MiB cache nor these cleanup changes guarantees staying below the
OS memory limit. Device gameplay, JIT permissions and sustained RAM/FPS still
need verification on iPhone; no measured MB savings are claimed here.

## CPU backend and guest allocation budget

Settings now groups CPU, memory, graphics/display, audio, interface, library,
controls, overlay and system options into separate pages. The default guest
allocation budget is 768 MiB; this excludes JIT/GPU/UI and is not a process
RAM cap. An experimental scalar IR Interpreter can be selected for the next
process alongside the default Dynarmic JIT. Unsupported instructions stop
with an explicit error; general game support still requires JIT. See
[the runtime settings guide](runtime-settings.md) for exact scope, application
timing, supported instructions and outstanding device checks.
