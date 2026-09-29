import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

Item {
    id: root
    implicitWidth: 600
    implicitHeight: 400
    property var gsender: null

    property double widthMm: 100.0
    property double heightMm: 60.0
    property double feedrate: 1200.0
    property string statusMsg: ""

    Card {
        anchors.fill: parent
        anchors.margins: 16

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 16

            Label {
                text: qsTr("Basic CAM: Surfacing Generator")
                font.pixelSize: Theme.font2xl
                font.bold: true
                color: Theme.contentPrimary
            }

            Label {
                text: qsTr("Generate rectangular pocketing / facing toolpaths and stream directly to the 3D visualizer.")
                color: Theme.contentSecondary
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            GridLayout {
                columns: 2
                rowSpacing: 12
                columnSpacing: 16

                Label { text: qsTr("Width (mm):"); color: Theme.contentPrimary; font.bold: true }
                TextField {
                    id: widthInput
                    text: root.widthMm.toString()
                    onTextChanged: root.widthMm = parseFloat(text) || 100.0
                }

                Label { text: qsTr("Height (mm):"); color: Theme.contentPrimary; font.bold: true }
                TextField {
                    id: heightInput
                    text: root.heightMm.toString()
                    onTextChanged: root.heightMm = parseFloat(text) || 60.0
                }

                Label { text: qsTr("Feedrate (mm/min):"); color: Theme.contentPrimary; font.bold: true }
                TextField {
                    id: feedInput
                    text: root.feedrate.toString()
                    onTextChanged: root.feedrate = parseFloat(text) || 1200.0
                }
            }

            GButton {
                text: qsTr("Generate & Load into Visualizer")
                variant: "primary"
                onClicked: {
                    const gcode = "G21 G90\n" +
                                  "G0 Z5.000\n" +
                                  "G0 X0 Y0\n" +
                                  "G1 Z-1.000 F300\n" +
                                  "G1 X" + root.widthMm.toFixed(3) + " F" + root.feedrate.toFixed(0) + "\n" +
                                  "G1 Y" + root.heightMm.toFixed(3) + "\n" +
                                  "G1 X0.000\n" +
                                  "G1 Y0.000\n" +
                                  "G0 Z5.000\n";
                    if (root.gsender) {
                        const res = root.gsender.send("gcode:load:to:visualizer", {
                            name: "basic_cam.gcode",
                            gcode: gcode
                        });
                        root.statusMsg = res.ok ? qsTr("G-code loaded successfully!") : qsTr("Error: ") + res.error;
                    }
                }
            }

            Label {
                text: root.statusMsg
                color: root.statusMsg.startsWith("Error") ? Theme.red[500] : Theme.green[500]
                visible: root.statusMsg !== ""
                font.bold: true
            }

            Item { Layout.fillHeight: true }
        }
    }
}
