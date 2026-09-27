# iOS settings and graphics audit

## Comparison baseline

Reviewed against [Vita3K 095371f8](https://github.com/Vita3K/Vita3K/tree/095371f83d7234eb6faed40e6310845bb82c7d52) and the pinned [MoltenVK v1.4.2](https://github.com/KhronosGroup/MoltenVK/releases/tag/v1.4.2). The iOS workflow already uses that release; no driver upgrade or guessed driver options were applied. MoltenVK translates the existing Vulkan renderer to Metal.

Upstream's high-accuracy path uses shader interlock when available and disables the texture-viewport shortcut. It is a compatibility tradeoff, not a guarantee that every title renders correctly. The iOS port retains explicit staging/readback and avoids memory-protection traps while attached to a JIT debugger. These platform differences still require device testing.

The [MoltenVK configuration documentation](https://github.com/KhronosGroup/MoltenVK/blob/v1.4.2/Docs/MoltenVK_Configuration_Parameters.md) describes queue submission behavior; changing submission mode is not a general FPS fix. The [Vulkan synchronization examples](https://github.com/KhronosGroup/Vulkan-Docs/wiki/Synchronization-Examples) describe the transfer dependencies used here.

## Settings traced to consumers

Core settings flow through SettingsModel, TsubomiBridge, native dictionaries, and UpstreamMain's global/per-game apply functions. Global settings persist through app::commit_settings; per-game dictionaries are applied before renderer/runtime initialization. A title override takes precedence over global values. The iOS Settings screen saves core settings through the library action loop, before launching the next game. Saves retain FIFO order relative to Launch; consecutive saves coalesce. Saves during an import are deferred until the worker finishes.

| Setting | Consumer / effect | Apply time |
| --- | --- | --- |
| CPU backend | ios_runtime::uses_jit selects Dynarmic or experimental IR interpreter | App restart |
| CPU / JIT execution threads | FIFO admission gate around Dynarmic Run/Step; explicit limits enable bounded instruction slices | App restart |
| JIT cache | Dynarmic per-thread cache budget | App restart |
| JIT CPU optimizations | current_config.cpu_opt, CPU initialization | Next game launch |
| Guest RAM | MemState guest_bytes_limit; allocations can fail at the budget | App restart |
| Texture entries | TextureCache::init, entry count rather than byte limit | App restart |
| Reclaim GPU buffers | VKTextureCache staging shrink policy, after safe frame reuse | App restart |
| Shader threads | PipelineCache worker policy, capped to available logical CPUs | App restart |
| Precompile shaders | app::request_shader_precompile, requires shader disk cache | App restart, next launch |
| Shader disk cache | Renderer use_disk_shader_cache and disk loader | Next game launch (core supports runtime changes) |
| Async compilation | PipelineCache::set_async_compilation; pending pipelines can omit draws | Next game launch (core supports runtime changes) |
| Resolution | Renderer res_multiplier and render-target sizes | Restart game/app |
| High accuracy | VKState::late_init, interlock / texture viewport policy | Restart game/app |
| Surface sync | Renderer disable_surface_sync; GPU readback for CPU consumers | Next game launch (core supports runtime changes) |
| Double-buffered guest memory | memory_mapping = double-buffer vs disabled | Restart game/app |
| V-Sync | Atomic vsync state, swapchain present-mode selection | Next game launch; core rebuilds swapchain when changed |
| Anisotropic filtering | TextureCache sampler configuration | Next game launch; subsequent binds in the core |
| NGS audio | current_config.ngs_enable | Next game launch |

The iPhone 8 Plus preset was removed. Reset memory settings now resets only guest RAM, texture entries and GPU buffer reclamation. CPU and shader preferences are retained.

## Changes and verification limits

- Casted surface cache keys now distinguish host format (including gamma) and channel mapping. A previous view must not be reused for a differently interpreted texture.
- Explicit dependencies cover render/shader/transfer writes before surface copies, clear-before-copy writes, and the two transfers in typeless conversions.
- Sampler cache keys include anisotropic filtering, so toggling it cannot keep returning samplers configured with the old value.
- Presentation falls back to guaranteed FIFO when other modes are unavailable. Immediate is selected only when advertised.
- The guest frame counter is atomic; periodic metric reset uses exchange to avoid losing concurrent submissions. The overlay remains an honest one-second guest-submission average, not a claimed measurement of GPU execution time.

Host fixtures exercise settings propagation, cache identity, transfer ordering and present-mode selection with recording backends. They do not render commercial games or emulate Metal. SwiftUI appearance, full iOS compilation, graphics correctness and 40-to-60 FPS improvement remain device/CI checks. Lower resolution can reduce GPU work, but cannot remove CPU, shader-compilation, bandwidth or thermal limits. The guest memory budget does not cap process RSS.

For a remaining black screen or rainbow surface, capture the title ID, app commit, relevant graphics settings, screenshot and tsubomi.log. Compare the same scene with only one setting changed. Keep async compilation off while determining whether missing geometry is a shader-compilation artifact.

## CPU / JIT concurrency and GPU allocation

`CPU / JIT execution threads` is a concurrency ceiling, not physical core affinity.
Automatic (0) preserves the existing OS scheduler and does not enable instruction
counting or acquire a scheduler mutex. Explicit values 1–8 are clamped to the host
logical CPU count. Each admitted Dynarmic run gets a 10,000-instruction budget
(checked at JIT block boundaries); waiting runs enter in FIFO order. The permit is
released before HLE calls, so a guest thread waiting for an event cannot hold the
only slot needed by the signalling thread. Run retries and single stepping use the
same gate. IR Interpreter, HLE services, audio, rendering and shader workers are
outside this limit. Thread/cache creation and memory budgets remain unchanged.

This is an experimental tuning option. Extra scheduling and instruction counting
can lower performance. Keep Automatic as the baseline and compare the same scene;
setting a larger limit does not split a serial game thread or guarantee higher FPS.
Dynarmic translates missing blocks synchronously on each guest CPU thread. There
is no independent JIT compiler worker count in this implementation. The `core=`
number in JIT allocation logs is an exclusive-monitor identifier, not an iPhone
physical core number.

GPU shader compiler CPU threads (1–4 or Automatic) control the existing pipeline
worker pool. They prepare graphics work on the CPU. Metal/driver scheduling owns
physical GPU execution; no GPU-core-count selector is exposed. See Apple's
[task scheduling guidance](https://developer.apple.com/library/archive/documentation/Performance/Conceptual/power_efficiency_guidelines_osx/PrioritizeWorkAtTheTaskLevel.html)
for the distinction between task priority and OS scheduling.

The supplied Attack on Titan log shows repeated swapchain creation, but does not
record both window and Vulkan extents. The renderer now keeps the SDL window size
separate from the driver's chosen surface size: differing/clamped extents must not
trigger a device-wide idle and rebuild on every frame. Real window resizes,
out-of-date/surface-lost results and V-Sync changes still request rebuilds. New
swapchain logs record both sizes to confirm the diagnosis on device. Host fixtures
cover fixed and clamped extents, rotation, zero-size windows and explicit rebuilds.

The screenshots also show picture-in-picture video and the live log. For a
repeatable performance comparison, close the video and hide the live log, then
compare identical game scenes and settings. This does not establish that either
caused the reported slowdown. Distorted in-game HUD graphics remain unverified on
a device; the concurrency option is not a graphics compatibility fix.

## Metal HUD and render diagnostics

The Performance Overlay page exposes Apple's `MetalHUDForceEnabled` user default.
Apple documents this programmatic route alongside Xcode, environment variables
and Developer settings in [Monitoring your Metal app's graphics performance](https://developer.apple.com/documentation/xcode/monitoring-your-metal-apps-graphics-performance).
The toggle requests the native HUD after restarting the process. It does not
change signing/entitlements, use private selectors or promise availability on
all iOS/provisioning combinations. The startup log records the request, not a
claim that the OS displayed it. Native HUD GPU timings are independent of
Tsubomi's guest-submit FPS display. Metal per-frame logging and encoder timing
are not enabled by this setting.

Renderer diagnostics are off by default and applied on app restart:

| Mode | Output / cost |
| --- | --- |
| Off | No counter updates, clock sampling, shader sample formatting or reports |
| Summary | Relaxed atomic event counters and two summary lines every 5 seconds; timers only around pipeline compilation |
| Summary + shader compilation samples | Above, plus up to four newly compiled shader pairs per report interval, shared across compiler workers |

Fixed-size counters replace neither existing error logs nor normal cache logs.
The added instrumentation has no growing history, no background polling thread,
no shader source/uniform dumps and no additional GPU fence waits. It still has
nonzero overhead. Compile durations use host monotonic elapsed time, summed
across workers; they are **not GPU timings or scheduled CPU time**. Compilation
and sample records may straddle summary boundaries. Cached pipelines need not
produce shader samples. A new game starts a new counter interval after boot.

`draws` counts attempted draws; `flat` is the guest flat-viewport flag, not proof
that a draw is UI. `culled`, `pending`, `feedback`, `passes`, `copies`, `uploads`
and `swapchains` count skipped clipped draws, missing pipelines, framebuffer
feedback draws, render-pass starts, casted-surface copies, texture upload calls
and swapchain creation attempts respectively. They are not bytes or GPU cost.
Pipeline logs include queued requests, completed compile calls, explicit pipeline
creation failures and total compile wall microseconds. Existing logs identify
the build, game, GPU and graphics settings.

For a 2D HUD slowdown, capture at least 10 seconds of the same scene with UI
hidden and shown, first with Summary enabled. Export `tsubomi.log` and note the
time of the UI transition. Increased feedback/passes/copies points toward the
framebuffer path; queued/compile time/pending points toward compilation; uploads
points toward texture churn. These are leads, not proof of a CPU/GPU bottleneck.
Use the native Metal HUD for GPU timing where available. Compare baseline FPS
with diagnostics and live log/PiP disabled.

## Conservative draw culling

The Graphics page has an opt-in, global Conservative draw culling switch
(restart required). It skips zero-index/zero-instance draws and fully empty
scissor draws with a vertex shader eligible for skipping. Vertex eligibility is
cached once at program creation: buffer-store flags, either USSE load/store
opcode family in primary or secondary code, and invalid code ranges disable
skipping. Rejecting read-only loads too is intentionally conservative.
Visibility queries always keep their normal path. Render-pass clears and
first-draw depth/stencil bookkeeping happen before the skip decision.

This is not world-space object, distance or occlusion culling: an emulator has no
reliable scene graph/object bounds to infer which objects a game can omit. Guest
face-culling state remains authoritative, and visible 2D HUD draws are retained.
An existing scissor bug is also fixed: negative origins now intersect with the
render target rather than expanding the unsigned extent. Tests cover negative,
outside, fractional-resolution and overflow-edge rectangles, buffer effects,
visibility queries, zero draws and preservation of clears. Real Metal graphics
correctness and a reduction in the reported UI slowdown still need device tests.

## Compact interface and periodic stutter follow-up

The supplied Undertale log (d101bc61) shows a one-pixel window/surface mismatch
(1080×1920 vs 1080×1921) without repeated swapchain creation after boot. The
20 FPS sample at 15:04:53 has no pipeline compilation, pending draws or casted
surface copies. It does not identify the cause of the hitch. Memory headroom
falls during the session; neither the FPS average nor that trend establishes
CPU, GPU or memory-pressure causality.

The live log now uses a plain translucent 300×116-point panel (clamped to the
safe area), with no blur/backdrop sampling. Drag the header, collapse to 32
points, or close it. Normalized positions persist and clamp after rotation.
Only the header intercepts touches; the text area passes touches to the game.
It copies eight recent lines instead of the complete 500-line ring, truncates
long display lines, skips unchanged UITextView text replacement and does no
text work while collapsed. The exported file retains the full log. Background
opacity is adjustable in Performance Overlay. Native dragging, touch routing
and actual frame-time improvement still require device validation.

Settings separates ordinary graphics/audio/controls from Advanced Settings
(CPU/JIT, memory, shaders, compatibility and diagnostics). Explanations are
collapsed under Details. The dedicated NoNpDrm folder import menu entry is
removed. Onboarding uses compact scrollable content and shorter copy.

Device & Runtime reads a snapshot on demand: model and hardware identifier,
iOS version, CPU architecture and logical counts, Metal GPU name, physical RAM,
app footprint, process memory headroom, disk space, thermal state, Low Power
Mode, app version and active backend. JIT availability comes from the existing
core status publication, not a new allocation/probe every time the page draws.
The initial untested state no longer defaults to available. Runtime capability
can change after the last check. No hardware benchmark or periodic sampling
thread is created for this page.

The HUD preference alone did not display Apple's HUD on the user's build.
Startup now also sets Apple's documented `MTL_HUD_ENABLED` environment variable
before renderer initialization, matching the saved preference. This is a
second documented activation route, not confirmation that the OS accepted it;
provisioning/version restrictions still apply. Device confirmation is pending.

## Attack on Titan (PCSE00812): CPU scheduling follow-up

The supplied `d2b8898` session uses an Apple A11, four concurrent guest CPU
threads, 4 MiB JIT caches per guest thread, a 512 MiB guest allocation budget,
512 texture entries and native rendering resolution. The Metal HUD is now
visible. Its GPU times in the supplied screenshots are substantially shorter
than the presentation intervals. Many low-FPS log intervals also contain no
pipeline compilation, texture uploads, surface copies or swapchain rebuilds.
This points toward CPU-side work or synchronization as a useful next target;
it does not identify the exact cost of guest logic, translation or driver work.

Two changes reduce avoidable host work without enlarging the caches:

- The explicit CPU concurrency limit now hands a released slot directly to the
  oldest waiting guest thread. It wakes one waiter instead of broadcasting to
  every waiting thread on admission and release. Waiters live on the caller's
  stack; there is no heap queue or extra worker thread. The cap, FIFO admission,
  bounded instruction slices and release before blocking HLE calls are retained.
- Scheduled boot diagnostics skip full guest-state dumps when frame submission
  is progressing. These dumps inspect threads and synchronization primitives,
  taking locks and writing many log records. Stuck boots still get early dumps,
  the eight-second stall detector is retained, and the watchdog still retires
  after its boot observation window. Renderer summaries are unchanged.

Pipeline cache saves were also inspected: they are scheduled after compilation,
so this change leaves persistence intact. Draws, visible game UI, game timing and
memory budgets are unchanged. No title-specific frame skipping or rendering
shortcut is enabled.

### Compare on the device

1. First test the same training route, camera and settings on the new build.
   Warm the scene once, then record a second pass. Keep PiP and live logging off
   for the comparison; use the same Metal HUD state in both runs.
2. Compare the current four-thread limit with **Advanced Settings → CPU & JIT →
   Concurrent CPU threads → Automatic**, restarting the app between runs.
   Automatic bypasses the admission gate and its instruction-slice accounting;
   it lets iOS schedule guest threads. Four is a concurrency cap, not a request
   for four performance cores. It can reduce throughput even with the improved
   wakeup policy, depending on the game's runnable threads.
3. Keep the current JIT/cache/RAM values for that comparison. Changing those at
   the same time would hide whether scheduling helped. A small JIT cache can
   require more translation, but this log does not measure eviction cost.
4. For a remaining slowdown, capture the same route with renderer **Summary**
   enabled, including the moment the mission banner appears. Include the new
   log and Metal HUD screenshots. CPU profiling on an actual device is still
   needed to separate guest execution, JIT compilation and renderer CPU work.

Host concurrency tests verify safety and progress. Host admission benchmarks
measure synchronization overhead only; they cannot establish an iPhone FPS gain
or that Attack on Titan maintains its target frame rate.

## Attack on Titan: black screen after the opening movie

The later `6bc3613` log renders initially, then stops submitting game frames
immediately after `LOGO_KT.mp4` ends. Guest frame count stays at 552 and renderer
draw/pass counters stay at zero. The firmware AvPlayer controller/video workers
are waiting and its demux thread has exited. Automatic CPU scheduling is active,
so this session bypasses the explicit CPU admission gate. This is a movie-end
stall to investigate; a small Metal GPU duration does not show that the GPU
renderer is disabled.

A concrete decoder defect was found in this path: `sceAvcdecDecodeStop` reported
one output with the previous frame's metadata without draining FFmpeg or writing
pixels. With frame threading, several real frames can still be buffered. Stop
now sends end-of-input to the decoder and returns actual remaining pictures and
timestamps, bounded by the guest's output-array capacity. Further Stop calls
continue until there is no output. Only then is the decoder flushed for reuse;
no frame is fabricated. Expected EAGAIN/EOF receive states are no longer warnings.

A host test using the real FFmpeg 8.1 decoder and the official FATE H264 sample
`BASQP1_Sony_C.jsv` returned one frame during input and three during draining,
recovering all four frames. Module tests cover small output arrays, repeated
Stop calls and restart. These checks establish the drain fix, not a confirmed
resolution of this particular iPhone stall. Re-test both natural movie completion
and skipping the movie on device, and capture the log if either still stalls.
