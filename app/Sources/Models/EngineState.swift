import Foundation

@Observable
class EngineState {
    var isRunning = false
    var cpuUsage: Double = 0.0
    var latencyMs: Double = 0.0
    var noiseReductionDb: Double = 0.0
}
