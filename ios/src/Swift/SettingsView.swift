import SwiftUI

/// The settings screen.
///
/// Deliberately plain: a `Form` in a `NavigationStack`, built from stock
/// `Toggle`/`Slider`/`Picker` rows. On iOS 26 that is what produces correct
/// Liquid Glass — the navigation bar and any sheet chrome are glass, and the
/// content underneath is opaque and scrolls beneath it. Hand-rolling glass
/// behind list rows (which the UIKit screen used to do) both fights the design
/// system and costs a refraction pass per visible row.
///
/// Everything here is a system control, so Dynamic Type, VoiceOver labels and
/// values, high-contrast, and reduce-transparency are all inherited rather than
/// reimplemented.
@MainActor
struct SettingsView: View {
    @Environment(\.scenePhase) private var scenePhase
    @StateObject private var model: SettingsModel
    @ObservedObject private var runtimeLatch = RuntimeLatch.shared
    @AppStorage("tsubomi.orientationLockEnabled")
    private var orientationLockEnabled = false
    @AppStorage("tsubomi.orientationLock")
    private var orientationLock = OrientationLockOption.portrait.rawValue
    @AppStorage(NormalListArtwork.defaultsKey)
    private var normalListArtwork = NormalListArtwork.coverArt.rawValue
    @AppStorage(LibrarySortOption.defaultsKey)
    private var librarySort = LibrarySortOption.alphabetical.rawValue
    @AppStorage("tsubomi.jitCacheMiB") private var jitCacheMiB = 0
    @AppStorage("tsubomi.shaderWorkers") private var shaderWorkers = 0
    @AppStorage("tsubomi.textureCacheEntries") private var textureCacheEntries = 0
    @AppStorage("tsubomi.trimStagingBuffers") private var trimStagingBuffers = true
    @AppStorage("tsubomi.compactPerformanceHUD") private var compactPerformanceHUD = false
    /// Invoked when the user is done; the host controller dismisses.
    private let onFinish: () -> Void

    @State private var showingResetConfirmation = false
    @State private var showingRuntimeNotice = false

    init(scope: SettingsModel.Scope, onFinish: @escaping () -> Void) {
        _model = StateObject(wrappedValue: SettingsModel(scope: scope))
        self.onFinish = onFinish
    }

