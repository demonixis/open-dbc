import SwiftUI
import UIKit

/// The words the language colours, per profile.
final class LanguageWords {
    static let shared = LanguageWords()
    private var keywords: [Int: Set<String>] = [:]
    private var commands: [Int: Set<String>] = [:]

    func keywords(_ profile: DBCProfile) -> Set<String> {
        if let cached = keywords[profile.rawValue] { return cached }
        let words = Set(DBCStudio.shared.keywords(for: profile))
        keywords[profile.rawValue] = words
        return words
    }

    func commands(_ profile: DBCProfile) -> Set<String> {
        if let cached = commands[profile.rawValue] { return cached }
        // Flow control wins over the command list when a word is in both.
        let words = Set(DBCStudio.shared.commandWords(for: profile)).subtracting(keywords(profile))
        commands[profile.rawValue] = words
        return words
    }
}

/// Colours BASIC, and the comments, strings and numbers of the other files the editor opens.
struct Highlighter {
    var basic: Bool
    var keywords: Set<String> = []
    var commands: Set<String> = []

    struct Colors {
        let text = Palette.uiColor("Text")
        let keyword = Palette.uiColor("Keyword")
        let command = Palette.uiColor("Command")
        let string = Palette.uiColor("String")
        let number = Palette.uiColor("Number")
        let comment = Palette.uiColor("Comment")
        let errorLine = Palette.uiColor("ErrorLine")
    }

    private static func isWord(_ c: unichar) -> Bool {
        (c >= 97 && c <= 122) || (c >= 65 && c <= 90) || (c >= 48 && c <= 57) || c == 95
    }

    private static func isDigit(_ c: unichar) -> Bool {
        c >= 48 && c <= 57
    }

    func apply(to storage: NSTextStorage, font: UIFont, errorLines: Set<Int>) {
        let colors = Colors()
        let text = storage.string as NSString
        let length = text.length
        storage.beginEditing()
        storage.setAttributes([.font: font, .foregroundColor: colors.text], range: NSRange(location: 0, length: length))

        var inBlock = false
        var lineNumber = 0
        var lineStart = 0
        while lineStart <= length {
            lineNumber += 1
            var lineEnd = lineStart
            while lineEnd < length && text.character(at: lineEnd) != 10 { lineEnd += 1 }
            if errorLines.contains(lineNumber) && lineEnd > lineStart {
                storage.addAttribute(.backgroundColor, value: colors.errorLine,
                                     range: NSRange(location: lineStart, length: lineEnd - lineStart))
            }
            colorLine(text, from: lineStart, to: lineEnd, inBlock: &inBlock, storage: storage, colors: colors)
            if lineEnd >= length { break }
            lineStart = lineEnd + 1
        }
        storage.endEditing()
    }

    private func colorLine(_ text: NSString, from start: Int, to end: Int, inBlock: inout Bool,
                           storage: NSTextStorage, colors: Colors) {
        func paint(_ color: UIColor, _ from: Int, _ to: Int) {
            if to > from {
                storage.addAttribute(.foregroundColor, value: color, range: NSRange(location: from, length: to - from))
            }
        }
        func word(at index: Int) -> (String, Int) {
            var stop = index
            while stop < end && Self.isWord(text.character(at: stop)) { stop += 1 }
            return (text.substring(with: NSRange(location: index, length: stop - index)).lowercased(), stop)
        }

        var index = start
        if inBlock {
            // REMSTART ... REMEND: the block ends on the line that holds REMEND.
            paint(colors.comment, start, end)
            while index < end {
                if Self.isWord(text.character(at: index)) {
                    let (name, stop) = word(at: index)
                    if name == "remend" { inBlock = false }
                    index = stop
                } else {
                    index += 1
                }
            }
            return
        }
        while index < end {
            let c = text.character(at: index)
            if c == 34 {
                var stop = index + 1
                while stop < end && text.character(at: stop) != 34 { stop += 1 }
                stop = min(stop + 1, end)
                paint(colors.string, index, stop)
                index = stop
            } else if c == 96 || (c == 47 && index + 1 < end && text.character(at: index + 1) == 47) {
                paint(colors.comment, index, end)
                return
            } else if Self.isDigit(c) {
                var stop = index
                while stop < end && (Self.isWord(text.character(at: stop)) || text.character(at: stop) == 46) { stop += 1 }
                paint(colors.number, index, stop)
                index = stop
            } else if Self.isWord(c) {
                let (name, stop) = word(at: index)
                if basic {
                    if name == "rem" {
                        paint(colors.comment, index, end)
                        return
                    }
                    if name == "remstart" {
                        paint(colors.comment, index, end)
                        inBlock = true
                        return
                    }
                    if keywords.contains(name) {
                        paint(colors.keyword, index, stop)
                    } else if commands.contains(name) {
                        paint(colors.command, index, stop)
                    }
                }
                index = stop
            } else {
                index += 1
            }
        }
    }
}

