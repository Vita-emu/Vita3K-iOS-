import Foundation
import Combine

/// Live firmware-installation state.
///
/// Onboarding gates each page on one of these flags, and the import that sets
/// them runs asynchronously on the emulator thread. Rather than polling, the
/// core pushes a new settings snapshot through `FirmwareStateBridge` whenever
/// the library refreshes, and the observing views update themselves.
@MainActor
final class FirmwareState: ObservableObject {
    static let shared = FirmwareState()

    @Published private(set) var preinstalledReady = false
    @Published private(set) var fontPackageReady = false
    @Published private(set) var mainFirmwareReady = false
    /// All three official packages have populated their partitions. Games stay
    /// unavailable until this is true.
    @Published private(set) var allPackagesReady = false

    private init() {
        let settings = Bridge.currentSettings
        apply(
            preinstalled: settings.preinstalledPackageReady,
            font: settings.fontPackageReady,
            mainFirmware: settings.mainFirmwareReady,
            allReady: settings.firmwareReady
        )
    }

    fileprivate func apply(
        preinstalled: Bool,
        font: Bool,
        mainFirmware: Bool,
        allReady: Bool
    ) {
        // Assign only on change: @Published emits for every write,
        // and this is called on each library refresh.
        if preinstalledReady != preinstalled { preinstalledReady = preinstalled }
        if fontPackageReady != font { fontPackageReady = font }
        if mainFirmwareReady != mainFirmware { mainFirmwareReady = mainFirmware }
        if allPackagesReady != allReady { allPackagesReady = allReady }
    }
}

/// Objective-C entry point for pushing firmware state in.
///
/// Keeps the Objective-C entry points separate from SwiftUI published state.
@objc(TsubomiFirmwareStateBridge)
@MainActor
final class FirmwareStateBridge: NSObject {
    @objc(updateWithPreinstalledReady:fontReady:mainFirmwareReady:allReady:)
    static func update(
        preinstalledReady: Bool,
        fontReady: Bool,
        mainFirmwareReady: Bool,
        allReady: Bool
    ) {
        FirmwareState.shared.apply(
            preinstalled: preinstalledReady,
            font: fontReady,
            mainFirmware: mainFirmwareReady,
            allReady: allReady
        )
    }
}
