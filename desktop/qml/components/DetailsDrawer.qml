import QtQuick
import VitaSync
import VitaSync.Ui

// The protocol trace, collapsed by default.
//
// The main view never shows a log line. When something goes wrong this is
// where the exact FTP exchange is, verbatim, in mono, colour-coded by
// direction -- which is far more useful than an error paragraph would be.
Rectangle {
    id: root

    property bool open: false

    height: open ? 190 : 0
    clip: true
    color: Theme.surfaceAlt
    visible: height > 0

    Behavior on height { NumberAnimation { duration: Theme.normal; easing.type: Easing.OutCubic } }

    Rectangle {
        anchors.top: parent.top
        width: parent.width
        height: Theme.border
        color: Theme.lineStrong
    }

    Item {
        id: bar
        width: parent.width
        height: 32

        Row {
            anchors.left: parent.left
            anchors.leftMargin: Theme.gapLarge
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8

            Icon {
                anchors.verticalCenter: parent.verticalCenter
                name: "queue"
                size: 13
                color: Theme.inkFaint
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "PROTOCOL"
                font.family: Theme.sansFamily
                font.pixelSize: Theme.sizeMicro
                font.weight: Font.Bold
                font.letterSpacing: 1.4
                color: Theme.inkFaint
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: Device.log.length
                font.family: Theme.monoFamily
                font.pixelSize: Theme.sizeMicro
                color: Theme.blue
            }
        }

        Row {
            anchors.right: parent.right
            anchors.rightMargin: Theme.gap
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2

            AppButton {
                variant: "quiet"
                iconName: "trash"
                onClicked: Device.clearLog()
            }

            AppButton {
                variant: "quiet"
                iconName: "close"
                onClicked: root.open = false
            }
        }
    }

    ListView {
        id: lines
        anchors.top: bar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: Theme.gapLarge
        anchors.rightMargin: Theme.gapLarge
        anchors.bottomMargin: Theme.gap
        clip: true
        model: Device.log
        spacing: 1

        // Keep the newest line in view without stealing scroll from a user
        // who has deliberately scrolled back.
        property bool pinned: true
        onCountChanged: if (pinned) positionViewAtEnd()
        onMovementEnded: pinned = atYEnd

        delegate: Text {
            width: lines.width
            text: modelData
            font.family: Theme.monoFamily
            font.pixelSize: Theme.sizeMicro
            elide: Text.ElideRight
            color: {
                if (modelData.startsWith(">")) return Theme.blue
                if (modelData.startsWith("<")) return Theme.inkMuted
                if (modelData.startsWith("!")) return Theme.purple
                return Theme.green
            }
        }
    }
}