    var body: some View {
        NavigationStack {
            Form {
                if !model.isPerGame && runtimeLatch.revealed {
                    runtimeSection
                }
                if !model.isPerGame {
                    generalSection
                    librarySection
                    memorySection
                }
                videoSection
                graphicsSection
                audioSection
                if !model.isPerGame {
                    controlsSection
                    performanceOverlaySection
                    firmwareSection
                }
                if model.isPerGame {
                    perGameResetSection
                }
            }
            .onDisappear { model.save() }
            .onChange(of: scenePhase) { phase in
                if phase != .active { model.save() }
            }
            .navigationTitle(model.navigationTitle)
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .principal) {
                    Text(model.navigationTitle)
                        .font(.headline)
                        .lineLimit(1)
                }
                ToolbarItem(placement: .confirmationAction) {
                    Button("Done") {
                        model.save()
                        // Per-game settings persist immediately in the native
                        // frontend. Global settings play after the core reports
                        // that its commit completed.
                        if model.isPerGame {
                            HomeSoundEffects.play(.sparkle)
                        }
                        onFinish()
                    }
                }
            }
            .alert("Developer mode enabled", isPresented: $showingRuntimeNotice) {
                Button("OK", role: .cancel) {}
            }
        }
    }

    // MARK: - Sections

    private var generalSection: some View {
        Section {
            DefaultsToggle("Interface sound effects", key: .soundEffects)
            DefaultsToggle("Liquid Glass", key: .liquidGlassInGame)
            Toggle("Orientation lock", isOn: $orientationLockEnabled)
                .onChange(of: orientationLockEnabled) { isEnabled in
                    Bridge.setOrientationLockEnabled(isEnabled)
                }
            if orientationLockEnabled {
                Picker("Locked orientation", selection: $orientationLock) {
                    ForEach(OrientationLockOption.allCases) { option in
                        Text(option.title).tag(option.rawValue)
                    }
                }
                .onChange(of: orientationLock) { newValue in
                    Bridge.applyOrientationLock(newValue)
                }
            }
        } header: {
            Text("General")
        } footer: {
            Text("Liquid Glass is turned off in game only.")
        }
    }

    private var runtimeSection: some View {
        Section {
            Button {
                exportGames()
            } label: {
                Label("Export games", systemImage: "square.and.arrow.up")
            }
            Button {
                importGames()
            } label: {
                Label("Import games…", systemImage: "square.and.arrow.down")
            }
        } header: {
            Text("Developer")
        } footer: {
            Text("Exporting a large library takes a few minutes. A share sheet opens when the archive is ready.")
        }
    }

    /// Export runs on a background thread and reports through the library's
    /// busy overlay and status toast - both of which live *behind* this sheet.
    /// So this closes Settings first, exactly as Done does, and the work is
    /// visible instead of appearing to do nothing at all.
    private func exportGames() {
        model.save()
        onFinish()
        LibraryStateBridge.setBusy("Exporting games…")
        Bridge.exportLibraryArchive()
    }

    /// Imports need the same treatment as exportGames(): once a file is picked,
    /// the install progress and its result toast are drawn on the library,
    /// behind this sheet. Leaving Settings up made a running import look like
    /// nothing had happened until the user dragged the sheet away themselves.
    /// The picker is presented once the sheet has actually gone - presenting
    /// into a controller that is still dismissing is dropped by UIKit.
    private func importGames() {
        model.save()
        onFinish()
        Bridge.presentLibraryArchiveImportPicker()
    }

    private var memorySection: some View {
        Section {
            Picker("JIT cache per guest thread", selection: $jitCacheMiB) {
                Text("Automatic").tag(0)
                ForEach([12, 16, 24, 32], id: \.self) { value in
                    Text("\(value) MiB").tag(value)
                }
            }
            Picker("Shader compiler threads", selection: $shaderWorkers) {
                Text("Automatic").tag(0)
                ForEach(1...4, id: \.self) { value in Text("\(value)").tag(value) }
            }
            Picker("Texture cache", selection: $textureCacheEntries) {
                Text("Automatic").tag(0)
                ForEach([128, 256, 512], id: \.self) { value in
                    Text("\(value) textures").tag(value)
                }
            }
            Toggle("Trim oversized upload buffers", isOn: $trimStagingBuffers)
            Button("Use iPhone 8 Plus memory settings") {
                jitCacheMiB = 12
                shaderWorkers = 1
                textureCacheEntries = 128
                trimStagingBuffers = true
                model.useLowerMemoryPreset()
            }
            Button("Reset memory settings to Automatic") {
                jitCacheMiB = 0
                shaderWorkers = 0
                textureCacheEntries = 0
                trimStagingBuffers = true
            }
        } header: {
            Text("JIT & Memory")
        } footer: {
            Text("""
                Saved immediately. Close and reopen Tsubomi to apply these settings; no rebuild is needed.                 Automatic uses 12 MiB per JIT thread and 128 cached textures on a 3 GB phone.                 JIT cache is translated code, not all RAM used by a thread. Smaller caches save memory but can cause recompilation.                 Shader threads control background GPU compilation, not the game's CPU core count; fewer use less memory but compile more slowly.                 A smaller texture cache may cause extra uploads. Upload buffer trimming releases oversized temporary GPU buffers after reuse becomes safe.                 These limits do not cap total app RAM or guarantee 30 FPS.
                """)
        }
    }

    private var videoSection: some View {
        Section("Video") {
            Toggle("V-Sync", isOn: $model.vSync)
                .accessibilityHint("Synchronizes presentation to the display.")
            Toggle("Shader cache", isOn: $model.shaderCache)
                .accessibilityHint("Reuses compiled shaders between sessions. Turn off to force regeneration when diagnosing a graphics fault.")
        }
    }

    private var graphicsSection: some View {
        Section {
            Button("Lower memory preset (0.5×)") {
                model.useLowerMemoryPreset()
            }
            // A labelled slider rather than a stepper: the multiplier is
            // continuous and the exact number matters less than the direction.
            LabeledContent("Resolution") {
                Text(model.resolutionLabel)
                    .foregroundStyle(.secondary)
                    .monospacedDigit()
            }
            Slider(value: $model.resolutionMultiplier, in: 0.5...2.0, step: 0.25) {
                Text("Resolution multiplier")
            } minimumValueLabel: {
                Text("0.5×").font(.caption2)
            } maximumValueLabel: {
                Text("2×").font(.caption2)
            }
            .accessibilityValue(model.resolutionLabel)

            Toggle("High accuracy", isOn: $model.highAccuracy)
            Toggle("Surface sync", isOn: $model.surfaceSync)
            Toggle("Double buffer", isOn: $model.doubleBuffer)
            Toggle("Async pipeline compilation", isOn: $model.asyncPipelineCompilation)

            Picker("Anisotropic filtering", selection: $model.anisotropicFiltering) {
                ForEach(SettingsModel.anisotropicOptions, id: \.self) { value in
                    Text(SettingsModel.anisotropicLabel(value)).tag(value)
                }
            }
        } header: {
            Text("Graphics")
        } footer: {
            // Replaces the library's "?" button: the explanation belongs next
            // to the switches it is about, not behind a glyph on the home
            // screen. Kept to the three symptoms people actually report.
            Text("""
                Changes save automatically and apply on the next game launch. \
                The lower memory preset renders at 480×272; 30 FPS means 33.33 ms per frame, but varies by game. \
                Graphics look wrong? Try High accuracy. \
                Lighting white or missing? Also turn on Surface sync. \
                Character models shattered? Make sure Double buffer is off.
                """)
        }
    }

    private var audioSection: some View {
        Section {
            Toggle("NGS audio", isOn: $model.ngsAudio)
            LabeledContent("Audio backend", value: "SDL")
            Toggle("CPU optimizations", isOn: $model.cpuOptimizations)
        } header: {
            Text("Audio & CPU")
        } footer: {
            Text("NGS is full Vita audio emulation; disable it only while diagnosing a problem.")
        }
    }

    private var controlsSection: some View {
        Section {
            Button("Virtual controls…") {
                Bridge.presentControllerOptions()
            }
            DefaultsToggle("Colored face buttons", key: .coloredFaceButtons)
            NavigationLink("Face button layout") {
                FaceButtonLayoutView(model: model)
            }
        } header: {
            Text("Controls")
        } footer: {
            Text("Virtual controls covers opacity, scale, layout, visibility, and physical-pad auto-hide. Colored face buttons tint the on-screen ✕ ○ □ △ glyphs. Some third-party controllers report face buttons in Xbox-style positions; remap them if the wrong button responds.")
        }
    }

    private var performanceOverlaySection: some View {
        Section {
            Toggle("Compact readout", isOn: $compactPerformanceHUD)
            DefaultsToggle("Show FPS", key: .perfFPS, onEnable: enablePerfOverlay)
            DefaultsToggle("Show frametime", key: .perfFrametime, onEnable: enablePerfOverlay)
            DefaultsToggle("Show frametime graph", key: .perfFrametimeGraph, onEnable: enablePerfOverlay)
            DefaultsToggle("Show RAM usage", key: .perfRAM, onEnable: enablePerfOverlay)
            DefaultsToggle("Show battery %", key: .perfBattery, onEnable: enablePerfOverlay)
            DefaultsToggle("Show live log", key: .perfLog, onEnable: enablePerfOverlay)
        } header: {
            Text("Performance overlay")
        } footer: {
            Text("The overlay appears in-game once any metric is enabled. The live log keeps the last ~250 lines, which is useful when reporting a bug.")
        }
    }

    private var librarySection: some View {
        Section {
            DefaultsToggle("Show title IDs", key: .showTitleIDs, onChange: Bridge.reloadLibraryCells)
            DefaultsToggle("Show version number", key: .showVersion, onChange: Bridge.reloadLibraryCells)
            DefaultsToggle("Show game size", key: .showGameSize, onChange: Bridge.reloadLibraryCells)
            DefaultsToggle("Wide cover art", key: .wideCoverArt)
            DefaultsToggle("Compact list", key: .compactList)
            Picker("Normal list artwork", selection: $normalListArtwork) {
                ForEach(NormalListArtwork.allCases) { option in
                    Text(option.title).tag(option.rawValue)
                }
            }
            .pickerStyle(.menu)
            Picker("Sort games by", selection: $librarySort) {
                ForEach(LibrarySortOption.allCases) { option in
                    Text(option.title).tag(option.rawValue)
                }
            }
            .pickerStyle(.menu)
            .onChange(of: librarySort) { newValue in
                LibraryState.shared.setSortOption(rawValue: newValue)
            }
            Button {
                // Same reason as exportGames(): the progress lives behind this
                // sheet, so close it first.
                model.save()
                onFinish()
                LibraryStateBridge.setBusy("Exporting saves…")
                Bridge.exportAllSaves()
            } label: {
                Label("Export all game saves", systemImage: "square.and.arrow.up")
            }
            Button {
                // Same reason as importGames(): close Settings so the import
                // progress on the library is actually visible.
                model.save()
                onFinish()
                Bridge.presentAllSaveImportPicker()
            } label: {
                Label("Import all game saves…", systemImage: "square.and.arrow.down")
            }
        } header: {
            Text("Library")
        } footer: {
            Text("Choose cover art or the square game icon for the normal list. Compact list always uses the game icon. All-save archives include save data, trophy progress, playtime, and last-played dates. Import replaces included progress; other games remain unchanged.")
        }
    }

    /// Enabling any metric un-hides an overlay the user dismissed in-game.
    private func enablePerfOverlay() {
        Bridge.performanceOverlayDidEnableMetric()
    }

    private var firmwareSection: some View {
        Section {
            LabeledContent("Firmware") {
                Text(model.firmwareVersion.isEmpty ? "Not installed" : model.firmwareVersion)
                    .foregroundStyle(.secondary)
            }
            .contentShape(Rectangle())
            .onTapGesture {
                if runtimeLatch.record(0x6D4E_13B7) {
                    showingRuntimeNotice = true
                }
            }
            if !model.firmwareReady && !model.missingFirmware.isEmpty {
                // A plain label, not an alert: this is steady-state
                // information, and the library already blocks launching.
                Label(model.missingFirmware, systemImage: "exclamationmark.triangle")
                    .foregroundStyle(.secondary)
            }
            LabeledContent("Version", value: AppInfo.versionDisplay)
                .contentShape(Rectangle())
                .onTapGesture {
                    if runtimeLatch.record(0xA29C_508D) {
                        showingRuntimeNotice = true
                    }
                }
            NavigationLink("What's New") {
                ChangelogView()
            }
            Button {
                Bridge.openBugReportForm()
            } label: {
                Text("Report a bug")
                    .foregroundStyle(.red)
            }
            Button {
                Bridge.shareLogFile()
            } label: {
                Label("Share log file", systemImage: "square.and.arrow.up")
            }
            Button("Forked from Vita3K") {
                Bridge.open(urlString: "https://github.com/Vita3K/Vita3K")
            }
            Button("Developed by @halcyonpalace") {
                Bridge.open(urlString: "https://x.com/halcyonpalace")
            }
        } header: {
            Text("About")
        }
    }

    private var perGameResetSection: some View {
        Section {
            Button("Use global settings", role: .destructive) {
                showingResetConfirmation = true
            }
            .confirmationDialog(
                "Remove this game's custom settings?",
                isPresented: $showingResetConfirmation,
                titleVisibility: .visible
            ) {
                Button("Use global settings", role: .destructive) {
                    model.resetPerGameOverrides()
                    onFinish()
                }
            } message: {
                Text("This game will follow the global settings the next time it launches.")
            }
        } footer: {
            Text("These settings apply only to this game and take effect the next time it launches.")
        }
    }
}