/// Line numbers, drawn beside the text they number.
final class GutterView: UIView {
    weak var textView: CodeTextView?

    override func draw(_ rect: CGRect) {
        guard let textView else { return }
        Palette.uiColor("Panel").setFill()
        UIRectFill(bounds)
        let layout = textView.layoutManager
        let storage = textView.textStorage
        let visible = CGRect(x: 0, y: textView.contentOffset.y - textView.textContainerInset.top,
                             width: textView.bounds.width, height: textView.bounds.height)
        let glyphs = layout.glyphRange(forBoundingRect: visible, in: textView.textContainer)
        let font = UIFont.monospacedDigitSystemFont(ofSize: textView.codeFont.pointSize * 0.8, weight: .regular)
        let errors = textView.document?.errors ?? [:]
        let draw = { (line: Int, y: CGFloat, height: CGFloat) in
            let isError = errors[line] != nil
            let attributes: [NSAttributedString.Key: Any] = [
                .font: font,
                .foregroundColor: isError ? Palette.uiColor("Error") : Palette.uiColor("LineNumber"),
            ]
            let label = "\(line)" as NSString
            let size = label.size(withAttributes: attributes)
            label.draw(at: CGPoint(x: self.bounds.width - size.width - 6, y: y + (height - size.height) / 2),
                       withAttributes: attributes)
        }
        layout.enumerateLineFragments(forGlyphRange: glyphs) { fragment, _, _, range, _ in
            let characters = layout.characterRange(forGlyphRange: range, actualGlyphRange: nil)
            // Only the first fragment of a wrapped line carries its number.
            guard let line = textView.lineNumber(startingAt: characters.location) else { return }
            draw(line, fragment.minY + textView.textContainerInset.top - textView.contentOffset.y, fragment.height)
        }
        // The empty line after a final newline has no glyph of its own.
        if storage.length == 0 || (storage.string as NSString).character(at: storage.length - 1) == 10 {
            let extra = layout.extraLineFragmentRect
            if extra.height > 0 {
                draw(textView.lineCount, extra.minY + textView.textContainerInset.top - textView.contentOffset.y,
                     extra.height)
            }
        }
    }
}

/// The list of suggestions under the caret.
final class CompletionPopup: UIView, UITableViewDataSource, UITableViewDelegate {
    private let table = UITableView(frame: .zero, style: .plain)
    private let help = UILabel()
    private(set) var completion: DBCCompletion?
    private(set) var selected = 0
    var onAccept: ((DBCCompletionItem) -> Void)?
    static let rowHeight: CGFloat = 32
    static let width: CGFloat = 380

