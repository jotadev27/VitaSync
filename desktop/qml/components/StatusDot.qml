import QtQuick
import VitaSync.Ui

// Connection state as one mark. Idle is a hollow ring, busy pulses, live is
// solid green. No wording -- the label beside it carries that.
Item {
    id: root

    property string tone: "idle"          // idle | busy | ok | warn | error
    property int size: 9

    implicitWidth: size
    implicitHeight: size

    readonly property color toneColor: Theme.toneColor(tone)
    readonly property bool hollow: tone === "idle"

    Rectangle {
        id: core
        anchors.centerIn: parent
        width: root.size
        height: root.size
        radius: width / 2
        color: root.hollow ? "transparent" : root.toneColor
        border.width: root.hollow ? 1.5 : 0
        border.color: Theme.inkFaint

        Behavior on color { ColorAnimation { duration: Theme.normal } }
    }

    // A halo only while something is actually happening.
    Rectangle {
        id: halo
        anchors.centerIn: core
        width: root.size
        height: root.size
        radius: width / 2
        color: "transparent"
        border.width: 1
        border.color: root.toneColor
        visible: root.tone === "busy"

        SequentialAnimation {
            running: root.tone === "busy"
            loops: Animation.Infinite
            ParallelAnimation {
                NumberAnimation { target: halo; property: "scale"; from: 1; to: 2.4; duration: 1100; easing.type: Easing.OutQuad }
                NumberAnimation { target: halo; property: "opacity"; from: 0.8; to: 0; duration: 1100 }
            }
        }
    }
}
