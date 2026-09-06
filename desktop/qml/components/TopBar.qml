import QtQuick
import VitaSync
import VitaSync.Ui

// Identity on the left, live connection state on the right. The status is a
// dot, an address and at most three words -- never a sentence.
Rectangle {
    id: root

    signal detailsRequested()
    property bool detailsOpen: false

    height: 52
    color: Theme.surface

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: Theme.border
        color: Theme.line
    }

    Row {
        anchors.left: parent.left
        anchors.leftMargin: Theme.gapLarge
        anchors.verticalCenter: parent.verticalCenter
        spacing: 10

        Text {
            anchors.verticalCenter: parent.verticalCenter
            font.family: Theme.sansFamily
            font.pixelSize: Theme.sizeTitle
            font.weight: Font.Bold
            font.letterSpacing: 2.2
            textFormat: Text.StyledText
            // The wordmark's own colour split: cyan VITA, pale SYNC.
            text: "<font color='" + Theme.blue + "'>VITA</font>"
                  + "<font color='" + Theme.ink + "'>SYNC</font>"
        }
    }

    Row {
        anchors.right: parent.right
        anchors.rightMargin: Theme.gapLarge
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.gap

        Row {
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8

            StatusDot {
                anchors.verticalCenter: parent.verticalCenter
                tone: Device.statusTone
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                // USB is a mounted folder, never a socket, so this names the
                // volume instead of an address that was never dialled.
                text: Device.connected ? Device.connectionSummary : Device.statusText
                font.family: Theme.monoFamily
                font.pixelSize: Theme.sizeSmall
                color: Device.connected ? Theme.ink : Theme.inkMuted
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                visible: Device.connected
                text: Device.connectionDetail
                font.family: Theme.monoFamily
                font.pixelSize: Theme.sizeSmall
                color: Theme.inkFaint
            }
        }

        Chip {
            anchors.verticalCenter: parent.verticalCenter
            visible: Device.companionAvailable
            text: "AWAKE"
            iconName: "shield"
            accent: Theme.green
            selected: Device.keepAwake
            onClicked: Device.keepAwake = !Device.keepAwake
        }

        AppButton {
            anchors.verticalCenter: parent.verticalCenter
            variant: "quiet"
            iconName: "queue"
            accent: Theme.purple
            onClicked: root.detailsRequested()
        }
    }
}