    override init(frame: CGRect) {
        super.init(frame: frame)
        layer.cornerRadius = 8
        layer.borderWidth = 1
        clipsToBounds = true
        table.dataSource = self
        table.delegate = self
        table.rowHeight = Self.rowHeight
        table.separatorStyle = .none
        help.numberOfLines = 3
        help.font = .preferredFont(forTextStyle: .caption1)
        addSubview(table)
        addSubview(help)
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:) is not used")
    }

    var items: [DBCCompletionItem] { completion?.items ?? [] }

    func show(_ completion: DBCCompletion) {
        self.completion = completion
        selected = 0
        backgroundColor = Palette.uiColor("PanelAlt")
        layer.borderColor = Palette.uiColor("Border").cgColor
        table.backgroundColor = .clear
        help.textColor = Palette.uiColor("TextDim")
        table.reloadData()
        updateHelp()
        if !items.isEmpty {
            table.scrollToRow(at: IndexPath(row: 0, section: 0), at: .top, animated: false)
        }
    }

    var preferredHeight: CGFloat {
        CGFloat(min(items.count, 7)) * Self.rowHeight + helpHeight
    }

    private var helpHeight: CGFloat {
        (help.text ?? "").isEmpty ? 0 : 56
    }

    override func layoutSubviews() {
        super.layoutSubviews()
        table.frame = CGRect(x: 0, y: 0, width: bounds.width, height: bounds.height - helpHeight)
        help.frame = CGRect(x: 8, y: bounds.height - helpHeight, width: bounds.width - 16, height: helpHeight)
    }

    func move(by offset: Int) {
        guard !items.isEmpty else { return }
        // The list wraps around.
        selected = ((selected + offset) % items.count + items.count) % items.count
        table.reloadData()
        table.scrollToRow(at: IndexPath(row: selected, section: 0), at: .none, animated: false)
        updateHelp()
        setNeedsLayout()
    }

    var current: DBCCompletionItem? {
        items.indices.contains(selected) ? items[selected] : nil
    }

    private func updateHelp() {
        guard let item = current else {
            help.text = ""
            return
        }
        var lines = item.help.signatures.prefix(2).joined(separator: "\n")
        if !item.help.warning.isEmpty { lines += (lines.isEmpty ? "" : "\n") + item.help.warning }
        if !item.help.text.isEmpty { lines += (lines.isEmpty ? "" : "\n") + item.help.text }
        help.text = lines
    }

    func tableView(_ tableView: UITableView, numberOfRowsInSection section: Int) -> Int {
        items.count
    }

    func tableView(_ tableView: UITableView, cellForRowAt indexPath: IndexPath) -> UITableViewCell {
        let cell = tableView.dequeueReusableCell(withIdentifier: "item")
            ?? UITableViewCell(style: .value1, reuseIdentifier: "item")
        let item = items[indexPath.row]
        cell.textLabel?.text = item.name
        cell.textLabel?.font = .monospacedSystemFont(ofSize: 14, weight: .regular)
        cell.textLabel?.textColor = Palette.uiColor(item.kind == 0 ? "Command" : "Identifier")
        cell.detailTextLabel?.text = item.detail
        cell.detailTextLabel?.font = .preferredFont(forTextStyle: .caption1)
        cell.detailTextLabel?.textColor = Palette.uiColor("TextDim")
        cell.backgroundColor = indexPath.row == selected ? Palette.uiColor("Selection") : .clear
        cell.selectionStyle = .none
        return cell
    }

    func tableView(_ tableView: UITableView, didSelectRowAt indexPath: IndexPath) {
        onAccept?(items[indexPath.row])
    }
}

/// The code editor of one tab: a text view with line numbers, colours, completion and hints.
final class CodeTextView: UITextView, UITextViewDelegate {
    weak var document: EditorDocument?
    private let gutter = GutterView()
    private let popup = CompletionPopup()
    private let hint = UILabel()
    private var highlighter = Highlighter(basic: false)
    private var lineStarts: [Int] = [0]
    private var highlightWork: DispatchWorkItem?
    private var symbolWork: DispatchWorkItem?
    private var replacing = false
    private(set) var codeFont = UIFont.monospacedSystemFont(ofSize: 14, weight: .regular)
    private static let baseFontSize: CGFloat = 14

