import Foundation
import Photos

enum ReceivedFiles {
    static let folderName = "Deskhub"
    private static let partSuffix = ".deskhub-part"

    private static let imageExtensions: Set<String> = [
        "jpg", "jpeg", "png", "gif", "heic", "heif", "webp", "bmp", "tiff", "tif", "dng", "avif",
    ]
    private static let videoExtensions: Set<String> = ["mp4", "mov", "m4v", "3gp"]

    static var incomingDir: URL? {
        BroadcastStatus.containerURL?.appendingPathComponent(folderName, isDirectory: true)
    }

    static func sweep(sharing: Bool) async {
        guard let dir = incomingDir else { return }
        let manager = FileManager.default
        guard let entries = try? manager.contentsOfDirectory(
            at: dir, includingPropertiesForKeys: nil
        ) else { return }

        for url in entries {
            if url.lastPathComponent.hasSuffix(partSuffix) {
                if !sharing { try? manager.removeItem(at: url) }
                continue
            }
            await deliver(url)
        }
    }

    private static func deliver(_ url: URL) async {
        let ext = url.pathExtension.lowercased()
        let isImage = imageExtensions.contains(ext)
        let isVideo = videoExtensions.contains(ext)
        if isImage || isVideo, await addToPhotoLibrary(url, isVideo: isVideo) {
            try? FileManager.default.removeItem(at: url)
            return
        }
        moveToDocuments(url)
    }

    private static func addToPhotoLibrary(_ url: URL, isVideo: Bool) async -> Bool {
        let access = await PHPhotoLibrary.requestAuthorization(for: .addOnly)
        guard access == .authorized || access == .limited else { return false }
        do {
            try await PHPhotoLibrary.shared().performChanges {
                if isVideo {
                    PHAssetChangeRequest.creationRequestForAssetFromVideo(atFileURL: url)
                } else {
                    PHAssetChangeRequest.creationRequestForAssetFromImage(atFileURL: url)
                }
            }
            return true
        } catch {
            return false
        }
    }

    private static func moveToDocuments(_ url: URL) {
        let manager = FileManager.default
        guard let documents = manager.urls(for: .documentDirectory, in: .userDomainMask).first
        else { return }
        try? manager.moveItem(at: url, to: freeSlot(for: url.lastPathComponent, in: documents))
    }

    private static func freeSlot(for name: String, in directory: URL) -> URL {
        let manager = FileManager.default
        var candidate = directory.appendingPathComponent(name)
        guard manager.fileExists(atPath: candidate.path) else { return candidate }

        let base = (name as NSString).deletingPathExtension
        let ext = (name as NSString).pathExtension
        var attempt = 1
        repeat {
            let numbered = ext.isEmpty ? "\(base)-\(attempt)" : "\(base)-\(attempt).\(ext)"
            candidate = directory.appendingPathComponent(numbered)
            attempt += 1
        } while manager.fileExists(atPath: candidate.path)
        return candidate
    }
}
