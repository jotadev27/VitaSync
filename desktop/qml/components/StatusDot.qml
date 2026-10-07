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

    SequentialAnimation {
        running: root.tone === "busy" && root.visible
        loops: Animation.Infinite
        NumberAnimation { target: core; property: "opacity"; from: 1; to: 0.4; duration: 700 }
        NumberAnimation { target: core; property: "opacity"; from: 0.4; to: 1; duration: 700 }
        onStopped: core.opacity = 1
    }
}
