import Foundation
import os

/// Monitors device thermal state using ProcessInfo.
///
/// Uses NotificationCenter (no polling thread) to observe thermal state changes.
/// Posts EchoMessage.thermalState via MessageStream so the Flutter UI can
/// display warnings. Also exposes the current thermal state for pipeline stages
/// to adapt their behaviour (e.g. reduce MLX context window).
final class ThermalMonitor {

    private let messages: MessageStream
    private var observer: NSObjectProtocol?

    /// Current thermal state (0 = nominal, 1 = fair, 2 = serious, 3 = critical).
    /// Maps to the legacy integer codes the Flutter UI expects:
    /// 0 = Normal, 1 = Throttle, 2 = Critical.
    private(set) var currentMode: Int = 0

    init(messages: MessageStream) {
        self.messages = messages
    }

    /// Start observing thermal state changes.
    func start() {
        // Post the initial state immediately.
        postCurrentState()

        observer = NotificationCenter.default.addObserver(
            forName: ProcessInfo.thermalStateDidChangeNotification,
            object: nil,
            queue: .main
        ) { [weak self] _ in
            self?.handleThermalChange()
        }

        os_log("[ThermalMonitor] Started — current state: %d", currentMode)
    }

    /// Stop observing and clean up.
    func stop() {
        if let observer = observer {
            NotificationCenter.default.removeObserver(observer)
            self.observer = nil
        }
        os_log("[ThermalMonitor] Stopped")
    }

    // MARK: - Internal

    private func handleThermalChange() {
        let newState = ProcessInfo.processInfo.thermalState
        let mode = mapThermalState(newState)

        os_log("[ThermalMonitor] State changed: %d", mode)

        if mode != currentMode {
            currentMode = mode
            postCurrentState()
        }
    }

    private func postCurrentState() {
        let detail: String
        switch currentMode {
        case 0: detail = "Thermal nominal"
        case 1: detail = "Thermal throttle — reducing performance"
        case 2: detail = "Thermal critical — aggressive throttling"
        default: detail = "Unknown thermal state"
        }
        messages.post(.thermalState(mode: currentMode, detail: detail))
    }

    /// Map ProcessInfo.ThermalState to the legacy integer code.
    /// 0 = Normal, 1 = Throttle, 2 = Critical.
    private func mapThermalState(_ state: ProcessInfo.ThermalState) -> Int {
        switch state {
        case .nominal:   return 0
        case .fair:       return 0   // Still normal for UI purposes
        case .serious:    return 1   // Throttle
        case .critical:   return 2   // Critical
        @unknown default: return 0
        }
    }
}
