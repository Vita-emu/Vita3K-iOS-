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

## Bounded memory on iOS

- The iOS renderer applies backpressure at eight pending guest command lists
  (desktop remains at 30). A full queue blocks its producer until the renderer
  consumes a list; commands are not dropped. This bounds the list count, not the
  bytes inside a scene. Queue shutdown now changes the abort predicate under the
  same mutex used by waiting threads, preventing a lost shutdown wakeup.
- With **Memory → Reclaim unused GPU buffer memory** enabled, each recycled
  Vulkan frame slot requests `eReleaseResources` once every 120 slot cycles,
  after its GPU fences complete. Ordinary resets reuse driver allocations.
  This supplements the existing upload-buffer trimming. The pinned
  [MoltenVK 1.4.2 implementation](https://github.com/KhronosGroup/MoltenVK/blob/v1.4.2/MoltenVK/MoltenVK/Commands/MVKCommandPool.mm)
  releases cached command objects on that flag. It can reduce memory retained
  after a large scene, with a possible allocation cost on subsequent frames.
  RenderPass barriers, attachment operations and submission order are preserved.
- Automatic shader compilation uses the existing CPU-core policy capped at two
  workers on devices reporting at most 3 GiB and four otherwise. It no longer
  forces at least four workers on larger/unknown-memory devices. The explicit
  Settings worker count still takes precedence. Guest/audio/render threads keep
  their synchronization responsibilities.
- PKG and VPK payloads already stream from disk. PUP firmware segment decryption
  now also streams AES-CTR and miniz inflation through two 64 KiB heap buffers,
  plus fixed cipher/inflater state. It no longer retains whole encrypted and
  expanded segments together. Header parsing remains bounded at 16 MiB.
  Truncated input, invalid compression/checksums and output write failures fail
  installation. This optimizes installation; it is not an in-game asset cache.

The default 768 MiB budget covers guest allocations. A reserved 4 GiB guest
address range is not 4 GiB of resident RAM. Guest pages are committed as needed
and freed pages are discarded without changing guest addresses. Moving live
allocations for compaction would invalidate guest pointers and GPU mappings.
The process also needs memory for Metal, JIT, shaders, audio and the UI; the guest
budget alone cannot guarantee that iOS will not terminate it for memory pressure.

JIT already defaults to 8 MiB per initialized guest CPU on devices with at most
3 GiB, or 16 MiB otherwise; it is not a single process-wide 8 MiB allocation.
Caches are created lazily and released at safe dormant-thread lifecycle points.
There is no game-independent scene-change event that safely permits discarding
executing JIT code, so scene-change eviction is not implemented. The experimental
IR backend is selectable but still lacks instructions required by general games.

### Focused host checks

```sh
PYTHONPATH=.ci/tests python3 -m unittest test_ios_install_and_cache test_render_memory -v
```

These execute production streaming code with real OpenSSL/miniz, production
frame recycling with a recording Vulkan substitute, and real queue threads.
They cover chunk boundaries, high expansion, corrupt/truncated input, failed
writes, worker selection, completion-before-reset, all frame slots, the Settings
opt-out, desktop behavior and producer/consumer shutdown. Actual Metal memory
savings, frame-time costs and gameplay compatibility require an iPhone run.
