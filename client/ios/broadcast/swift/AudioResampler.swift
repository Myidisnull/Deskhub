import AVFoundation

final class AudioResampler {
    private let input: AVAudioFormat
    private let output: AVAudioFormat
    private let converter: AVAudioConverter?

    init(from input: AVAudioFormat, to output: AVAudioFormat) {
        self.input = input
        self.output = output
        converter = AVAudioConverter(from: input, to: output)
    }

    func matches(_ candidate: AVAudioFormat) -> Bool {
        candidate.sampleRate == input.sampleRate
            && candidate.channelCount == input.channelCount
            && candidate.commonFormat == input.commonFormat
    }

    func render(_ source: AVAudioPCMBuffer) -> [Int16]? {
        guard let converter else { return nil }

        let ratio = output.sampleRate / input.sampleRate
        let capacity = AVAudioFrameCount(Double(source.frameLength) * ratio) + 1024
        guard let target = AVAudioPCMBuffer(pcmFormat: output, frameCapacity: capacity) else {
            return nil
        }

        var offered = false
        var conversionError: NSError?
        let outcome = converter.convert(to: target, error: &conversionError) { _, status in
            if offered {
                status.pointee = .noDataNow
                return nil
            }
            offered = true
            status.pointee = .haveData
            return source
        }
        guard outcome == .haveData || outcome == .inputRanDry, conversionError == nil else {
            return nil
        }

        guard let channel = target.int16ChannelData, target.frameLength > 0 else { return nil }
        let count = Int(target.frameLength) * Int(output.channelCount)
        return Array(UnsafeBufferPointer(start: channel[0], count: count))
    }
}

extension AVAudioPCMBuffer {
    static func deskhubBuffer(from sampleBuffer: CMSampleBuffer) -> AVAudioPCMBuffer? {
        guard let description = CMSampleBufferGetFormatDescription(sampleBuffer),
              let streamDescription = CMAudioFormatDescriptionGetStreamBasicDescription(description)
        else {
            return nil
        }

        let format = AVAudioFormat(streamDescription: streamDescription)
        let frames = AVAudioFrameCount(CMSampleBufferGetNumSamples(sampleBuffer))
        guard let format, frames > 0,
              let buffer = AVAudioPCMBuffer(pcmFormat: format, frameCapacity: frames)
        else {
            return nil
        }
        buffer.frameLength = frames

        let status = CMSampleBufferCopyPCMDataIntoAudioBufferList(
            sampleBuffer,
            at: 0,
            frameCount: Int32(frames),
            into: buffer.mutableAudioBufferList
        )
        return status == noErr ? buffer : nil
    }
}
