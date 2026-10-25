import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import VitaSync
import VitaSync.Ui
import "../components"

Item {
    id: root
    property bool dragActive: false
    property var destinationOwner: null
    signal connectionRequested()

    function chooseDestination(folder) {
        if (destinationOwner)
            Device.drops.setDestination(destinationOwner.stagedIndex, "/" + Device.mount + "/" + folder)
    }

    Menu {
        id: destinationMenu
        width: 240
        x: 0
        y: parent ? parent.height : 0
        enter: null
        exit: null
        palette.text: Theme.ink
        palette.highlightedText: Theme.ink
        palette.highlight: Theme.surfaceAlt
        background: Rectangle { color: Theme.surface; border.color: Theme.line; radius: Theme.radius }
        MenuItem { text: "Data"; onTriggered: root.chooseDestination("data") }
        MenuItem { text: "Saves (user/00/savedata)"; onTriggered: root.chooseDestination("user/00/savedata") }
        MenuItem { text: "Downloads"; onTriggered: root.chooseDestination("downloads") }
        onClosed: {
            root.destinationOwner = null
            parent = root
        }
    }

    function kindIcon(label) {
        if (label === "Folder" || label === "Game folder") return "folder"
        if (label === "Archive") return "archive"
        if (label === "Android APK") return "android"
        if (label === "Photo") return "photo"
        if (label === "Video") return "video"
        if (label === "Music") return "music"
        if (label === "File") return "download"
        if (label.indexOf("Theme") >= 0) return "theme"
        return "package"
    }

    Column {
        id: heading
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 24
        spacing: 10
        Text {
            text: "Files to send"
            font.family: Theme.sansFamily
            font.pixelSize: Theme.sizeDisplay
            font.weight: Font.Medium
            color: Theme.ink
        }
        Text {
            width: parent.width
            text: "Drop files or folders to review them before sending."
            font.family: Theme.sansFamily
            font.pixelSize: Theme.sizeBody
            color: Theme.inkMuted
            wrapMode: Text.WordWrap
        }
        Rectangle {
            id: dropPlate
            width: parent.width
            height: Device.drops.count > 0 ? 64 : 156
            radius: 8
            color: root.dragActive ? Theme.blueWash : Theme.surface
            border.width: 1
            border.color: root.dragActive ? Theme.blue : Theme.lineStrong
            Behavior on color { ColorAnimation { duration: Theme.fast } }

            Column {
                anchors.centerIn: parent
                width: parent.width - 32
                spacing: 10
                visible: Device.drops.count === 0
                Row {
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 12
                    Icon {
                        name: "upload"
                        size: 24
                        color: Theme.blue
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        text: root.dragActive ? "Release to add your files" : "Drop files or folders here"
                        font.family: Theme.sansFamily
                        font.pixelSize: 17
                        font.weight: Font.Medium
                        color: Theme.ink
                    }
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "VPK, ZIP, RAR, photos, videos and more"
                    font.family: Theme.sansFamily
                    font.pixelSize: Theme.sizeSmall
                    color: Theme.inkMuted
                }
                Row {
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 8
                    AppButton { text: "Choose files"; iconName: "file"; onClicked: fileDialog.open() }
                    AppButton { text: "Choose folder"; iconName: "folder"; variant: "quiet"; onClicked: folderDialog.open() }
                }
            }
            Row {
                anchors.left: parent.left
                anchors.leftMargin: 16
                anchors.verticalCenter: parent.verticalCenter
                spacing: 12
                visible: Device.drops.count > 0
                AppButton { text: "Add files"; iconName: "upload"; onClicked: fileDialog.open() }
                AppButton { text: "Add folder"; iconName: "newfolder"; variant: "quiet"; onClicked: folderDialog.open() }
            }
        }
    }

    ClippedList {
        anchors.top: heading.bottom
        anchors.topMargin: 12
        anchors.left: heading.left
        anchors.right: heading.right
        anchors.bottom: actionBar.top
        anchors.bottomMargin: 12
        visible: Device.drops.count > 0
        ListView {
            id: stagedList
            anchors.fill: parent
            model: Device.drops
            spacing: 0
            reuseItems: true
            cacheBuffer: Math.max(0, height)
            boundsBehavior: Flickable.StopAtBounds
            pixelAligned: true
            onDraggingChanged: if (dragging) wheelScroll.stop()

            NumberAnimation {
                id: wheelScroll
                target: stagedList
                property: "contentY"
                duration: 100
                easing.type: Easing.OutCubic
            }
            WheelHandler {
                target: null
                onWheel: function(event) {
                    if (event.pixelDelta.y === 0 && event.angleDelta.y === 0) {
                        event.accepted = false
                        return
                    }
                    var lower = stagedList.originY
                    var upper = lower + Math.max(0, stagedList.contentHeight - stagedList.height)
                    var direct = event.pixelDelta.y !== 0
                    var start = !direct && wheelScroll.running ? wheelScroll.to : stagedList.contentY
                    var lines = Qt.styleHints.wheelScrollLines
                    var distance = direct ? event.pixelDelta.y
                        : event.angleDelta.y / 120 * (lines > 0 ? lines * 88 : stagedList.height * 0.9)
                    wheelScroll.stop()
                    stagedList.cancelFlick()
                    var end = Math.max(lower, Math.min(upper, start - distance))
                    if (direct) stagedList.contentY = end
                    else {
                        wheelScroll.from = stagedList.contentY
                        wheelScroll.to = end
                        wheelScroll.start()
                    }
                    event.accepted = true
                }
            }
            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
                visible: stagedList.contentHeight > stagedList.height
                onPressedChanged: if (pressed) wheelScroll.stop()
                contentItem: Rectangle {
                    implicitWidth: 5
                    radius: 2
                    color: Theme.lineStrong
                }
            }
            delegate: Rectangle {
                id: stagedRow
                property int stagedIndex: index
                property string stagedDestination: model.destination
                property bool automaticRouting: model.automaticRouting
                property string mediaArtwork: model.kindLabel === "Music"
                    ? "qrc:/qt/qml/VitaSync/Ui/filetypes/music.png"
                    : (model.kindLabel === "Video" ? "qrc:/qt/qml/VitaSync/Ui/filetypes/video.png" : "")
                width: ListView.view.width - 12
                height: Math.max(88, fileDetails.implicitHeight + 24)
                ListView.onPooled: {
                    if (root.destinationOwner === stagedRow) destinationMenu.close()
                    destinationControl.focus = false
                    removeButton.focus = false
                }
                color: rowHover.hovered ? Theme.surface : "transparent"
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.line }
                Row {
                    anchors.fill: parent
                    anchors.topMargin: 12
                    anchors.bottomMargin: 12
                    spacing: 14
                    Rectangle {
                        width: 48
                        height: 48
                        anchors.verticalCenter: parent.verticalCenter
                        radius: 6
                        color: "transparent"
                        clip: true
                        Image {
                            anchors.fill: parent
                            source: stagedRow.mediaArtwork.length ? stagedRow.mediaArtwork
                                : "image://filetypes/" + root.kindIcon(model.kindLabel)
                            sourceSize: Qt.size(144, 144)
                            fillMode: Image.PreserveAspectFit
                            asynchronous: false
                            smooth: true
                            mipmap: true
                            visible: stagedRow.mediaArtwork.length > 0 || preview.status !== Image.Ready
                        }
                        Image {
                            id: preview
                            anchors.fill: parent
                            source: stagedRow.mediaArtwork.length ? "" : model.coverSource
                            sourceSize: Qt.size(144, 144)
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                        }
                    }
                    Column {
                        id: fileDetails
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - 48 - 14 - 32 - 14
                        spacing: 4
                        Text {
                            width: parent.width
                            text: model.title
                            elide: Text.ElideMiddle
                            font.family: Theme.sansFamily
                            font.pixelSize: Theme.sizeBody
                            font.weight: Font.Medium
                            color: Theme.ink
                        }
                        Text {
                            width: parent.width
                            text: model.kindLabel + " · " + model.sizeText
                            elide: Text.ElideRight
                            font.family: Theme.sansFamily
                            font.pixelSize: Theme.sizeSmall
                            color: Theme.inkMuted
                        }
                        Item {
                            id: destinationControl
                            width: parent.width
                            height: 22
                            activeFocusOnTab: true
                            Accessible.role: Accessible.Button
                            Accessible.name: "Change destination for " + model.title
                            Accessible.onPressAction: openDestinationMenu()
                            Keys.onSpacePressed: openDestinationMenu()
                            Keys.onReturnPressed: openDestinationMenu()
                            function openDestinationMenu() {
                                forceActiveFocus()
                                root.destinationOwner = stagedRow
                                destinationMenu.parent = destinationControl
                                destinationMenu.open()
                            }
                            Text {
                                id: destinationLabel
                                anchors.verticalCenter: parent.verticalCenter
                                width: Math.min(implicitWidth, parent.width - 22)
                                text: stagedRow.automaticRouting ? "Automatic destination · file types" : "To " + stagedRow.stagedDestination
                                elide: Text.ElideMiddle
                                font.family: Theme.sansFamily
                                font.pixelSize: Theme.sizeSmall
                                color: Theme.blue
                                font.underline: destinationControl.activeFocus
                            }
                            Icon {
                                x: destinationLabel.width + 6
                                anchors.verticalCenter: parent.verticalCenter
                                name: "chevron"; rotation: 90; size: 12
                                color: Theme.inkMuted
                            }
                            HoverHandler { cursorShape: Qt.PointingHandCursor }
                            TapHandler {
                                gesturePolicy: TapHandler.ReleaseWithinBounds
                                onTapped: destinationControl.openDestinationMenu()
                            }
                        }
                        Text {
                            width: parent.width
                            text: model.error.length > 0 ? model.error : model.subtitle
                            visible: model.error.length > 0 || (model.subtitle.length > 0 && model.subtitle !== model.title)
                            maximumLineCount: 2
                            wrapMode: Text.WordWrap
                            elide: Text.ElideRight
                            font.family: Theme.sansFamily
                            font.pixelSize: Theme.sizeSmall
                            color: model.error.length > 0 ? Theme.purple : Theme.inkFaint
                        }
                    }
                    AppButton {
                        id: removeButton
                        anchors.verticalCenter: parent.verticalCenter
                        width: 32
                        height: 32
                        variant: "quiet"
                        iconName: "close"
                        accessibleName: "Remove " + model.title + " from the list"
                        onClicked: Device.drops.removeAt(index)
                    }
                }
                HoverHandler { id: rowHover; enabled: !stagedList.moving && !wheelScroll.running }
            }
        }
    }

    Rectangle {
        id: actionBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 72
        color: Theme.surface
        Rectangle { width: parent.width; height: 1; color: Theme.line }
        Column {
            anchors.left: parent.left
            anchors.leftMargin: 24
            anchors.verticalCenter: parent.verticalCenter
            spacing: 4
            Text {
                text: Device.drops.readyCount + (Device.drops.readyCount === 1 ? " item ready" : " items ready")
                font.family: Theme.sansFamily
                font.pixelSize: Theme.sizeBody
                font.weight: Font.Medium
                color: Theme.ink
            }
            Text {
                text: Device.drops.count > 0
                    ? Device.drops.totalSizeText + (Device.connected ? " in this batch" : " · Connect your Vita to start")
                    : "Add files, then press Start"
                font.family: Theme.sansFamily
                font.pixelSize: Theme.sizeSmall
                color: Theme.inkMuted
            }
        }
        Row {
            anchors.right: parent.right
            anchors.rightMargin: 24
            anchors.verticalCenter: parent.verticalCenter
            spacing: 10
            AppButton {
                text: "Clear"
                variant: "secondary"
                visible: Device.drops.count > 0
                onClicked: Device.clearDrops()
            }
            AppButton {
                text: "Start"
                accessibleName: Device.connected ? "Start transfers" : "Connect your Vita to start transfers"
                iconName: "play"
                variant: "primary"
                enabled: Device.drops.readyCount > 0
                onClicked: { if (Device.connected) Device.startStaged(); else root.connectionRequested() }
            }
        }
    }

    FileDialog {
        id: fileDialog
        title: "Add files to send"
        fileMode: FileDialog.OpenFiles
        nameFilters: ["All supported files (*.vpk *.zip *.rar *.7z *.apk *.mp4 *.mkv *.avi *.mov *.m4v *.mpg *.mpeg *.wmv *.webm *.jpg *.jpeg *.png *.bmp *.gif *.tif *.tiff *.webp *.mp3 *.flac *.wav *.m4a *.ogg *.aac)", "All files (*)"]
        onAccepted: {
            var paths = []
            for (var i = 0; i < selectedFiles.length; ++i) paths.push(selectedFiles[i].toString())
            Device.addDrops(paths)
        }
    }
    FolderDialog {
        id: folderDialog
        title: "Add a folder to send"
        onAccepted: Device.addDrops([selectedFolder.toString()])
    }
}
