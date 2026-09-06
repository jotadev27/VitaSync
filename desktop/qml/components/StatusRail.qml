import QtQuick
import VitaSync.Ui

// The coloured edge that runs down the side of a card or a field.
//
// It carries state twice over. Colour says which state, and texture says
// whether that state wants attention: an ordinary rail is one solid bar, while
// an alert rail is broken into hard-edged notches. The two read apart in a
// screenshot, in greyscale, and for anyone who does not separate green from
// purple by hue.
Item {
    id: root

    property string tone: "info"
    property color color: Theme.toneColor(tone)
    property bool alert: Theme.toneIsAlert(tone)

    implicitWidth: alert ? 4 : 2

    // Solid: an ordinary state.
    Rectangle {
        anchors.fill: parent
        visible: !root.alert
        color: root.color
    }

    // Notched: something to look at. The gaps are deliberately coarse so the
    // texture survives at the sizes this is actually drawn at.
    Column {
        anchors.fill: parent
        visible: root.alert
        spacing: 3

        Repeater {
            model: Math.max(1, Math.floor(root.height / 9))

            Rectangle {
                width: root.width
                height: 6
                color: root.color
            }
        }
    }
}