    init(document: EditorDocument) {
        let storage = NSTextStorage()
        let layout = NSLayoutManager()
        let container = NSTextContainer()
        container.widthTracksTextView = true
        layout.addTextContainer(container)
        storage.addLayoutManager(layout)
        super.init(frame: .zero, textContainer: container)
        self.document = document
        delegate = self
        autocorrectionType = .no
        autocapitalizationType = .none
        smartQuotesType = .no
        smartDashesType = .no
        smartInsertDeleteType = .no
        spellCheckingType = .no
        keyboardDismissMode = .interactive
        alwaysBounceVertical = true
        isFindInteractionEnabled = true
        text = document.text

        gutter.textView = self
        gutter.isOpaque = true
        gutter.isUserInteractionEnabled = false
        addSubview(gutter)
        popup.isHidden = true
        popup.onAccept = { [weak self] item in self?.accept(item) }
        addSubview(popup)
        hint.isHidden = true
        hint.numberOfLines = 1
        hint.layer.cornerRadius = 4
        hint.layer.masksToBounds = true
        addSubview(hint)

        addGestureRecognizer(UIPinchGestureRecognizer(target: self, action: #selector(pinched(_:))))
        applyAppearance()
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:) is not used")
    }

    // MARK: Appearance

    func applyAppearance() {
        let model = StudioModel.shared
        codeFont = .monospacedSystemFont(ofSize: Self.baseFontSize * model.textZoom, weight: .regular)
        backgroundColor = Palette.uiColor("Background")
        tintColor = Palette.uiColor("Accent")
        keyboardAppearance = DBCStudio.shared.appearanceIsLight ? .light : .dark
        let profile = DBCStudio.shared.profile
        let basic = document?.isBasic ?? false
        highlighter = Highlighter(basic: basic,
                                  keywords: basic ? LanguageWords.shared.keywords(profile) : [],
                                  commands: basic ? LanguageWords.shared.commands(profile) : [])
        typingAttributes = [.font: codeFont, .foregroundColor: Palette.uiColor("Text")]
        rebuildLines()
        highlight()
        setNeedsLayout()
    }

    private var gutterWidth: CGFloat {
        let digits = max(3, String(lineCount).count)
        return CGFloat(digits) * codeFont.pointSize * 0.55 + 14
    }

    override func layoutSubviews() {
        super.layoutSubviews()
        let width = gutterWidth
        if textContainerInset.left != width + 4 {
            textContainerInset = UIEdgeInsets(top: 8, left: width + 4, bottom: 8, right: 8)
        }
        gutter.frame = CGRect(x: contentOffset.x, y: contentOffset.y, width: width, height: bounds.height)
        gutter.setNeedsDisplay()
    }

    @objc private func pinched(_ recognizer: UIPinchGestureRecognizer) {
        guard recognizer.state == .changed else { return }
        StudioModel.shared.setTextZoom(StudioModel.shared.textZoom * recognizer.scale)
        recognizer.scale = 1
    }

    // MARK: Lines

    var lineCount: Int { lineStarts.count }

    private func rebuildLines() {
        let string = textStorage.string as NSString
        var starts = [0]
        var index = 0
        let length = string.length
        while index < length {
            if string.character(at: index) == 10 { starts.append(index + 1) }
            index += 1
        }
        lineStarts = starts
    }

    /// The 1-based number of the line that starts at a character, or nil when none does.
    func lineNumber(startingAt location: Int) -> Int? {
        var low = 0
        var high = lineStarts.count - 1
        while low <= high {
            let middle = (low + high) / 2
            if lineStarts[middle] == location { return middle + 1 }
            if lineStarts[middle] < location { low = middle + 1 } else { high = middle - 1 }
        }
        return nil
    }

    /// The 0-based index of the line holding a character.
    private func lineIndex(of location: Int) -> Int {
        var low = 0
        var high = lineStarts.count - 1
        while low < high {
            let middle = (low + high + 1) / 2
            if lineStarts[middle] <= location { low = middle } else { high = middle - 1 }
        }
        return low
    }

    func goToLine(_ line: Int) {
        let index = min(max(line, 1), lineStarts.count) - 1
        let location = lineStarts[index]
        becomeFirstResponder()
        selectedRange = NSRange(location: location, length: 0)
        layoutManager.ensureLayout(for: textContainer)
        if let position = position(from: beginningOfDocument, offset: location) {
            // The line lands in the middle of the view.
            var rect = caretRect(for: position)
            rect.origin.y -= bounds.height / 2 - rect.height
            rect.size.height = bounds.height - rect.height
            scrollRectToVisible(rect, animated: false)
        }
    }

    // MARK: Text

    func flushText() {
        guard let document else { return }
        StudioModel.shared.textChanged(document, text: text)
    }

    /// Replaces the whole text as one edit that undo takes back.
    func replaceAll(with newText: String) {
        guard let range = textRange(from: beginningOfDocument, to: endOfDocument) else { return }
        replacing = true
        replace(range, withText: newText)
        replacing = false
        textDidSettle()
    }

    func trimTrailingWhitespace() {
        let trimmed = text.components(separatedBy: "\n").map { line -> String in
            var end = line.endIndex
            while end > line.startIndex {
                let previous = line.index(before: end)
                guard line[previous] == " " || line[previous] == "\t" || line[previous] == "\r" else { break }
                end = previous
            }
            return String(line[..<end])
        }.joined(separator: "\n")
        if trimmed != text {
            let selection = selectedRange
            replaceAll(with: trimmed)
            selectedRange = NSRange(location: min(selection.location, (trimmed as NSString).length), length: 0)
        }
    }

    func insertSnippet(_ insertion: DBCSnippetInsertion) {
        switch insertion.place {
        case 1:
            let index = min(max(insertion.line, 0), lineStarts.count - 1)
            selectedRange = NSRange(location: lineStarts[index], length: 0)
        case 2:
            selectedRange = NSRange(location: textStorage.length, length: 0)
        default:
            break
        }
        if let range = selectedTextRange {
            replace(range, withText: insertion.text)
        }
        scrollRangeToVisible(selectedRange)
    }

    var selectedText: String {
        (textStorage.string as NSString).substring(with: selectedRange)
    }

    private func textDidSettle() {
        rebuildLines()
        flushText()
        scheduleHighlight()
        setNeedsLayout()
    }

    private func scheduleHighlight() {
        highlightWork?.cancel()
        // A large file is coloured once typing pauses.
        if textStorage.length < 60_000 {
            highlight()
            return
        }
        let work = DispatchWorkItem { [weak self] in self?.highlight() }
        highlightWork = work
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.2, execute: work)
    }

