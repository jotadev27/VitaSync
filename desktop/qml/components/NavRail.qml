import QtQuick
import VitaSync
import VitaSync.Ui

Rectangle {
    id: root
    property int currentIndex: 0
    property int transferBadge: 0
    property int dropBadge: 0
    property int alertBadge: 0
    signal navigate(int index)
    signal faqRequested()
    width: Theme.railWidth
    color: Theme.surface

    Rectangle {
        anchors.right: parent.right
        width: 1
        height: parent.height
        color: Theme.line
    }
    Row {
        id: brand
        anchors.left: parent.left
        anchors.leftMargin: 22
        anchors.top: parent.top
        anchors.topMargin: 25
        spacing: 10
        Image {
            source: "qrc:/qt/qml/VitaSync/Ui/brand/system.svg"
            width: 32
            height: 32
            sourceSize: Qt.size(64, 64)
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            text: "VitaSync"
            color: Theme.ink
            font.family: Theme.sansFamily
            font.pixelSize: 20
            font.weight: Font.DemiBold
            anchors.verticalCenter: parent.verticalCenter
        }
    }
    Column {
        anchors.top: brand.bottom
        anchors.topMargin: 36
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        spacing: 4
        Repeater {
            model: [
                { icon: "vita", label: "Connection", badge: 0 },
                { icon: "upload", label: "Send files", badge: root.dropBadge },
                { icon: "folder", label: "Files on Vita", badge: 0 },
                { icon: "queue", label: "Transfers", badge: root.transferBadge }
            ]
            Rectangle {
                id: entry
                readonly property bool active: root.currentIndex === index
                width: parent.width
                height: 44
                radius: Theme.radius
                color: active ? Theme.raised : (hover.hovered ? Theme.surfaceAlt : "transparent")
                Row {
                    anchors.left: parent.left
                    anchors.leftMargin: 14
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 12
                    Icon {
                        name: modelData.icon
                        size: 19
                        color: entry.active ? Theme.ink : Theme.inkMuted
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        text: modelData.label
                        color: entry.active ? Theme.ink : Theme.inkMuted
                        font.family: Theme.sansFamily
                        font.pixelSize: Theme.sizeBody
                        font.weight: entry.active ? Font.DemiBold : Font.Normal
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                Text {
                    visible: modelData.badge > 0 && !(index === 3 && root.alertBadge > 0)
                    text: modelData.badge
                    anchors.right: parent.right
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    color: Theme.inkMuted
                    font.family: Theme.monoFamily
                    font.pixelSize: Theme.sizeSmall
                }
                Icon {
                    visible: index === 3 && root.alertBadge > 0
                    anchors.right: parent.right
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    name: "alert"
                    size: 16
                    color: Theme.purple
                }
                HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: root.navigate(index) }
            }
        }
    }
    Column {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 22
        spacing: 14
        AppButton {
            text: "VitaShell help"
            iconName: "bulb"
            variant: "quiet"
            onClicked: root.faqRequested()
        }
        Text {
            text: "v" + Device.appVersion.replace(/\.0$/, "") + "  ·  jotadev27"
            color: Theme.inkFaint
            font.family: Theme.sansFamily
            font.pixelSize: Theme.sizeMicro
        }
    }
}
