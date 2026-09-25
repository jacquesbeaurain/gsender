#pragma once

// G-code syntax colouring in the Qt views (the G-code editor and the Step
// Through's source list), from gs::gcode::highlightLine - react-syntax-
// highlighter's gcode language with the a11y-light / a11y-dark themes, the
// dark one with the application's dark mode (workspace.enableDarkMode).
//
// Upstream colours only the rows on screen; so do the views here: a whole
// 200,000-line file would take seconds.

#include <QList>
#include <QTextLayout>

class QTextBlock;
class QTextDocument;

namespace gs::app {

// The line's colours as format ranges over its QString positions, shifted by
// `offset` (where the line starts in a longer text).
QList<QTextLayout::FormatRange> gcodeFormats(const QString& line, bool dark, int offset = 0);

// A text document block's colours set, or cleared, as QSyntaxHighlighter
// applies them: the layout's formats, then the block marked for relayout
// (no content-change signals).
void colourGcodeBlock(QTextDocument& document, const QTextBlock& block, bool dark);
void clearBlockColours(QTextDocument& document, const QTextBlock& block);

}  // namespace gs::app
