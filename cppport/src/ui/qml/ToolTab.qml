import QtQuick

// One of the tool area's tabs (ToolsWidget): its label, content, and whether
// it is shown (Spindle/Laser, Coolant and Rotary can be switched off).
QtObject {
    property string key
    property string label
    property Component component
    property bool shown: true
}
