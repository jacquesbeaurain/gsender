import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

// A wizard row's state: done (a check), current (an arrow) or waiting (a dot).
Label {
    property bool done
    property bool current

    Layout.preferredWidth: 24
    horizontalAlignment: Text.AlignHCenter
    text: done ? "✔" : current ? "▶" : "•"
    color: done ? Theme.green[500] : current ? Theme.blue[500] : Theme.gray[400]
    font.pixelSize: 16
}
