import QtQuick
import QtQuick.Controls
import VitaSync
import VitaSync.Ui

// Identity on the left, live connection state on the right. The status is a
// dot, an address and at most three words -- never a sentence.
Rectangle {
    id: root

    signal detailsRequested()
    property bool detailsOpen: false
    property string pageTitle: "Connection"

    height: 64
    color: Theme.base

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
            font.weight: Font.DemiBold
            text: root.pageTitle
            color: Theme.ink
        }
    }

    Row {
        anchors.right: parent.right
        anchors.rightMargin: Theme.gapLarge
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.gap

        Row {
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2
            Repeater {
                model: [{dark: false, symbol: "sun", label: "Light theme"},
                        {dark: true, symbol: "moon", label: "Dark theme"}]
                ToolButton {
                    id: themeChoice
                    width: 36
                    height: 32
                    text: modelData.label
                    checkable: true
                    autoExclusive: true
                    checked: Device.darkMode === modelData.dark
                    Accessible.name: modelData.label
                    ToolTip.visible: hovered
                    ToolTip.text: modelData.label
                    ToolTip.delay: 450
                    onClicked: Device.darkMode = modelData.dark
                    contentItem: Icon {
                        name: modelData.symbol
                        size: 18
                        color: themeChoice.checked ? Theme.blue : Theme.inkMuted
                    }
                    background: Rectangle {
                        color: themeChoice.down ? Theme.surfaceAlt : (themeChoice.hovered ? Theme.surface : "transparent")
                        radius: Theme.radius
                        border.width: themeChoice.visualFocus ? 2 : 0
                        border.color: Theme.blue
                        Rectangle {
                            anchors.bottom: parent.bottom
                            anchors.bottomMargin: 1
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: 14
                            height: 2
                            radius: 1
                            color: Theme.blue
                            visible: themeChoice.checked
                        }
                    }
                }
            }
        }

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
                font.family: Theme.sansFamily
                font.pixelSize: Theme.sizeSmall
                color: Device.connected ? Theme.ink : Theme.inkMuted
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                visible: Device.connected
                text: Device.connectionDetail
                font.family: Theme.sansFamily
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
            accessibleName: "Show connection details"
            accent: Theme.inkMuted
            onClicked: root.detailsRequested()
        }
    }
}
