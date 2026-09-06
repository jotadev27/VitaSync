import QtQuick
import VitaSync.Ui

// What a screen shows when it has nothing yet: one mark, two or three words.
Column {
    id: root

    property string iconName: "package"
    property string text: ""
    property color accent: Theme.inkFaint

    spacing: 12

    Icon {
        name: root.iconName
        size: 30
        weight: 1.3
        color: root.accent
        opacity: 0.55
        anchors.horizontalCenter: parent.horizontalCenter
    }

    Text {
        text: root.text
        font.family: Theme.sansFamily
        font.pixelSize: Theme.sizeSmall
        font.letterSpacing: 0.8
        color: Theme.inkFaint
        anchors.horizontalCenter: parent.horizontalCenter
    }
}
