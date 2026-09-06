import QtQuick
import VitaSync.Ui

// Confirmation for anything that cannot be undone. Deliberately terse: the
// action, what it touches, and two buttons.
Item {
    id: root

    property bool open: false
    property string action: ""
    property string subject: ""
    property string iconName: "alert"

    /// Optional single field, for "new folder" and "rename".
    property bool inputVisible: false
    property string inputLabel: "NAME"
    property alias inputText: nameField.text

    signal confirmed()

    /// Opens the sheet, seeding the field when there is one.
    function show(seed) {
        if (inputVisible)
            nameField.text = seed !== undefined ? seed : ""
        open = true
    }

    anchors.fill: parent
    visible: open || card.opacity > 0

    Rectangle {
        anchors.fill: parent
        color: Theme.base
        opacity: root.open ? 0.75 : 0
        Behavior on opacity { NumberAnimation { duration: Theme.fast } }
        TapHandler { onTapped: root.open = false }
    }

    Rectangle {
        id: card
        anchors.centerIn: parent
        width: Math.min(380, root.width - 40)
        height: layout.implicitHeight + 2 * Theme.gapLarge
        radius: Theme.radius
        color: Theme.surface
        border.width: Theme.border
        border.color: Theme.purple
        opacity: root.open ? 1 : 0
        scale: root.open ? 1 : 0.97

        Behavior on opacity { NumberAnimation { duration: Theme.fast } }
        Behavior on scale { NumberAnimation { duration: Theme.fast } }

        Column {
            id: layout
            anchors.centerIn: parent
            width: parent.width - 2 * Theme.gapLarge
            spacing: Theme.gap

            Row {
                spacing: 10

                Icon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: root.iconName
                    size: 18
                    color: Theme.purple
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.action
                    font.family: Theme.sansFamily
                    font.pixelSize: Theme.sizeTitle
                    font.weight: Font.DemiBold
                    color: Theme.ink
                }
            }

            InputField {
                visible: root.inputVisible
                width: parent.width
                label: root.inputLabel
                mono: false
                id: nameField
            }

            Text {
                visible: !root.inputVisible
                width: parent.width
                text: root.subject
                font.family: Theme.monoFamily
                font.pixelSize: Theme.sizeSmall
                color: Theme.inkMuted
                wrapMode: Text.Wrap
                maximumLineCount: 4
                elide: Text.ElideRight
            }

            Row {
                anchors.right: parent.right
                spacing: Theme.gap

                AppButton {
                    text: "Cancel"
                    variant: "secondary"
                    onClicked: root.open = false
                }

                AppButton {
                    text: "Confirm"
                    variant: "primary"
                    dangerous: true
                    onClicked: {
                        root.open = false
                        root.confirmed()
                    }
                }
            }
        }
    }
}
