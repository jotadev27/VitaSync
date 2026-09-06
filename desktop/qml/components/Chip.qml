import QtQuick
import VitaSync.Ui

// Compact selectable token: recent addresses, mount points, filters.
Item {
    id: root

    property string text: ""
    property string iconName: ""
    property bool selected: false
    property color accent: Theme.blue
    property bool interactive: true

    signal clicked()

    implicitHeight: 26
    implicitWidth: row.implicitWidth + 20

    Rectangle {
        anchors.fill: parent
        radius: Theme.radius
        color: root.selected ? Qt.rgba(root.accent.r, root.accent.g, root.accent.b, 0.13)
                             : (hover.hovered && root.interactive ? Theme.surfaceAlt : "transparent")
        border.width: Theme.border
        border.color: root.selected ? root.accent
                                    : (hover.hovered && root.interactive ? Theme.lineStrong : Theme.line)

        Behavior on color { ColorAnimation { duration: Theme.fast } }
        Behavior on border.color { ColorAnimation { duration: Theme.fast } }
    }

    Row {
        id: row
        anchors.centerIn: parent
        spacing: root.iconName.length > 0 ? 6 : 0

        Icon {
            visible: root.iconName.length > 0
            name: root.iconName
            size: 13
            color: root.selected ? root.accent : Theme.inkFaint
            anchors.verticalCenter: parent.verticalCenter
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: root.text
            font.family: Theme.monoFamily
            font.pixelSize: Theme.sizeSmall
            color: root.selected ? root.accent : Theme.inkMuted
            Behavior on color { ColorAnimation { duration: Theme.fast } }
        }
    }

    HoverHandler { id: hover; enabled: root.interactive; cursorShape: Qt.PointingHandCursor }
    TapHandler { enabled: root.interactive; onTapped: root.clicked() }
}
