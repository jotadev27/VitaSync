import QtQuick
import VitaSync.Ui

// A screen's only heading: a short accent-coloured word with a hairline that
// runs to the edge. Does the work a paragraph of introduction would.
Item {
    id: root

    property string text: ""
    property color accent: Theme.blue
    property string trailing: ""

    implicitHeight: 18
    implicitWidth: 200

    // The tick is what makes this read as a panel legend rather than a
    // heading: the accent is a mark on the edge, not a coloured word floating
    // on its own.
    Rectangle {
        id: tick
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        width: 2
        height: parent.height - 4
        color: root.accent
    }

    Text {
        id: caption
        anchors.left: tick.right
        anchors.leftMargin: Theme.space2
        anchors.verticalCenter: parent.verticalCenter
        text: root.text
        font.family: Theme.sansFamily
        font.pixelSize: Theme.sizeSmall
        font.weight: Font.Bold
        font.letterSpacing: 1.6
        color: Theme.ink
    }

    Rectangle {
        anchors.left: caption.right
        anchors.leftMargin: Theme.space3
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
