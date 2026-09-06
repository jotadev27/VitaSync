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
    property bool valid: true
    property bool showValidity: false
    property bool mono: true
    property int maximumLength: 255
    property var validator: null
    property alias inputMethodHints: field.inputMethodHints

    /// Height of the label line above the box, or 0 when there is no label.
    readonly property int labelInset: label.length > 0 ? Theme.labelHeight : 0
    /// The text box's own top offset inside this component. A sibling that has
    /// to sit level with the box -- a "Browse" button beside a field -- binds
    /// its `y` to this. It is deliberately a number and not an anchor line:
    /// the box is a child of this item, and QML silently ignores an anchor
    /// aimed at anything that is not a sibling or a parent, which is exactly
    /// how these buttons drifted a label's height too high for so long.
    readonly property int controlY: labelInset

    signal accepted()
    signal edited(string value)

    implicitHeight: labelInset + Theme.controlHeight
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

    Rectangle {
        id: box
        y: root.labelInset
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
