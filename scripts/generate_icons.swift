// SPDX-License-Identifier: GPL-2.0-only
// Run on macOS: swift scripts/generate_icons.swift
// Generated assets are checked in; normal builds need no image conversion tools.
import Foundation
import CoreGraphics
import ImageIO

let root = URL(fileURLWithPath: #filePath).deletingLastPathComponent().deletingLastPathComponent()
let icons = root.appendingPathComponent("resources/icons")
let source = CGImageSourceCreateWithURL(icons.appendingPathComponent("emu51.png") as CFURL, nil)!
let image = CGImageSourceCreateImageAtIndex(source, 0, nil)!
let temporary = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
let iconset = temporary.appendingPathComponent("emu51.iconset")
try FileManager.default.createDirectory(at: iconset, withIntermediateDirectories: true)
defer { try? FileManager.default.removeItem(at: temporary) }

var pngs = [Int: Data]()
for size in [16, 24, 32, 48, 64, 128, 256, 512, 1024] {
    let context = CGContext(data: nil, width: size, height: size, bitsPerComponent: 8,
                            bytesPerRow: size * 4, space: CGColorSpace(name: CGColorSpace.sRGB)!,
                            bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue
                                | CGBitmapInfo.byteOrder32Big.rawValue)!
    context.interpolationQuality = .high
    context.draw(image, in: CGRect(x: 0, y: 0, width: size, height: size))
    let rendered = context.makeImage()!
    let png = NSMutableData()
    let destination = CGImageDestinationCreateWithData(png, "public.png" as CFString, 1, nil)!
    CGImageDestinationAddImage(destination, rendered, nil)
    precondition(CGImageDestinationFinalize(destination), "PNG encoding failed")
    pngs[size] = png as Data
    try (png as Data).write(to: icons.appendingPathComponent("emu51-\(size).png"))

    if size == 32 || size == 64 {
        // GLFW requires straight-alpha RGBA, whereas CoreGraphics draws premultiplied RGBA.
        var pixels = Array(UnsafeBufferPointer(start: context.data!.assumingMemoryBound(to: UInt8.self),
                                              count: size * size * 4))
        for offset in stride(from: 0, to: pixels.count, by: 4) {
            let alpha = Int(pixels[offset + 3])
            for channel in 0..<3 {
                pixels[offset + channel] = alpha == 0 ? 0
                    : UInt8(min(255, (Int(pixels[offset + channel]) * 255 + alpha / 2) / alpha))
            }
        }
        try Data(pixels).write(to: icons.appendingPathComponent("emu51-\(size).rgba"))
    }
}

for (name, size) in [("16x16", 16), ("16x16@2x", 32), ("32x32", 32), ("32x32@2x", 64),
                     ("128x128", 128), ("128x128@2x", 256), ("256x256", 256),
                     ("256x256@2x", 512), ("512x512", 512), ("512x512@2x", 1024)] {
    try pngs[size]!.write(to: iconset.appendingPathComponent("icon_\(name).png"))
}
let iconutil = Process()
iconutil.executableURL = URL(fileURLWithPath: "/usr/bin/iconutil")
iconutil.arguments = ["-c", "icns", "-o", icons.appendingPathComponent("emu51.icns").path, iconset.path]
try iconutil.run()
iconutil.waitUntilExit()
precondition(iconutil.terminationStatus == 0, "iconutil failed")

// Windows ICO directory with PNG frames (supported since Windows Vista).
func littleEndian(_ value: Int, bytes: Int) -> Data {
    Data((0..<bytes).map { UInt8(truncatingIfNeeded: value >> ($0 * 8)) })
}
let sizes = [16, 24, 32, 48, 64, 128, 256]
var ico = Data([0, 0, 1, 0])
ico.append(littleEndian(sizes.count, bytes: 2))
var offset = 6 + sizes.count * 16
for size in sizes {
    let png = pngs[size]!
    ico.append(contentsOf: [UInt8(truncatingIfNeeded: size), UInt8(truncatingIfNeeded: size), 0, 0])
    ico.append(littleEndian(1, bytes: 2))
    ico.append(littleEndian(32, bytes: 2))
    ico.append(littleEndian(png.count, bytes: 4))
    ico.append(littleEndian(offset, bytes: 4))
    offset += png.count
}
for size in sizes { ico.append(pngs[size]!) }
try ico.write(to: icons.appendingPathComponent("emu51.ico"))
print("Generated macOS, Windows, Linux and GLFW icons in \(icons.path)")
