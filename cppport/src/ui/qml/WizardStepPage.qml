import QtQuick
import QtQuick.Layouts
import GSender

// An accessory wizard step's page: it says when it is done; `finish` closes
// the installer (a page that loaded a file into the visualizer).
ColumnLayout {
    property AccessoryModel model
    property var installer
    property bool complete: false
    signal finish()

    spacing: 12
}
