import QtQuick
import VitaSync
import VitaSync.Ui
import "../components"

// Browse: the remote tree, with selection as the primary interaction.
//
// One overlay handles every pointer gesture over the list -- click, ctrl-click,
// shift-range, rubber-band sweep and drag-to-move -- so all five agree on the
// same selection and the delegates stay pure presentation.
Item {
    id: root

    readonly property int rowPitch: Theme.rowHeight + 2
    property int anchorRow: -1
    property int hoverRow: -1

    property bool banding: false
    property real bandStartX: 0
    property real bandStartY: 0
    property real bandX: 0
    property real bandY: 0

    property bool moving: false
    property string moveTarget: ""

    /// Touch adaptation. A finger drag has to scroll the list, so the
    /// rubber-band sweep is a mouse gesture only; on touch a long press turns
    /// on a tap-to-toggle mode instead, which is the same selection set seen
    /// through a different gesture.
    property bool selectionMode: false

    function rowAt(localY) {
        var absolute = localY + list.contentY
        if (absolute < 0)
            return -1
        var row = Math.floor(absolute / rowPitch)
        return row >= 0 && row < list.count ? row : -1
    }

    ConfirmSheet {
        id: deleteConfirm
        action: "Delete permanently"
        subject: Device.browser.selectedNames().join("\n")
        iconName: "trash"
        onConfirmed: Device.deleteSelection()
    }

    Row {
        anchors.fill: parent
        spacing: 0

        // --- quick locations -------------------------------------------
        Rectangle {
            id: sidebar
            width: 168
            height: parent.height
            color: Theme.surface

            Rectangle {
                anchors.right: parent.right
                width: Theme.border
                height: parent.height
                color: Theme.line
            }

            Column {
                anchors.fill: parent
                anchors.margins: Theme.gap
                spacing: 2

                SectionLabel {
                    width: parent.width - Theme.gap
                    text: Device.mount.toUpperCase()
                    accent: Theme.blue
                }

                Item { width: 1; height: 6 }

                Repeater {
                    model: Device.quickLocations

                    Rectangle {
                        width: parent.width
                        height: 30
                        radius: Theme.radius
                        // A location lights up green while a move is hovering
                        // over it: the sidebar is a drop target too.
                        color: root.moving && root.moveTarget === modelData.path
                               ? Theme.greenWash
                               : (modelData.current ? Theme.blueWash
                                                    : (locHover.hovered ? Theme.surfaceAlt : "transparent"))
                        border.width: root.moving && root.moveTarget === modelData.path ? Theme.border : 0
                        border.color: Theme.green

                        Behavior on color { ColorAnimation { duration: Theme.fast } }

                        Row {
                            anchors.left: parent.left
                            anchors.leftMargin: 9
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 9

                            Icon {
                                anchors.verticalCenter: parent.verticalCenter
                                name: modelData.iconName
                                size: 15
                                color: modelData.current ? Theme.blue
                                                         : (locHover.hovered ? Theme.ink : Theme.inkFaint)
                            }

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.label
                                font.family: Theme.sansFamily
                                font.pixelSize: Theme.sizeSmall
                                color: modelData.current ? Theme.blue
                                                         : (locHover.hovered ? Theme.ink : Theme.inkMuted)
                            }
                        }

                        HoverHandler {
                            id: locHover
                            cursorShape: Qt.PointingHandCursor
                            onHoveredChanged: {
                                if (root.moving)
                                    root.moveTarget = hovered ? modelData.path : ""
                            }
                        }
                        TapHandler { onTapped: Device.navigateTo(modelData.path) }
                    }
                }
            }
        }

        // --- listing -----------------------------------------------------
        Item {
            width: parent.width - sidebar.width
            height: parent.height

            // Breadcrumb + navigation
            Item {
                id: crumbBar
                width: parent.width
                height: 44

                Row {
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.gap
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 4

                    AppButton {
                        anchors.verticalCenter: parent.verticalCenter
                        variant: "quiet"
                        iconName: "up"
                        enabled: Device.connected
                        onClicked: Device.navigateUp()
                    }

                    AppButton {
                        anchors.verticalCenter: parent.verticalCenter
                        variant: "quiet"
                        iconName: "refresh"
                        enabled: Device.connected
                        busy: Device.listing
                        onClicked: Device.refresh()
                    }

                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: Theme.border
                        height: 18
                        color: Theme.line
                    }

                    Repeater {
                        model: Device.breadcrumb

                        Row {
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 4

                            Icon {
                                visible: index > 0
                                anchors.verticalCenter: parent.verticalCenter
                                name: "chevron"
                                size: 10
                                color: Theme.inkFaint
                            }

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.label
                                font.family: Theme.monoFamily
                                font.pixelSize: Theme.sizeSmall
                                color: index === Device.breadcrumb.length - 1
                                       ? Theme.ink
                                       : (crumbHover.hovered ? Theme.blue : Theme.inkMuted)
                                leftPadding: 2
                                rightPadding: 2

                                HoverHandler { id: crumbHover; cursorShape: Qt.PointingHandCursor }
                                TapHandler { onTapped: Device.navigateTo(modelData.path) }
                            }
                        }
                    }
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: Theme.border
                    color: Theme.line
                }
            }

            // Selection summary + batch actions
            Item {
                id: toolbar
                anchors.top: crumbBar.bottom
                width: parent.width
                height: 44

                Row {
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.gap
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 8

                    Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "select"
                        size: 15
                        color: Device.browser.selectionCount > 0 ? Theme.green : Theme.inkFaint
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: Device.browser.selectionCount > 0
                              ? Device.browser.selectionSummary
                              : Device.browser.count + " items"
                        font.family: Theme.monoFamily
                        font.pixelSize: Theme.sizeSmall
                        color: Device.browser.selectionCount > 0 ? Theme.green : Theme.inkFaint
                    }
                }

                Row {
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.gap
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 4

                    Chip {
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.selectionMode ? "DONE" : "SELECT"
                        iconName: "select"
                        accent: Theme.green
                        selected: root.selectionMode
                        onClicked: {
                            root.selectionMode = !root.selectionMode
                            if (!root.selectionMode)
                                Device.browser.clearSelection()
                        }
                    }

                    AppButton {
                        variant: "quiet"
                        iconName: "newfolder"
                        enabled: Device.connected
                        onClicked: newFolder.show("New folder")
                    }

                    AppButton {
                        variant: "quiet"
                        iconName: "pencil"
                        enabled: Device.browser.selectionCount === 1
                        onClicked: renameSheet.show(Device.browser.selectedNames().join(""))
                    }

                    AppButton {
                        variant: "quiet"
                        iconName: "trash"
                        accent: Theme.purple
                        enabled: Device.browser.selectionCount > 0
                        onClicked: deleteConfirm.open = true
                    }

                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: Theme.border
                        height: 18
                        color: Theme.line
                    }

                    AppButton {
                        text: "Download"
                        iconName: "download"
                        variant: Device.browser.selectionCount > 0 ? "primary" : "secondary"
                        accent: Theme.green
                        enabled: Device.browser.selectionCount > 0
                        onClicked: Device.downloadSelection()
                    }
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: Theme.border
                    color: Theme.line
                }
            }

            // The listing itself
            ClippedList {
                id: listFrame
                anchors.top: toolbar.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: Theme.gap

            ListView {
                id: list
                anchors.fill: parent
                clip: true
                spacing: 2
                model: Device.browser
                interactive: !root.banding && !root.moving
                boundsBehavior: Flickable.StopAtBounds

                delegate: Rectangle {
                    width: ListView.view.width
                    height: Theme.rowHeight
                    radius: Theme.radius

                    readonly property bool isDropTarget: root.moving && isDirectory
                                                         && root.moveTarget === path

                    color: {
                        if (isDropTarget) return Theme.greenWash
                        if (selected) return Theme.blueWash
                        if (root.hoverRow === index) return Theme.surfaceAlt
                        return "transparent"
                    }
                    border.width: selected || isDropTarget ? Theme.border : 0
                    border.color: isDropTarget ? Theme.green : Theme.blueDim

                    Behavior on color { ColorAnimation { duration: 80 } }

                    Row {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.pad
                        anchors.rightMargin: Theme.pad
                        spacing: Theme.gap

                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: root.selectionMode
                            width: 15
                            height: 15
                            radius: 2
                            color: selected ? Theme.blue : "transparent"
                            border.width: Theme.border
                            border.color: selected ? Theme.blue : Theme.lineStrong

                            Icon {
                                anchors.centerIn: parent
                                visible: selected
                                name: "check"
                                size: 11
                                weight: 2.4
                                color: Theme.base
                            }
                        }

                        Icon {
                            anchors.verticalCenter: parent.verticalCenter
                            name: iconName
                            size: 16
                            color: {
                                if (isProtected) return Theme.purple
                                if (selected) return Theme.blue
                                if (isDirectory) return Theme.blue
                                return Theme.inkFaint
                            }
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - 360
                            text: name
                            font.family: isDirectory ? Theme.sansFamily : Theme.monoFamily
                            font.pixelSize: Theme.sizeBody
                            font.weight: isDirectory ? Font.Medium : Font.Normal
                            color: selected ? Theme.ink : (isDirectory ? Theme.ink : Theme.inkMuted)
                            elide: Text.ElideMiddle
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 90
                            horizontalAlignment: Text.AlignRight
                            text: sizeText
                            font.family: Theme.monoFamily
                            font.pixelSize: Theme.sizeSmall
                            color: Theme.inkFaint
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 150
                            horizontalAlignment: Text.AlignRight
                            text: modifiedText
                            font.family: Theme.monoFamily
                            font.pixelSize: Theme.sizeMicro
                            color: Theme.inkFaint
                            elide: Text.ElideRight
                        }
                    }

                    // System folders carry a notched edge rather than a warning
                    // label; the confirm sheet does the talking if it matters.
                    StatusRail {
                        visible: isProtected
                        height: parent.height
                        tone: "warn"
                    }
                }

                EmptyState {
                    anchors.centerIn: parent
                    visible: list.count === 0 && !Device.listing
                    iconName: Device.connected ? "folder" : "link"
                    text: Device.connected ? "EMPTY FOLDER" : "NOT CONNECTED"
                }
            }
            }

            // One overlay owns every gesture over the list.
            MouseArea {
                id: gestures
                anchors.fill: listFrame
                acceptedButtons: Qt.LeftButton
                hoverEnabled: true
                propagateComposedEvents: false
                cursorShape: root.moving ? Qt.DragMoveCursor : Qt.ArrowCursor

                property real pressX: 0
                property real pressY: 0
                property int pressedRow: -1
                property bool pressWasSelected: false
                property bool decided: false

                onPositionChanged: function (mouse) {
                    root.hoverRow = root.rowAt(mouse.y)

                    if (!pressed)
                        return

                    if (!decided) {
                        // Past the threshold, a press on a selected row starts
                        // a move and a press on empty space starts a sweep.
                        if (Math.abs(mouse.x - pressX) + Math.abs(mouse.y - pressY) < 6)
                            return
                        decided = true
                        if (pressedRow >= 0 && pressWasSelected) {
                            root.moving = true
                        } else {
                            root.banding = true
                            root.bandStartX = pressX
                            root.bandStartY = pressY
                        }
                    }

                    if (root.banding) {
                        root.bandX = mouse.x
                        root.bandY = mouse.y
                        var a = root.rowAt(Math.min(root.bandStartY, root.bandY))
                        var b = root.rowAt(Math.max(root.bandStartY, root.bandY))
                        if (a < 0) a = 0
                        if (b < 0) b = list.count - 1
                        Device.browser.clearSelection()
                        if (list.count > 0)
                            Device.browser.selectRange(a, b, true)
                    } else if (root.moving) {
                        var overRow = root.rowAt(mouse.y)
                        var item = overRow >= 0 ? Device.browser.itemAt(overRow) : null
                        root.moveTarget = (item && item.isDirectory && !Device.browser.isSelected(overRow))
                                          ? item.path : ""
                    }
                }

                onPressAndHold: function (mouse) {
                    const row = root.rowAt(mouse.y)
                    if (row < 0)
                        return
                    root.selectionMode = true
                    Device.browser.setSelected(row, true)
                    root.anchorRow = row
                    decided = true          // do not also start a sweep or a move
                }

                onPressed: function (mouse) {
                    pressX = mouse.x
                    pressY = mouse.y
                    decided = false
                    pressedRow = root.rowAt(mouse.y)
                    pressWasSelected = pressedRow >= 0 && Device.browser.isSelected(pressedRow)

                    if (root.selectionMode) {
                        if (pressedRow >= 0) {
                            Device.browser.toggleSelected(pressedRow)
                            root.anchorRow = pressedRow
                        }
                        decided = true
                        return
                    }

                    if (pressedRow < 0) {
                        if (!(mouse.modifiers & Qt.ControlModifier))
                            Device.browser.clearSelection()
                        return
                    }

                    if (mouse.modifiers & Qt.ShiftModifier) {
                        if (root.anchorRow < 0)
                            root.anchorRow = pressedRow
                        Device.browser.clearSelection()
                        Device.browser.selectRange(root.anchorRow, pressedRow, true)
                    } else if (mouse.modifiers & Qt.ControlModifier) {
                        Device.browser.toggleSelected(pressedRow)
                        root.anchorRow = pressedRow
                    } else if (!pressWasSelected) {
                        Device.browser.selectOnly(pressedRow)
                        root.anchorRow = pressedRow
                    }
                }

                onReleased: function (mouse) {
                    if (root.selectionMode) {
                        decided = false
                        return
                    }
                    if (root.moving) {
                        if (root.moveTarget.length > 0)
                            Device.moveSelectionTo(root.moveTarget)
                        root.moving = false
                        root.moveTarget = ""
                    } else if (root.banding) {
                        root.banding = false
                    } else if (pressedRow >= 0 && !decided
                               && !(mouse.modifiers & (Qt.ControlModifier | Qt.ShiftModifier))) {
                        // A plain click on an already-selected row collapses
                        // the selection back to just that row.
                        Device.browser.selectOnly(pressedRow)
                        root.anchorRow = pressedRow
                    }
                    decided = false
                }

                onDoubleClicked: function (mouse) {
                    if (root.selectionMode)
                        return
                    var row = root.rowAt(mouse.y)
                    if (row >= 0)
                        Device.openRow(row)
                }

                onExited: root.hoverRow = -1
            }

            // The sweep rectangle.
            Rectangle {
                visible: root.banding
                x: listFrame.x + Math.min(root.bandStartX, root.bandX)
                y: listFrame.y + Math.min(root.bandStartY, root.bandY)
                width: Math.abs(root.bandX - root.bandStartX)
                height: Math.abs(root.bandY - root.bandStartY)
                color: Qt.rgba(Theme.blue.r, Theme.blue.g, Theme.blue.b, 0.10)
                border.width: 1
                border.color: Theme.blue
            }

            // The move badge follows the cursor while dragging rows.
            Rectangle {
                visible: root.moving
                x: listFrame.x + gestures.mouseX + 14
                y: listFrame.y + gestures.mouseY + 14
                width: moveText.implicitWidth + 18
                height: 24
                radius: Theme.radius
                color: Theme.surfaceAlt
                border.width: Theme.border
                border.color: root.moveTarget.length > 0 ? Theme.green : Theme.lineStrong

                Text {
                    id: moveText
                    anchors.centerIn: parent
                    text: root.moveTarget.length > 0
                          ? "→ " + root.moveTarget
                          : Device.browser.selectionCount + " selected"
                    font.family: Theme.monoFamily
                    font.pixelSize: Theme.sizeMicro
                    color: root.moveTarget.length > 0 ? Theme.green : Theme.inkMuted
                }
            }

            // Dropping files from the desktop uploads them into this folder.
            DropArea {
                anchors.fill: listFrame
                keys: ["text/uri-list"]

                onDropped: function (drop) {
                    if (!drop.hasUrls || !Device.connected) {
                        drop.accepted = false
                        return
                    }
                    var paths = []
                    for (var i = 0; i < drop.urls.length; ++i)
                        paths.push(drop.urls[i].toString())
                    Device.addDrops(paths)
                    drop.acceptProposedAction()
                }

                Rectangle {
                    anchors.fill: parent
                    visible: parent.containsDrag
                    color: Qt.rgba(Theme.green.r, Theme.green.g, Theme.green.b, 0.07)
                    border.width: 2
                    border.color: Theme.green
                    radius: Theme.radius
                }
            }
        }
    }

    // --- inline prompts ---------------------------------------------------

    ConfirmSheet {
        id: newFolder
        action: "New folder"
        subject: Device.currentPath
        iconName: "newfolder"
        inputVisible: true
        inputLabel: "NAME"
        onConfirmed: Device.createFolder(inputText)
    }

    ConfirmSheet {
        id: renameSheet
        action: "Rename"
        iconName: "pencil"
        inputVisible: true
        inputLabel: "NEW NAME"
        onConfirmed: {
            var paths = Device.browser.selectedPaths()
            if (paths.length === 1)
                Device.renameEntry(paths[0], inputText)
        }
    }
}
