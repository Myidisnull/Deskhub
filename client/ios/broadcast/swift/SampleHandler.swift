import ReplayKit

final class SampleHandler: RPBroadcastSampleHandler {
    private static let statusInterval: TimeInterval = 1
    private static let errorDomain = "com.ios.deskhub.broadcast"

    private var lastStatusAt = Date.distantPast

    override func broadcastStarted(withSetupInfo _: [String: NSObject]?) {
        if let path = BroadcastStatus.containerURL?.path {
            dhb_use_app_group(path)
        }
        BroadcastStatus().save()
    }

    override func broadcastFinished() {
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

        let stamp = CMSampleBufferGetPresentationTimeStamp(sampleBuffer)
        let micros = CMTimeGetSeconds(stamp) * 1_000_000
        dhb_push_frame(
            Unmanaged.passUnretained(pixelBuffer).toOpaque(),
            UInt64(max(0, micros))
        )
        publishStatus()
    }

    private func publishStatus() {
        let now = Date()
        guard now.timeIntervalSince(lastStatusAt) >= SampleHandler.statusInterval else { return }
        lastStatusAt = now

        let failure = startFailure()
        BroadcastStatus(
            sharing: dhb_sharing(), viewers: Int(dhb_viewer_count()), error: failure
        ).save()

        guard !failure.isEmpty else { return }
        finishBroadcastWithError(
            NSError(
                domain: SampleHandler.errorDomain,
                code: 1,
                userInfo: [NSLocalizedDescriptionKey: failure]
            )
        )
    }

    private func startFailure() -> String {
        var buffer = [CChar](repeating: 0, count: 320)
        let written = Int(dhb_last_error(&buffer, Int32(buffer.count)))
        guard written > 0 else { return "" }
        let bytes = buffer.prefix(written).map { UInt8(bitPattern: $0) }
        return String(bytes: bytes, encoding: .utf8) ?? ""
    }
}
