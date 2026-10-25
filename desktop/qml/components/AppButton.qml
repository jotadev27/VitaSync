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
    property bool spinIcon: false
    property bool dangerous: false
    readonly property bool hovered: hover.hovered
    property string accessibleName: text.length > 0 ? text : iconName

    signal clicked()

    readonly property color effectiveAccent: dangerous ? Theme.purple : accent
    readonly property bool isPrimary: variant === "primary"
    readonly property bool isQuiet: variant === "quiet"
    readonly property color fill: dangerous ? Theme.purpleDim
        : (accent === Theme.blue ? Theme.primaryFill
           : (accent === Theme.inkMuted ? Theme.raised : Qt.darker(effectiveAccent, 1.7)))

    implicitHeight: Theme.controlHeight
    implicitWidth: content.implicitWidth + (isQuiet ? 18 : 30)
    activeFocusOnTab: true
    Accessible.role: Accessible.Button
    Accessible.name: accessibleName
    Accessible.onPressAction: if (enabled) root.clicked()
    Keys.onSpacePressed: if (enabled) root.clicked()
    Keys.onReturnPressed: if (enabled) root.clicked()
    opacity: enabled || busy ? 1 : 0.42

    Rectangle {
        id: plate
        anchors.fill: parent
        radius: Theme.radius
        color: {
            if (root.isPrimary)
                return hover.hovered ? Qt.lighter(root.fill, 1.15) : root.fill
            if (root.isQuiet)
                return hover.hovered ? Theme.surfaceAlt : "transparent"
            return hover.hovered ? Theme.surfaceAlt : Theme.surface
        }
        border.width: root.activeFocus ? 2 : (root.isPrimary || root.isQuiet ? 0 : Theme.border)
        border.color: root.activeFocus ? Theme.blue : (hover.hovered ? Theme.inkMuted : Theme.lineStrong)
        scale: press.pressed ? 0.98 : 1

        Behavior on border.color { ColorAnimation { duration: Theme.fast } }
    }

    Row {
        id: content
        anchors.centerIn: parent
        spacing: root.text.length > 0 && root.iconName.length > 0 ? 8 : 0

        Icon {
            id: buttonIcon
            visible: root.iconName.length > 0
            name: root.iconName
            size: 18
            anchors.verticalCenter: parent.verticalCenter
            // Filled controls use the same white label and icon.
            color: root.isPrimary ? Theme.primaryInk
                                  : (hover.hovered ? root.effectiveAccent : Theme.inkMuted)
            RotationAnimation on rotation {
                running: root.spinIcon && root.busy
                from: 0
                to: 360
                duration: 800
                loops: Animation.Infinite
                onStopped: buttonIcon.rotation = 0
            }
        }

        Text {
            visible: root.text.length > 0
            anchors.verticalCenter: parent.verticalCenter
            text: root.text
            font.family: Theme.sansFamily
            font.pixelSize: Theme.sizeBody
            font.weight: root.isPrimary ? Font.DemiBold : Font.Medium
            color: root.isPrimary ? Theme.primaryInk
                                  : (hover.hovered ? Theme.ink : Theme.inkMuted)
        }
    }

    // A busy button keeps its label and grows a moving underline, so the
    // control does not jump or change size while work is in flight.
    Rectangle {
        visible: root.busy
        height: 2
        width: parent.width * 0.4
        color: root.isPrimary ? Theme.primaryInk : root.effectiveAccent
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
    TapHandler {
        id: press
        enabled: root.enabled
        gesturePolicy: TapHandler.ReleaseWithinBounds
        onTapped: { root.forceActiveFocus(Qt.MouseFocusReason); root.clicked() }
    }
}
