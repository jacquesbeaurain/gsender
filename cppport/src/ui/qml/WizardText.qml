import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

// A wizard page's paragraph (bold, lists and breaks allowed).
Label {
    Layout.fillWidth: true
    wrapMode: Text.Wrap
    textFormat: Text.StyledText
    color: Theme.contentPrimary
}
