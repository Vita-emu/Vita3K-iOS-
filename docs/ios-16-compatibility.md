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
  now gets **12 MiB** of JIT code-cache capacity instead of 16 MiB. Unknown RAM
  and larger devices keep 16 MiB. The setting is fixed for the process and is
  shared with pool prewarming so pool allocations always match thread caches.
  At 24 active caches this reduces configured capacity from 384 to 288 MiB;
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
