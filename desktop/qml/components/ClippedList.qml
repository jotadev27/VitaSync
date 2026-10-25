import QtQuick
import VitaSync.Ui

// Keep scrolling content inside the viewport without an extra render texture.
Item {
    id: root

    default property alias content: holder.data
    property alias view: holder

    clip: true

    Rectangle {
        anchors.fill: parent
        color: Theme.base
    }

    Item {
        id: holder
        anchors.fill: parent
    }
}
