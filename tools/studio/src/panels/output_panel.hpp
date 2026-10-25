#pragma once

#include <string>
#include <vector>

namespace dbc::studio
{
    namespace panels
    {
        // Run/build console. Replaces the launcher OutputPanel and the editor log
        // pane, which are now the same widget.
        class OutputPanel
        {
          public:
            enum class Severity
            {
                Normal,
                Error,
                Notice,
            };

            void Append(const std::string& Line, Severity SeverityValue = Severity::Normal);
            void Clear();
            void Draw();

          private:
            struct Entry
            {
                std::string Text;
                Severity Level;
            };

            bool IsSelected(size_t Index) const;
            void Select(size_t Index, bool Extend);
            // Text of the visible lines; restricted to the selection when there is
            // one and `selectionOnly` is set.
            std::string CopyText(bool SelectionOnly) const;

            std::vector<Entry> Lines_;
            std::string Filter_;
            bool AutoScroll_ = true;
            bool ScrollPending_ = false;

            // Selection is a range over m_lines; -1 means nothing selected.
            int Anchor_ = -1;
            int Cursor_ = -1;

            // Rebuilt every frame: indices of the lines the filter lets through.
            std::vector<size_t> Visible_;
        };
    } // namespace panels

} // namespace dbc::studio
