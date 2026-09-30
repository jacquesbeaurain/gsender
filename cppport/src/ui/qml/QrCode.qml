import QtQuick
import GSender

// A QR code of `text` (react-qr-code): black on white, the quiet zone
// included, in a white frame so it scans on the dark theme too.
Rectangle {
    id: qr

    property string text
    property string level: "L"
    property int size: 200

    implicitWidth: size
    implicitHeight: size
    color: "white"
    radius: 4

    Image {
        objectName: "qrImage"
        anchors.fill: parent
        source: qr.text ? "image://qr/" + qr.level + "/" + encodeURIComponent(qr.text) : ""
        sourceSize: Qt.size(qr.size, qr.size)
        fillMode: Image.PreserveAspectFit
        smooth: false
        cache: false
    }
}
