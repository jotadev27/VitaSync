import QtQuick
import VitaSync.Ui

// Every button in the app. One height, one radius, three weights:
//   primary   -- the single action a screen exists for
//   secondary -- outlined, everything else
//   quiet     -- borderless, for row-level actions
Item {
    id: root

    property string text: ""
    property string iconName: ""
    property string variant: "secondary"     // primary | secondary | quiet
    property color accent: Theme.blue
    property bool busy: false
    property bool dangerous: false

    signal clicked()

    readonly property color effectiveAccent: dangerous ? Theme.purple : accent
    readonly property bool isPrimary: variant === "primary"
    readonly property bool isQuiet: variant === "quiet"

    implicitHeight: Theme.controlHeight
    implicitWidth: content.implicitWidth + (isQuiet ? 18 : 30)
    opacity: enabled ? 1 : 0.42

    Rectangle {
        id: plate
        anchors.fill: parent
        radius: Theme.radius
        color: {
            if (root.isPrimary)
                return hover.hovered ? Qt.lighter(root.effectiveAccent, 1.12) : root.effectiveAccent
            if (root.isQuiet)
                return hover.hovered ? Theme.surfaceAlt : "transparent"
            return hover.hovered ? Theme.surfaceAlt : Theme.surface
        }
        border.width: root.isPrimary || root.isQuiet ? 0 : Theme.border
        border.color: hover.hovered ? root.effectiveAccent : Theme.lineStrong

        Behavior on color { ColorAnimation { duration: Theme.fast } }
        Behavior on border.color { ColorAnimation { duration: Theme.fast } }
    }

    Row {
        id: content
        anchors.centerIn: parent
        spacing: root.text.length > 0 && root.iconName.length > 0 ? 8 : 0

        Icon {
            visible: root.iconName.length > 0
            name: root.iconName
            size: 16
            anchors.verticalCenter: parent.verticalCenter
            // On a filled button the label sits on the accent, so the mark has
            // to flip to the dark plane to stay readable.
            color: root.isPrimary ? Theme.base
                                  : (hover.hovered ? root.effectiveAccent : Theme.inkMuted)
            Behavior on color { ColorAnimation { duration: Theme.fast } }
        }

        Text {
            visible: root.text.length > 0
            anchors.verticalCenter: parent.verticalCenter
            text: root.text
            font.family: Theme.sansFamily
            font.pixelSize: Theme.sizeBody
            font.weight: root.isPrimary ? Font.DemiBold : Font.Medium
            color: root.isPrimary ? Theme.base
                                  : (hover.hovered ? Theme.ink : Theme.inkMuted)
            Behavior on color { ColorAnimation { duration: Theme.fast } }
        }
    }

    // A busy button keeps its label and grows a moving underline, so the
    // control does not jump or change size while work is in flight.
    Rectangle {
        visible: root.busy
        height: 2
        width: parent.width * 0.4
        color: root.isPrimary ? Theme.base : root.effectiveAccent
        anchors.bottom: parent.bottom
        opacity: 0.9

        SequentialAnimation on x {
            running: root.busy
            loops: Animation.Infinite
            NumberAnimation { from: 0; to: root.width * 0.6; duration: 720; easing.type: Easing.InOutQuad }
            NumberAnimation { from: root.width * 0.6; to: 0; duration: 720; easing.type: Easing.InOutQuad }
        }
    }

    HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor; enabled: root.enabled }
    TapHandler { enabled: root.enabled; onTapped: root.clicked() }
}
