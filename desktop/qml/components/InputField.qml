import QtQuick
import VitaSync.Ui

// The one text input in the app.
//
// Fixed height, fixed padding, label above rather than inside, and a validity
// rail on the left edge. Because every field is this component, the boxes line
// up across every screen without anyone having to check.
Item {
    id: root

    property string label: ""
    property string text: ""
    property string placeholder: ""
    /// One short line under the field. A caption, not an explanation -- the
    /// lightbulb is where anything longer belongs.
    property string hint: ""
    property bool valid: true
    property bool showValidity: false
    property bool mono: true
    property int maximumLength: 255
    property var validator: null
    property alias inputMethodHints: field.inputMethodHints
    /// The actual text box's bottom edge, not the component's -- a hint
    /// caption extends below the box, so a sibling (a "Browse" button
    /// anchored alongside a field) needs this, not `bottom`, to land level
    /// with the box regardless of whether a hint is present or how long it is.
    property alias controlBottom: box.bottom

    signal accepted()
    signal edited(string value)

    implicitHeight: (label.length > 0 ? 18 : 0) + Theme.controlHeight
                    + (hint.length > 0 ? caption2.implicitHeight + 5 : 0)
    implicitWidth: 200

    Text {
        id: caption
        visible: root.label.length > 0
        text: root.label
        font.family: Theme.sansFamily
        font.pixelSize: Theme.sizeMicro
        font.letterSpacing: 1.1
        font.weight: Font.DemiBold
        color: field.activeFocus ? Theme.blue : Theme.inkFaint
        Behavior on color { ColorAnimation { duration: Theme.fast } }
    }

    Text {
        id: caption2
        visible: root.hint.length > 0
        anchors.bottom: parent.bottom
        width: parent.width
        text: root.hint
        font.family: Theme.sansFamily
        font.pixelSize: Theme.sizeMicro
        color: Theme.inkFaint
        elide: Text.ElideRight
    }

    Rectangle {
        id: box
        anchors.bottom: root.hint.length > 0 ? caption2.top : parent.bottom
        anchors.bottomMargin: root.hint.length > 0 ? 5 : 0
        width: parent.width
        height: Theme.controlHeight
        radius: Theme.radius
        color: Theme.surface
        border.width: Theme.border
        border.color: {
            if (root.showValidity && root.text.length > 0)
                return root.valid ? Theme.greenDim : Theme.purple
            return field.activeFocus ? Theme.blue : Theme.line
        }

        Behavior on border.color { ColorAnimation { duration: Theme.fast } }

        // Left rail: neutral until there is something to judge, then a solid
        // green bar or a notched purple one. It is the only validity signal --
        // no error sentence anywhere in the form.
        StatusRail {
            height: parent.height - 2
            anchors.left: parent.left
            anchors.leftMargin: 1
            anchors.verticalCenter: parent.verticalCenter
            visible: root.showValidity && root.text.length > 0
            tone: root.valid ? "ok" : "error"
        }

        // And a mark in the corner, so validity is not carried by the rail
        // alone at small sizes.
        Icon {
            anchors.right: parent.right
            anchors.rightMargin: 9
            anchors.verticalCenter: parent.verticalCenter
            visible: root.showValidity && root.text.length > 0 && !root.valid
            name: "alert"
            size: 14
            weight: 2.1
            color: Theme.purple
        }

        TextInput {
            id: field
            anchors.fill: parent
            anchors.leftMargin: Theme.pad
            anchors.rightMargin: Theme.pad
            verticalAlignment: TextInput.AlignVCenter
            clip: true

            text: root.text
            maximumLength: root.maximumLength
            validator: root.validator
            font.family: root.mono ? Theme.monoFamily : Theme.sansFamily
            font.pixelSize: Theme.sizeBody
            color: Theme.ink
            selectionColor: Theme.blueDim
            selectedTextColor: Theme.ink
            selectByMouse: true

            onTextEdited: {
                root.text = text
                root.edited(text)
            }
            onAccepted: root.accepted()

            Text {
                anchors.verticalCenter: parent.verticalCenter
                visible: field.text.length === 0 && !field.activeFocus
                text: root.placeholder
                font: field.font
                color: Theme.inkFaint
            }
        }
    }
}
