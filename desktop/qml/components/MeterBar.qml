import QtQuick
import VitaSync.Ui

// Continuous progress, with a moving band for work of unknown length.
Item {
    id: root

    property real value: 0            // 0..1
    property color accent: Theme.blue
    property bool indeterminate: false
    property int segments: 32

    implicitHeight: 6
    implicitWidth: 200

    Rectangle {
        anchors.fill: parent
        radius: height / 2
        color: Theme.line
    }
    Rectangle {
        visible: !root.indeterminate
        width: root.width * Math.max(0, Math.min(1, root.value))
        height: root.height
        radius: height / 2
        color: root.accent
        Behavior on width { NumberAnimation { duration: Theme.fast } }
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
