# CPU and GPU workloads on iOS

Tsubomi is a PS Vita emulator built on Vita3K. The active game target is
`ios-upstream-core`; the older iOS bootstrap target is a diagnostic application.

## Existing execution path

| Work | Host execution |
| --- | --- |
| Guest ARM instructions, game logic and guest threads | CPU, translated by Dynarmic JIT |
| Kernel/module HLE, input, audio and graphics command preparation | CPU |
| Vita shader translation and Vulkan pipeline compilation | CPU and graphics driver |
| Vertex/fragment shader execution, rasterization, depth and blending | GPU via Vulkan → MoltenVK → Metal |

The renderer already runs separately from the guest execution threads. The CPU
must translate Vita graphics commands and prepare GPU submissions; enabling GPU
rendering does not remove this work. JIT must be available when the Dynarmic JIT backend is selected. An
experimental scalar IR Interpreter is also available; see
[runtime settings](runtime-settings.md) for its instruction limitations. iOS controls physical CPU/GPU scheduling and clocks.

## Shader worker optimization

When multiple pipelines use the same shader, one worker loads or compiles its
module. Other workers now sleep on a condition variable until that module is
published under the cache mutex. Previously they repeatedly yielded the CPU and
read a module handle concurrently with an unsynchronized write.

This change removes that polling and synchronizes module publication. Runtime
compilation of different shaders remains concurrent. Both a disk-cache hit and
a newly generated shader wake the waiters. A failed compilation clears its
claim, wakes the waiters and propagates the exception. Startup precompilation
uses the same protected cache and never returns an in-progress handle.

This reduces avoidable CPU work during shader contention. It does not move
shader compilation to the GPU, change graphics accuracy, or establish an FPS
improvement. The benefit depends on the game's shaders and compilation workload.

## Verification

Run the portable regression suite:

```sh
python3 -m unittest discover -s .ci/tests -v
```

The shader worker fixture runs the production acquisition functions with a
synthetic device. It checks simultaneous requests, independent shaders, disk
hits/misses, spurious wakeups, failure/retry and startup/runtime cache reuse.
It does not exercise a Vulkan driver or compile the full iOS application.

Build the upstream-core IPA on macOS using the instructions in
[iOS compatibility](ios-16-compatibility.md). On an iPhone, compare the same
build configuration, game/save, camera, resolution, shader compiler worker
count and thermal conditions before and after the change. Measure both first
encounter and warm-cache scenes; record FPS, frame time, CPU time and memory.
Include a scene with several pipelines sharing shaders and check for missing
geometry, hangs, relaunch problems and background/foreground regressions.
No device performance measurements are available from the Linux sandbox.
