import ReplayKit
import UIKit

final class SampleHandler: RPBroadcastSampleHandler, @unchecked Sendable {
    private static let statusInterval = DispatchTimeInterval.seconds(1)
    private static let errorDomain = "com.ios.deskhub.broadcast"
    private static let errorBufferBytes = 320

    private let statusQueue = DispatchQueue(label: "com.ios.deskhub.broadcast.status")
    private var statusTimer: DispatchSourceTimer?

    override func broadcastStarted(withSetupInfo _: [String: NSObject]?) {
        dhb_start_broadcast(BroadcastStatus.containerURL?.path, UIDevice.current.model)
        BroadcastStatus().save()
        startPublishingStatus()
    }

    override func broadcastFinished() {
        statusTimer?.cancel()
        dhb_finish_broadcast()
        BroadcastStatus.clear()
    }

    override func processSampleBuffer(
        _ sampleBuffer: CMSampleBuffer, with sampleBufferType: RPSampleBufferType
    ) {
        guard sampleBufferType == .video,
              let pixelBuffer = CMSampleBufferGetImageBuffer(sampleBuffer)
        else {
            return
        }

        dhb_push_frame(Unmanaged.passUnretained(pixelBuffer).toOpaque())
    }

    private func startPublishingStatus() {
        let timer = DispatchSource.makeTimerSource(queue: statusQueue)
        timer.schedule(deadline: .now(), repeating: SampleHandler.statusInterval)
        timer.setEventHandler { [weak self] in self?.publishStatus() }
        statusTimer = timer
        timer.resume()
    }

    private func publishStatus() {
        let failure = startFailure()
        BroadcastStatus(
            sharing: dhb_sharing(),
            viewers: Int(dhb_viewer_count()),
            memoryMB: Int(dhb_memory_footprint_mb()),
            error: failure
        ).save()

        guard !failure.isEmpty else { return }
        statusTimer?.cancel()
        dhb_finish_broadcast()
        finishBroadcastWithError(
            NSError(
                domain: SampleHandler.errorDomain,
                code: 1,
                userInfo: [NSLocalizedDescriptionKey: failure]
            )
        )
    }

    private func startFailure() -> String {
        var buffer = [CChar](repeating: 0, count: SampleHandler.errorBufferBytes)
        let written = Int(dhb_last_error(&buffer, Int32(buffer.count)))
        guard written > 0 else { return "" }
        let bytes = buffer.prefix(written).map { UInt8(bitPattern: $0) }
        return String(bytes: bytes, encoding: .utf8) ?? ""
    }
}
