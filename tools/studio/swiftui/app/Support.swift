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
