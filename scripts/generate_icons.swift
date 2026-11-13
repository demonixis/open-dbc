// Generates the application icons from resources/branding/logo_mark.png:
//   resources/editor.png   1024 px, full bleed: Windows, Linux, Android, iOS and exported games
//   resources/editor.ico   Windows
//   resources/editor.icns  macOS, on the system icon grid
// Usage (macOS): swift scripts/generate_icons.swift

import CoreGraphics
import Foundation
import ImageIO
import UniformTypeIdentifiers

let root = URL(fileURLWithPath: #filePath).deletingLastPathComponent().deletingLastPathComponent()
let resources = root.appendingPathComponent("resources")
let srgb = CGColorSpace(name: CGColorSpace.sRGB)!

func fail(_ message: String) -> Never {
    FileHandle.standardError.write((message + "\n").data(using: .utf8)!)
    exit(1)
}

func color(_ r: Int, _ g: Int, _ b: Int, _ a: CGFloat = 1) -> CGColor {
    CGColor(colorSpace: srgb, components: [CGFloat(r) / 255, CGFloat(g) / 255, CGFloat(b) / 255, a])!
}

let markURL = resources.appendingPathComponent("branding/logo_mark.png")
guard let markSource = CGImageSourceCreateWithURL(markURL as CFURL, nil),
      let mark = CGImageSourceCreateImageAtIndex(markSource, 0, nil)
else { fail("Cannot read \(markURL.path)") }

// The mark's geometry on its 1024 px canvas, y down. Small icons redraw it with thicker strokes,
// because the scaled image's lines fall under a pixel.
let apex = CGPoint(x: 512.0, y: 112.0), bottom = CGPoint(x: 512.0, y: 912.0)
let left = CGPoint(x: 199.2, y: 476.1), right = CGPoint(x: 824.8, y: 547.9)
let front = CGPoint(x: 399.2, y: 604.3)
let cyan = color(0x5F, 0xE3, 0xFF), gold = color(0xF5, 0xC2, 0x42)
let vectorBelow = 128

func drawVectorMark(_ ctx: CGContext, in rect: CGRect) {
    let scale = rect.width / 1024
    func point(_ p: CGPoint) -> CGPoint {
        CGPoint(x: rect.minX + p.x * scale, y: rect.maxY - p.y * scale)
    }
    ctx.setFillColor(gold)
    ctx.addLines(between: [point(apex), point(left), point(front)])
    ctx.closePath()
    ctx.fillPath()

    ctx.saveGState()
    ctx.setShadow(offset: .zero, blur: 22.6 * scale, color: cyan)
    ctx.setStrokeColor(cyan)
    ctx.setLineWidth(max(14.36 * scale, 1.3))
    ctx.setLineJoin(.round)
    ctx.setLineCap(.round)
    ctx.addLines(between: [point(apex), point(right), point(bottom), point(left)])
    ctx.closePath()
    for end in [apex, bottom, left, right] {
        ctx.move(to: point(end))
        ctx.addLine(to: point(front))
    }
    ctx.strokePath()
    ctx.restoreGState()

    ctx.setFillColor(color(255, 255, 255))
    let radius = max(17.9 * scale, 1.0)
    for vertex in [apex, bottom, left, right, front] {
        let centre = point(vertex)
        ctx.fillEllipse(in: CGRect(x: centre.x - radius, y: centre.y - radius, width: radius * 2, height: radius * 2))
    }
}

// macGrid leaves the margin of the macOS icon grid around the body; otherwise the body fills the canvas.
func render(_ size: Int, macGrid: Bool) -> CGContext {
    guard let ctx = CGContext(data: nil, width: size, height: size, bitsPerComponent: 8, bytesPerRow: size * 4,
                              space: srgb, bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)
    else { fail("Cannot create a \(size) px bitmap") }
    let side = CGFloat(size)
    let body = macGrid ? CGRect(x: side * 100 / 1024, y: side * 100 / 1024, width: side * 824 / 1024,
                                height: side * 824 / 1024)
                       : CGRect(x: 0, y: 0, width: side, height: side)
    let radius = body.width * (macGrid ? 0.225 : 0.18)
    let outline = CGPath(roundedRect: body, cornerWidth: radius, cornerHeight: radius, transform: nil)

    ctx.saveGState()
    ctx.addPath(outline)
    ctx.clip()
    let gradient = CGGradient(colorsSpace: srgb, colors: [color(20, 48, 58), color(5, 12, 18)] as CFArray,
                              locations: [0, 1])!
    let centre = CGPoint(x: body.midX, y: body.midY)
    ctx.drawRadialGradient(gradient, startCenter: centre, startRadius: 0, endCenter: centre,
                           endRadius: body.width * 0.72, options: [.drawsAfterEndLocation])
    let markRect = body.insetBy(dx: body.width * 0.07, dy: body.width * 0.07)
    if size < vectorBelow {
        drawVectorMark(ctx, in: markRect)
    } else {
        ctx.interpolationQuality = .high
        ctx.draw(mark, in: markRect)
    }
    ctx.restoreGState()

    // A faint rim keeps the body's edge visible on dark backgrounds.
    let rim = max(side / 256, 1)
    let inner = body.insetBy(dx: rim / 2, dy: rim / 2)
    ctx.addPath(CGPath(roundedRect: inner, cornerWidth: radius - rim / 2, cornerHeight: radius - rim / 2,
                       transform: nil))
    ctx.setStrokeColor(color(0x5F, 0xE3, 0xFF, 0.28))
    ctx.setLineWidth(rim)
    ctx.strokePath()
    return ctx
}

func pngData(_ ctx: CGContext) -> Data {
    let data = NSMutableData()
    guard let image = ctx.makeImage(),
          let destination = CGImageDestinationCreateWithData(data, UTType.png.identifier as CFString, 1, nil)
    else { fail("Cannot encode a PNG") }
    CGImageDestinationAddImage(destination, image, nil)
    guard CGImageDestinationFinalize(destination) else { fail("Cannot encode a PNG") }
    return data as Data
}

func write(_ data: Data, to url: URL) {
    do { try data.write(to: url) } catch { fail("Cannot write \(url.path): \(error.localizedDescription)") }
}

func append<T: FixedWidthInteger>(_ value: T, to data: inout Data) {
    withUnsafeBytes(of: value.littleEndian) { data.append(contentsOf: $0) }
}

// A 32-bit icon bitmap: bottom-up BGRA rows with straight alpha, then an empty AND mask.
func icoBitmap(_ ctx: CGContext) -> Data {
    let size = ctx.width
    let pixels = ctx.data!.assumingMemoryBound(to: UInt8.self)
    var data = Data()
    append(UInt32(40), to: &data)
    append(Int32(size), to: &data)
    append(Int32(size * 2), to: &data)
    append(UInt16(1), to: &data)
    append(UInt16(32), to: &data)
    for _ in 0..<6 { append(UInt32(0), to: &data) }
    for row in stride(from: size - 1, through: 0, by: -1) {
        for column in 0..<size {
            let p = pixels + row * ctx.bytesPerRow + column * 4
            let alpha = Int(p[3])
            func straight(_ v: UInt8) -> UInt8 { alpha == 0 ? 0 : UInt8(min(255, (Int(v) * 255 + alpha / 2) / alpha)) }
            data.append(contentsOf: [straight(p[2]), straight(p[1]), straight(p[0]), p[3]])
        }
    }
    data.append(Data(count: ((size + 31) / 32) * 4 * size))
    return data
}

write(pngData(render(1024, macGrid: false)), to: resources.appendingPathComponent("editor.png"))

// Windows reads the 256 px entry as PNG and the smaller ones as bitmaps.
let icoSizes = [256, 128, 64, 48, 32, 16]
let icoImages = icoSizes.map { size -> Data in
    let ctx = render(size, macGrid: false)
    return size == 256 ? pngData(ctx) : icoBitmap(ctx)
}
var ico = Data()
append(UInt16(0), to: &ico)
append(UInt16(1), to: &ico)
append(UInt16(icoSizes.count), to: &ico)
var offset = 6 + 16 * icoSizes.count
for (size, image) in zip(icoSizes, icoImages) {
    ico.append(contentsOf: [UInt8(size & 0xFF), UInt8(size & 0xFF), 0, 0])
    append(UInt16(1), to: &ico)
    append(UInt16(32), to: &ico)
    append(UInt32(image.count), to: &ico)
    append(UInt32(offset), to: &ico)
    offset += image.count
}
icoImages.forEach { ico.append($0) }
write(ico, to: resources.appendingPathComponent("editor.ico"))

let iconset = FileManager.default.temporaryDirectory.appendingPathComponent("opendbc-\(UUID().uuidString).iconset")
do { try FileManager.default.createDirectory(at: iconset, withIntermediateDirectories: true) } catch {
    fail("Cannot create \(iconset.path)")
}
defer { try? FileManager.default.removeItem(at: iconset) }
for points in [16, 32, 128, 256, 512] {
    write(pngData(render(points, macGrid: true)), to: iconset.appendingPathComponent("icon_\(points)x\(points).png"))
    write(pngData(render(points * 2, macGrid: true)),
          to: iconset.appendingPathComponent("icon_\(points)x\(points)@2x.png"))
}
let iconutil = Process()
iconutil.executableURL = URL(fileURLWithPath: "/usr/bin/iconutil")
iconutil.arguments = ["-c", "icns", iconset.path, "-o", resources.appendingPathComponent("editor.icns").path]
do { try iconutil.run() } catch { fail("Cannot run iconutil") }
iconutil.waitUntilExit()
if iconutil.terminationStatus != 0 { fail("iconutil failed") }
print("Wrote editor.png, editor.ico and editor.icns to \(resources.path)")
