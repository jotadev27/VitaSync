import QtQuick
import VitaSync.Ui

// Progress, drawn as a segmented meter rather than a rounded pill so it reads
// as instrumentation instead of a stock widget.
Item {
    id: root

    property real value: 0            // 0..1
    property color accent: Theme.blue
    property bool indeterminate: false
    property int segments: 32

    implicitHeight: 6
    implicitWidth: 200

    Row {
        anchors.fill: parent
        spacing: 2

        Repeater {
            model: root.segments

            Rectangle {
                width: (root.width - (root.segments - 1) * 2) / root.segments
                height: root.height
                radius: 1

                readonly property real threshold: (index + 1) / root.segments
                readonly property bool lit: root.indeterminate
                    ? false
                    : root.value >= threshold - (1 / root.segments) * 0.5

                color: lit ? root.accent : Theme.line
                Behavior on color { ColorAnimation { duration: 90 } }
            }
        }
    }

    // Indeterminate work sweeps a short band instead of filling.
    Rectangle {
        visible: root.indeterminate
        width: root.width * 0.22
        height: root.height
        radius: 1
        color: root.accent
        opacity: 0.85

        SequentialAnimation on x {
            running: root.indeterminate && root.visible
            loops: Animation.Infinite
            NumberAnimation { from: 0; to: root.width * 0.78; duration: 900; easing.type: Easing.InOutSine }
            NumberAnimation { from: root.width * 0.78; to: 0; duration: 900; easing.type: Easing.InOutSine }
        }
    }
}