    func highlight() {
        let errors = Set((document?.errors ?? [:]).keys)
        // Colours are not edits: undo must not see them, nor the selection move.
        let selection = selectedRange
        undoManager?.disableUndoRegistration()
        highlighter.apply(to: textStorage, font: codeFont, errorLines: errors)
        undoManager?.enableUndoRegistration()
        selectedRange = selection
        gutter.setNeedsDisplay()
    }

    private func scheduleSymbols() {
        guard let document, document.isBasic else { return }
        symbolWork?.cancel()
        let work = DispatchWorkItem { [weak self] in
            guard let self, let document = self.document else { return }
            DBCStudio.shared.refreshSymbols(forText: self.text, path: document.path)
        }
        symbolWork = work
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.3, execute: work)
    }

    // MARK: Completion

    private struct CaretLine {
        let line: NSString
        let start: Int
        let cursor: Int
        let number: Int
        let before: String
    }

    private func caretLine() -> CaretLine? {
        guard selectedRange.length == 0 else { return nil }
        let string = textStorage.string as NSString
        let location = min(selectedRange.location, string.length)
        let index = lineIndex(of: location)
        let start = lineStarts[index]
        var end = start
        while end < string.length && string.character(at: end) != 10 { end += 1 }
        let line = string.substring(with: NSRange(location: start, length: end - start)) as NSString
        return CaretLine(line: line, start: start, cursor: location - start, number: index + 1,
                         before: string.substring(to: start))
    }

    func updateCompletion(manual: Bool) {
        guard let document, document.isBasic, let caret = caretLine() else {
            hideCompletion()
            return
        }
        let completion = DBCStudio.shared.completeLine(caret.line as String, cursor: caret.cursor, manual: manual,
                                                       lineNumber: caret.number, textBefore: caret.before)
        if let completion, !completion.items.isEmpty {
            popup.show(completion)
            placePopup()
            popup.isHidden = false
        } else {
            popup.isHidden = true
        }
        updateHint(caret)
    }

    private func hideCompletion() {
        popup.isHidden = true
        hint.isHidden = true
    }

    private func placePopup() {
        guard let range = selectedTextRange else { return }
        let caret = caretRect(for: range.start)
        let height = popup.preferredHeight
        let visibleBottom = contentOffset.y + bounds.height - adjustedContentInset.bottom
        var y = caret.maxY + 4
        // No room below: the list goes above the line.
        if y + height > visibleBottom && caret.minY - height - 4 > contentOffset.y {
            y = caret.minY - height - 4
        }
        let x = min(max(caret.minX, gutterWidth + 4), max(gutterWidth + 4, bounds.width - CompletionPopup.width - 8))
        popup.frame = CGRect(x: x, y: y, width: min(CompletionPopup.width, bounds.width - gutterWidth - 12),
                             height: height)
        bringSubviewToFront(popup)
    }

    private func updateHint(_ caret: CaretLine) {
        let before = caret.line.substring(to: caret.cursor)
        let overloads = DBCStudio.shared.parameterHint(forLine: before, textBefore: caret.before)
        guard let overload = overloads.first(where: { $0.active >= 0 }) ?? overloads.first,
              let range = selectedTextRange else {
            hint.isHidden = true
            return
        }
        let font = UIFont.monospacedSystemFont(ofSize: 12, weight: .regular)
        let text = NSMutableAttributedString(string: " " + overload.prefix,
                                             attributes: [.font: font, .foregroundColor: Palette.uiColor("TextDim")])
        for (index, parameter) in overload.parameters.enumerated() {
            if index > 0 {
                text.append(NSAttributedString(string: ", ", attributes: [.font: font, .foregroundColor: Palette.uiColor("TextDim")]))
            }
            let active = index == overload.active
            text.append(NSAttributedString(string: parameter, attributes: [
                .font: active ? UIFont.monospacedSystemFont(ofSize: 12, weight: .bold) : font,
                .foregroundColor: Palette.uiColor(active ? "Text" : "TextDim"),
            ]))
        }
        text.append(NSAttributedString(string: overload.suffix + " ",
                                       attributes: [.font: font, .foregroundColor: Palette.uiColor("TextDim")]))
        hint.attributedText = text
        hint.backgroundColor = Palette.uiColor("PanelAlt")
        let size = hint.sizeThatFits(CGSize(width: bounds.width, height: 24))
        let caretRect = caretRect(for: range.start)
        // Above the line, or below it when the list is above.
        let above = popup.isHidden || popup.frame.minY > caretRect.minY
        hint.frame = CGRect(x: max(gutterWidth + 4, min(caretRect.minX, bounds.width - size.width - 8)),
                            y: above ? caretRect.minY - 24 : caretRect.maxY + 2, width: size.width, height: 22)
        hint.isHidden = false
        bringSubviewToFront(hint)
    }

    private func accept(_ item: DBCCompletionItem) {
        guard let completion = popup.completion, let caret = caretLine() else { return }
        let start = caret.start + completion.replaceStart
        let end = caret.start + max(completion.replaceEnd, caret.cursor)
        guard let from = position(from: beginningOfDocument, offset: start),
              let to = position(from: beginningOfDocument, offset: end),
              let range = textRange(from: from, to: to) else { return }
        replace(range, withText: item.insertion)
        popup.isHidden = true
    }

    // MARK: Keys

    override var keyCommands: [UIKeyCommand]? {
        var commands = super.keyCommands ?? []
        commands.append(UIKeyCommand(input: " ", modifierFlags: .control, action: #selector(completeManually)))
        return commands
    }

    @objc private func completeManually() {
        updateCompletion(manual: true)
    }

    override func pressesBegan(_ presses: Set<UIPress>, with event: UIPressesEvent?) {
        if !popup.isHidden, let key = presses.first?.key {
            switch key.keyCode {
            case .keyboardUpArrow:
                popup.move(by: -1)
                return
            case .keyboardDownArrow:
                popup.move(by: 1)
                return
            case .keyboardPageUp:
                popup.move(by: -10)
                return
            case .keyboardPageDown:
                popup.move(by: 10)
                return
            case .keyboardEscape:
                popup.isHidden = true
                return
            default:
                break
            }
        } else if !hint.isHidden, presses.first?.key?.keyCode == .keyboardEscape {
            hint.isHidden = true
            return
        }
        super.pressesBegan(presses, with: event)
    }

    override func insertText(_ inserted: String) {
        if inserted == "\t" {
            if !popup.isHidden, let item = popup.current {
                accept(item)
            } else {
                super.insertText("    ")
            }
            return
        }
        if inserted == "\n" {
            // Return accepts only a suggestion that is certain; otherwise it starts a line.
            if !popup.isHidden, popup.completion?.selection == 2, let item = popup.current {
                accept(item)
                return
            }
            popup.isHidden = true
            // The new line starts where this one does.
            var indent = ""
            if let caret = caretLine() {
                var index = 0
                while index < caret.cursor, let scalar = UnicodeScalar(caret.line.character(at: index)),
                      scalar == " " || scalar == "\t" {
                    indent.unicodeScalars.append(scalar)
                    index += 1
                }
            }
            super.insertText("\n" + indent)
            return
        }
        super.insertText(inserted)
    }

    // MARK: UITextViewDelegate

    func textViewDidChange(_ textView: UITextView) {
        textDidSettle()
        guard !replacing else { return }
        scheduleSymbols()
        updateCompletion(manual: false)
    }

    func textViewDidChangeSelection(_ textView: UITextView) {
        // The caret moved without typing: the suggestions were for another place.
        if !popup.isHidden, let completion = popup.completion, let caret = caretLine() {
            if caret.cursor < completion.replaceStart {
                popup.isHidden = true
            }
        } else if selectedRange.length > 0 {
            hideCompletion()
        }
    }

    func textViewDidEndEditing(_ textView: UITextView) {
        hideCompletion()
    }
}

/// Shows the text view of the current tab. Each tab keeps its own, and with it its undo history.
struct CodeEditor: UIViewRepresentable {
    @ObservedObject var document: EditorDocument
    let zoom: Double
    let themeRevision: Int
    let projectRevision: Int

    final class Coordinator {
        var zoom: Double = 0
        var themeRevision = -1
        var projectRevision = -1
        var errors: [Int: String] = [:]
    }

    func makeCoordinator() -> Coordinator {
        Coordinator()
    }

    func makeUIView(context: Context) -> UIView {
        UIView()
    }

    func updateUIView(_ container: UIView, context: Context) {
        let textView: CodeTextView
        if let existing = document.textView {
            textView = existing
        } else {
            textView = CodeTextView(document: document)
            document.textView = textView
        }
        if textView.superview !== container {
            container.subviews.forEach { $0.removeFromSuperview() }
            textView.frame = container.bounds
            textView.autoresizingMask = [.flexibleWidth, .flexibleHeight]
            container.addSubview(textView)
        }
        let coordinator = context.coordinator
        if coordinator.zoom != zoom || coordinator.themeRevision != themeRevision
            || coordinator.projectRevision != projectRevision {
            coordinator.zoom = zoom
            coordinator.themeRevision = themeRevision
            coordinator.projectRevision = projectRevision
            textView.applyAppearance()
        }
        if coordinator.errors != document.errors {
            coordinator.errors = document.errors
            textView.highlight()
        }
        if let line = document.pendingLine {
            document.pendingLine = nil
            DispatchQueue.main.async { textView.goToLine(line) }
        }
    }
}
