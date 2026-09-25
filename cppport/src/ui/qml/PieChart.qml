import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Shapes
import GSender

// A pie or doughnut chart with its legend - what the Stats page draws with
// Chart.js: slices clockwise from the top with white borders, the legend
// above (a tap hides or shows a slice), a slice's label and value shown
// under the chart when it is tapped.
Item {
    id: chart

    property var labels: []
    property var values: []
    property var colors: []
    property bool doughnut: false
    property string seriesLabel
    // The value as the tooltip says it (a function), else "<series>: <value>".
    property var valueText: null
    property var hidden: ({})
    property int picked: -1

    readonly property real total: {
        let sum = 0
        for (let i = 0; i < values.length; ++i)
            if (!hidden[i])
                sum += Number(values[i])
        return sum
    }
    function colorOf(i) { return colors.length ? colors[i % colors.length] : Theme.blue[500] }
    function describe(i) {
        const value = Number(values[i])
        return labels[i] + ": " + (valueText ? valueText(value) : (seriesLabel ? seriesLabel + " " : "") + value)
    }
    // Where each slice starts and how far it goes, degrees from the top.
    function angles(i) {
        let start = 0
        for (let j = 0; j < i; ++j)
            if (!hidden[j])
                start += Number(values[j]) / total * 360
        return { start: start, span: hidden[i] || total <= 0 ? 0 : Number(values[i]) / total * 360 }
    }

    implicitWidth: 240
    implicitHeight: 260

    Flow {
        id: legend
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.min(implicitWidth, parent.width)
        spacing: 10
        Repeater {
            model: chart.labels.length
            Row {
                required property int index
                spacing: 4
                opacity: chart.hidden[index] ? 0.5 : 1
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 30; height: 12
                    color: chart.colorOf(index)
                }
                Label {
                    text: chart.labels[index]
                    font.pixelSize: Theme.fontXs
                    font.strikeout: !!chart.hidden[index]
                    color: Theme.contentSecondary
                }
                TapHandler {
                    onTapped: {
                        const next = Object.assign({}, chart.hidden)
                        next[index] = !next[index]
                        chart.hidden = next
                    }
                }
            }
        }
    }

    Item {
        id: pie
        anchors.top: legend.bottom
        anchors.topMargin: 8
        anchors.bottom: caption.top
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.min(parent.width, height)
        readonly property real radius: width / 2 - 2

        Repeater {
            model: chart.values.length
            Shape {
                id: slice
                required property int index
                readonly property var arc: chart.angles(index)
                anchors.fill: parent
                visible: arc.span > 0
                ShapePath {
                    strokeColor: Theme.dark ? Theme.surfaceRaised : "white"
                    strokeWidth: 2
                    fillColor: chart.colorOf(slice.index)
                    startX: pie.width / 2; startY: pie.height / 2
                    PathAngleArc {
                        centerX: pie.width / 2; centerY: pie.height / 2
                        radiusX: pie.radius; radiusY: pie.radius
                        startAngle: slice.arc.start - 90
                        sweepAngle: slice.arc.span
                    }
                    PathLine { x: pie.width / 2; y: pie.height / 2 }
                }
            }
        }
        // The doughnut's hole.
        Rectangle {
            visible: chart.doughnut
            anchors.centerIn: parent
            width: pie.radius; height: width; radius: width / 2
            color: Theme.dark ? Theme.surfaceRaised : "white"
        }
        TapHandler {
            onTapped: (point) => {
                const dx = point.position.x - pie.width / 2
                const dy = point.position.y - pie.height / 2
                const distance = Math.hypot(dx, dy)
                chart.picked = -1
                if (distance > pie.radius || (chart.doughnut && distance < pie.radius / 2))
                    return
                let angle = Math.atan2(dy, dx) * 180 / Math.PI + 90
                if (angle < 0)
                    angle += 360
                for (let i = 0; i < chart.values.length; ++i) {
                    const arc = chart.angles(i)
                    if (arc.span > 0 && angle >= arc.start && angle < arc.start + arc.span)
                        chart.picked = i
                }
            }
        }
    }
    Label {
        id: caption
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        height: 20
        text: chart.picked >= 0 ? chart.describe(chart.picked) : ""
        font.pixelSize: Theme.fontXs
        color: Theme.contentSecondary
    }
}
