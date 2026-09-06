import QtQuick
import VitaSync.Ui

// A screen's only heading: a short accent-coloured word with a hairline that
// runs to the edge. Does the work a paragraph of introduction would.
Item {
    id: root

    property string text: ""
    property color accent: Theme.blue
    property string trailing: ""

    implicitHeight: 16
    implicitWidth: 200

    Text {
        id: caption
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        text: root.text
        font.family: Theme.sansFamily
        font.pixelSize: Theme.sizeMicro
        font.weight: Font.Bold
        font.letterSpacing: 1.4
        color: root.accent
    }

    Rectangle {
        anchors.left: caption.right
        anchors.leftMargin: 10
        anchors.right: trailingText.visible ? trailingText.left : parent.right
        anchors.rightMargin: trailingText.visible ? 10 : 0
        anchors.verticalCenter: parent.verticalCenter
        height: 1
        color: Theme.line
    }

    Text {
        id: trailingText
        visible: root.trailing.length > 0
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        text: root.trailing
        font.family: Theme.monoFamily
        font.pixelSize: Theme.sizeMicro
        color: Theme.inkFaint
    }
}
