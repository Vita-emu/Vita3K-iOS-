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
