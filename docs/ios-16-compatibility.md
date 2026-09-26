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

## iPhone 8 Plus validation still required

1. Sign/install on iOS 16.7.16 and open the app without JIT. Onboarding, settings,
   and the library must open; a game launch must report missing JIT.
2. Import your own firmware packages. Verify progress updates and completion
   gates without reopening the app. Import your own game and license as needed.
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
