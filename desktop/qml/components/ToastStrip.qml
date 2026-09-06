import QtQuick
import VitaSync.Ui

// Transient feedback, docked to the bottom edge. One line, an accent rail and
// a mark; it never blocks anything and never grows into a paragraph.
Item {
    id: root

    property string message: ""
    property string tone: "info"

    function show(text, toneName) {
        message = text
        tone = toneName
        plate.opacity = 1
        life.restart()
    }

    implicitHeight: 30
    visible: plate.opacity > 0

    Rectangle {
        id: plate
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        width: Math.min(row.implicitWidth + 26, root.width)
        height: 30
        radius: Theme.radius
        color: Theme.surfaceAlt
        border.width: Theme.border
        border.color: Theme.toneColor(root.tone)
        opacity: 0
        clip: true

        Behavior on opacity { NumberAnimation { duration: Theme.normal } }

        StatusRail {
            height: parent.height
            tone: root.tone
        }

        Row {
            id: row
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: 13
            spacing: 8

            Icon {
                anchors.verticalCenter: parent.verticalCenter
                size: 14
                weight: Theme.toneIsAlert(root.tone) ? 2.1 : 1.6
                color: Theme.toneColor(root.tone)
                name: Theme.toneIcon(root.tone)
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: root.message
                font.family: Theme.sansFamily
                font.pixelSize: Theme.sizeSmall
                color: Theme.ink
                elide: Text.ElideRight
                width: Math.min(implicitWidth, root.width - 60)
            }
        }
    }

    Timer {
        id: life
        // Alerts stay up noticeably longer, which is a third, non-colour cue.
        interval: Theme.toneIsAlert(root.tone) ? 6500 : 3200
        onTriggered: plate.opacity = 0
    }
}
