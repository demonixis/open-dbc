import SwiftUI
import UIKit

/// Interface text: the English source is the key, as in the Dear ImGui Studio.
func tr(_ source: String) -> String {
    DBCStudio.tr(source)
}

/// `tr`, then {0}, {1}... become the arguments.
func tr(_ source: String, _ arguments: String...) -> String {
    var text = DBCStudio.tr(source)
    for (index, argument) in arguments.enumerated() {
        text = text.replacingOccurrences(of: "{\(index)}", with: argument)
    }
    return text
}

/// The Studio's palette, which follows the theme setting.
enum Palette {
    static func uiColor(_ name: String) -> UIColor {
        let argb = UInt32(truncatingIfNeeded: DBCStudio.shared.paletteColor(name))
        return UIColor(
            red: CGFloat((argb >> 16) & 0xFF) / 255.0,
            green: CGFloat((argb >> 8) & 0xFF) / 255.0,
            blue: CGFloat(argb & 0xFF) / 255.0,
            alpha: CGFloat((argb >> 24) & 0xFF) / 255.0)
    }

    static func color(_ name: String) -> Color {
        Color(uiColor: uiColor(name))
    }
}

extension Color {
    /// 0x00RRGGBB, as the level's statements take colours.
    init(rgb: Int) {
        self.init(
            red: Double((rgb >> 16) & 0xFF) / 255.0,
            green: Double((rgb >> 8) & 0xFF) / 255.0,
            blue: Double(rgb & 0xFF) / 255.0)
    }

    var rgb: Int {
        var red: CGFloat = 0
        var green: CGFloat = 0
        var blue: CGFloat = 0
        var alpha: CGFloat = 0
        UIColor(self).getRed(&red, green: &green, blue: &blue, alpha: &alpha)
        let byte = { (channel: CGFloat) in Int((min(max(channel, 0), 1) * 255).rounded()) }
        return (byte(red) << 16) | (byte(green) << 8) | byte(blue)
    }
}

extension String {
    var fileName: String { (self as NSString).lastPathComponent }
    var parentPath: String { (self as NSString).deletingLastPathComponent }
}

/// A panel's title bar, as every panel of the Studio has one.
struct PanelHeader<Trailing: View>: View {
    let title: String
    @ViewBuilder var trailing: () -> Trailing

    var body: some View {
        HStack(spacing: 8) {
            Text(title).font(.subheadline.weight(.semibold)).lineLimit(1)
            Spacer(minLength: 4)
            trailing()
        }
        .padding(.horizontal, 10)
        .frame(minHeight: 34)
        .background(Palette.color("PanelAlt"))
    }
}

extension PanelHeader where Trailing == EmptyView {
    init(title: String) {
        self.init(title: title) { EmptyView() }
    }
}

/// The edge between two panels: dragged, it resizes the one whose size it holds.
struct ResizeHandle: View {
    @Binding var size: Double
    let range: ClosedRange<Double>
    /// True for an edge dragged up and down.
    let vertical: Bool
    /// 1 when dragging right or down grows the panel, -1 when it shrinks it.
    let grows: Double
    @State private var start: Double?

    var body: some View {
        Capsule()
            .fill(Palette.color("TextDim"))
            .frame(width: vertical ? 36 : 3, height: vertical ? 3 : 36)
            .opacity(start == nil ? 0.5 : 1)
            .frame(maxWidth: vertical ? .infinity : 9, maxHeight: vertical ? 9 : .infinity)
            // A background reaches under the safe area, as the panels' own do.
            .background(Palette.color("PanelAlt"))
        .contentShape(Rectangle())
        // The handle moves with the panel: its own coordinates would feed the drag back.
        .gesture(DragGesture(minimumDistance: 1, coordinateSpace: .global)
            .onChanged { drag in
                let from = start ?? size
                start = from
                let moved = vertical ? drag.translation.height : drag.translation.width
                size = min(max(from + grows * Double(moved), range.lowerBound), range.upperBound)
            }
            .onEnded { _ in start = nil })
        .accessibilityLabel(tr("Resize"))
    }
}

extension UIApplication {
    /// The controller on top of the key window, which is the one that may present another.
    var topController: UIViewController? {
        let windows = connectedScenes.compactMap { $0 as? UIWindowScene }.flatMap(\.windows)
        var top = (windows.first(where: \.isKeyWindow) ?? windows.first)?.rootViewController
        while let presented = top?.presentedViewController { top = presented }
        return top
    }
}