/// Raw values are shared with NativeFrontend.mm. The lock is off by default;
/// once enabled, Portrait is its initial choice. Both landscape directions
/// remain explicit so controls and cables can sit on the user's preferred side.
enum OrientationLockOption: String, CaseIterable, Identifiable {
    case portrait
    case landscape
    case landscapeFlipped

    var id: String { rawValue }

    var title: String {
        switch self {
        case .portrait: "Portrait"
        case .landscape: "Landscape"
        case .landscapeFlipped: "Landscape (Flipped)"
        }
    }
}

/// Face-button remapping, split out as its own screen: four related pickers is
/// more than a section should carry, and it keeps the root list short.
@MainActor
private struct FaceButtonLayoutView: View {
    @ObservedObject var model: SettingsModel

    private static let positions = ["Bottom", "Right", "Left", "Top"]

    var body: some View {
        Form {
            Section {
                picker("Cross", selection: $model.bindCross)
                picker("Circle", selection: $model.bindCircle)
                picker("Square", selection: $model.bindSquare)
                picker("Triangle", selection: $model.bindTriangle)
            } footer: {
                Text("Choose which physical button position triggers each Vita button.")
            }
        }
        .navigationTitle("Face Buttons")
        .navigationBarTitleDisplayMode(.inline)
    }

    private func picker(_ label: String, selection: Binding<Int>) -> some View {
        Picker(label, selection: selection) {
            ForEach(Array(Self.positions.enumerated()), id: \.offset) { index, name in
                Text(name).tag(index)
            }
        }
    }
}
