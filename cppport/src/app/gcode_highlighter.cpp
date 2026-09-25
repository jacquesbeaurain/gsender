#include "gcode_highlighter.hpp"

#include "gs/gcode/highlight.hpp"

#include <QColor>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextDocument>

namespace gs::app {

QList<QTextLayout::FormatRange> gcodeFormats(const QString& line, bool dark, int offset) {
    const QByteArray utf8 = line.toUtf8();
    QList<QTextLayout::FormatRange> ranges;
    // The runs count UTF-8 bytes; a QString position counts UTF-16 units.
    int position = offset;
    qsizetype byte = 0;
    for (const gcode::HighlightRun& run :
         gcode::highlightLine(std::string_view(utf8.constData(), static_cast<std::size_t>(utf8.size())))) {
        int units = 0;
        for (const qsizetype end = byte + static_cast<qsizetype>(run.length); byte < end; ++byte) {
            const auto c = static_cast<unsigned char>(utf8[byte]);
            if ((c & 0xC0) != 0x80) {
                units += c >= 0xF0 ? 2 : 1;
            }
        }
        QTextLayout::FormatRange range;
        range.start = position;
        range.length = units;
        range.format.setForeground(QColor::fromRgb(gcode::highlightColor(run.cls, dark)));
        ranges.append(range);
        position += units;
    }
    return ranges;
}

namespace {

void setBlockFormats(QTextDocument& document, const QTextBlock& block,
                     const QList<QTextLayout::FormatRange>& formats) {
    QTextLayout* layout = block.layout();
    if (!layout) {
        return;
    }
    layout->setFormats(formats);
    document.markContentsDirty(block.position(), block.length());
}

}  // namespace

void colourGcodeBlock(QTextDocument& document, const QTextBlock& block, bool dark) {
    setBlockFormats(document, block, gcodeFormats(block.text(), dark));
}

void clearBlockColours(QTextDocument& document, const QTextBlock& block) {
    setBlockFormats(document, block, {});
}

}  // namespace gs::app
