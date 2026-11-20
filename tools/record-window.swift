// record-window -- records ONE app's window (and that app's sound) to a
// movie with ScreenCaptureKit. Nothing else on screen is captured: not the
// desktop, not windows on top of it. Used for the demo video:
//
//   swiftc -O tools/record-window.swift -o build/record-window
//   build/record-window Azahar 150 demo.mov          # 150 seconds
//   build/record-window Azahar still shot.png        # one frame
//
// macOS 13+. Needs screen recording permission for the terminal it runs in.
import AppKit
import AVFoundation
import CoreMedia
import Foundation
import ScreenCaptureKit

final class Recorder: NSObject, SCStreamOutput, SCStreamDelegate {
    let writer: AVAssetWriter?
    let video: AVAssetWriterInput?
    let audio: AVAssetWriterInput?
    let stillURL: URL?
    var started = false
    var gotStill = false
    let done = DispatchSemaphore(value: 0)

    init(movie: URL?, still: URL?, width: Int, height: Int) throws {
        stillURL = still
        if let movie = movie {
            try? FileManager.default.removeItem(at: movie)
            let w = try AVAssetWriter(outputURL: movie, fileType: .mov)
            let v = AVAssetWriterInput(mediaType: .video, outputSettings: [
                AVVideoCodecKey: AVVideoCodecType.h264, AVVideoWidthKey: width, AVVideoHeightKey: height,
                AVVideoCompressionPropertiesKey: [AVVideoAverageBitRateKey: 8_000_000],
            ])
            v.expectsMediaDataInRealTime = true
            let a = AVAssetWriterInput(mediaType: .audio, outputSettings: [
                AVFormatIDKey: kAudioFormatMPEG4AAC, AVSampleRateKey: 48000, AVNumberOfChannelsKey: 2,
                AVEncoderBitRateKey: 192_000,
            ])
            a.expectsMediaDataInRealTime = true
            w.add(v)
            w.add(a)
            writer = w
            video = v
            audio = a
        } else {
            writer = nil
            video = nil
            audio = nil
        }
    }

    func stream(_ stream: SCStream, didOutputSampleBuffer sb: CMSampleBuffer, of type: SCStreamOutputType) {
        guard sb.isValid else { return }
        if type == .screen {
            // Only complete frames carry pixels.
            guard let att = CMSampleBufferGetSampleAttachmentsArray(sb, createIfNecessary: false) as? [[SCStreamFrameInfo: Any]],
                  let raw = att.first?[.status] as? Int, SCFrameStatus(rawValue: raw) == .complete,
                  let px = sb.imageBuffer else { return }
            if let still = stillURL, !gotStill {
                gotStill = true
                let ci = CIImage(cvPixelBuffer: px)
                let ctx = CIContext()
                if let cg = ctx.createCGImage(ci, from: ci.extent),
                   let dest = CGImageDestinationCreateWithURL(still as CFURL, "public.png" as CFString, 1, nil) {
                    CGImageDestinationAddImage(dest, cg, nil)
                    CGImageDestinationFinalize(dest)
                }
                done.signal()
                return
            }
            guard let w = writer, let v = video else { return }
            if !started {
                w.startWriting()
                w.startSession(atSourceTime: sb.presentationTimeStamp)
                started = true
            }
            if v.isReadyForMoreMediaData { v.append(sb) }
        } else if type == .audio {
            guard started, let a = audio, a.isReadyForMoreMediaData else { return }
            a.append(sb)
        }
    }
}

_ = NSApplication.shared  // a window-server connection (CoreGraphics wants one)
let args = CommandLine.arguments
guard args.count >= 4 else {
    FileHandle.standardError.write("usage: record-window <app> <seconds|still> <out>\n".data(using: .utf8)!)
    exit(1)
}
let appName = args[1].lowercased()
let stillMode = args[2] == "still"
let seconds = Double(args[2]) ?? 0
let out = URL(fileURLWithPath: args[3])

let sem = DispatchSemaphore(value: 0)
Task {
    do {
        let content = try await SCShareableContent.excludingDesktopWindows(true, onScreenWindowsOnly: false)
        let wins = content.windows.filter {
            ($0.owningApplication?.applicationName.lowercased() ?? "") == appName && $0.frame.width > 200 && $0.frame.height > 200
        }
        guard let win = wins.max(by: { $0.frame.width * $0.frame.height < $1.frame.width * $1.frame.height }) else {
            // (Only counts, not other apps' names: what else is open is none of our business.)
            let mine = content.windows.filter { ($0.owningApplication?.applicationName.lowercased() ?? "").contains(appName) }
            let sizes = mine.map { "\(Int($0.frame.width))x\(Int($0.frame.height))" }.joined(separator: " ")
            FileHandle.standardError.write("no window for \(appName): \(content.windows.count) windows seen, \(mine.count) of its own [\(sizes)]\n".data(using: .utf8)!)
            exit(2)
        }
        let filter = SCContentFilter(desktopIndependentWindow: win)
        let cfg = SCStreamConfiguration()
        let scale = 2
        let w = Int(win.frame.width) * scale / 2 * 2, h = Int(win.frame.height) * scale / 2 * 2
        cfg.width = w
        cfg.height = h
        cfg.minimumFrameInterval = CMTime(value: 1, timescale: 30)
        cfg.showsCursor = false
        cfg.capturesAudio = !stillMode
        cfg.sampleRate = 48000
        cfg.channelCount = 2
        let rec = try Recorder(movie: stillMode ? nil : out, still: stillMode ? out : nil, width: w, height: h)
        let stream = SCStream(filter: filter, configuration: cfg, delegate: rec)
        let q = DispatchQueue(label: "rec")
        try stream.addStreamOutput(rec, type: .screen, sampleHandlerQueue: q)
        if !stillMode { try stream.addStreamOutput(rec, type: .audio, sampleHandlerQueue: q) }
        try await stream.startCapture()
        if stillMode {
            _ = rec.done.wait(timeout: .now() + 5)
        } else {
            try await Task.sleep(nanoseconds: UInt64(seconds * 1e9))
        }
        try await stream.stopCapture()
        if let wr = rec.writer, rec.started {
            q.sync {
                rec.video?.markAsFinished()
                rec.audio?.markAsFinished()
            }
            await wr.finishWriting()
            print("wrote \(out.path) \(w)x\(h)")
        } else if stillMode {
            print("wrote \(out.path)")
        }
    } catch {
        FileHandle.standardError.write("error: \(error)\n".data(using: .utf8)!)
        exit(3)
    }
    sem.signal()
}
sem.wait()
