import QtQuick
import QtQuick.Dialogs
import VitaSync
import VitaSync.Ui
import "../components"

// Install: drop a package, see exactly what it is, press Start.
//
// The identification step is the point of this screen. Nothing is uploaded
// until the user has seen the name and Title ID the app read out of the
// package itself, next to the folder it is going to land in.
Item {
    id: root

    property string readyTitle: ""
    property string readyPath: ""
    property string readyStep: "press X in VitaShell"
    property bool readyIsTheme: false

    Connections {
        target: Device
        function onInstallReady(title, titleId, remotePath) {
            root.readyTitle = title
            root.readyPath = remotePath
            root.readyStep = "press X in VitaShell"
            root.readyIsTheme = false
        }
        // Anything that installs as a folder -- a theme, an unpacked game --
        // is finished by a different tool, so it says which one rather than
        // reusing the package wording.
        function onFolderInstallReady(title, remotePath, nextStep) {
            root.readyTitle = title
            root.readyPath = remotePath
            root.readyStep = nextStep
            root.readyIsTheme = true
        }
    }

    Column {
        id: layout
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.gapLarge * 1.4
        spacing: Theme.gapLarge

        SectionLabel {
            width: parent.width
            text: "DROP"
            accent: Theme.blue
            trailing: Device.drops.count > 0
                      ? Device.drops.count + " staged · " + Device.drops.totalSizeText
                      : "games · themes · media"
        }

        // The drop target. Its border is the whole affordance: a dashed
        // hairline that goes solid green the moment a file is over it.
        Rectangle {
            id: dropPlate
            width: parent.width
            height: Device.drops.count > 0 ? 92 : 210
            radius: Theme.radius
            color: dropArea.containsDrag ? Theme.greenWash : Theme.surface
            border.width: dropArea.containsDrag ? 2 : Theme.border
            border.color: dropArea.containsDrag ? Theme.green : Theme.lineStrong

            Behavior on height { NumberAnimation { duration: Theme.normal; easing.type: Easing.OutCubic } }
            Behavior on color { ColorAnimation { duration: Theme.fast } }
            Behavior on border.color { ColorAnimation { duration: Theme.fast } }

            // Corner ticks: a deliberate, drawn frame rather than a dashed
            // rectangle, which is what every drop zone on earth already is.
            Repeater {
                model: [[0, 0], [1, 0], [0, 1], [1, 1]]

                Item {
                    x: modelData[0] === 0 ? 9 : dropPlate.width - 9 - 14
                    y: modelData[1] === 0 ? 9 : dropPlate.height - 9 - 14
                    width: 14
                    height: 14
                    opacity: dropArea.containsDrag ? 1 : 0.5

                    Rectangle {
                        width: 14; height: 2
                        color: dropArea.containsDrag ? Theme.green : Theme.blue
                        anchors.top: modelData[1] === 0 ? parent.top : undefined
                        anchors.bottom: modelData[1] === 1 ? parent.bottom : undefined
                    }
                    Rectangle {
                        width: 2; height: 14
                        color: dropArea.containsDrag ? Theme.green : Theme.blue
                        anchors.left: modelData[0] === 0 ? parent.left : undefined
                        anchors.right: modelData[0] === 1 ? parent.right : undefined
                    }
                }
            }

            Column {
                anchors.centerIn: parent
                spacing: 12
                visible: Device.drops.count === 0

                Icon {
                    anchors.horizontalCenter: parent.horizontalCenter
                    name: dropArea.containsDrag ? "download" : "package"
                    size: 34
                    weight: 1.3
                    color: dropArea.containsDrag ? Theme.green : Theme.blue
                }

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: dropArea.containsDrag ? "RELEASE" : "DROP FILES HERE"
                    font.family: Theme.sansFamily
                    font.pixelSize: Theme.sizeSmall
                    font.weight: Font.DemiBold
                    font.letterSpacing: 2
                    color: dropArea.containsDrag ? Theme.green : Theme.inkMuted
                }

                AppButton {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "Choose files"
                    iconName: "folder"
                    variant: "quiet"
                    onClicked: fileDialog.open()
                }
            }

            // Compact state once things are staged: the plate shrinks and
            // becomes a second, smaller target plus the actions.
            Row {
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: Theme.gapLarge
                anchors.right: parent.right
                anchors.rightMargin: Theme.gapLarge
                visible: Device.drops.count > 0
                spacing: Theme.gap

                Icon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: "package"
                    size: 22
                    color: dropArea.containsDrag ? Theme.green : Theme.blue
                }

                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 3
                    width: parent.width - 300

                    Text {
                        text: Device.drops.count + (Device.drops.count === 1 ? " item ready" : " items ready")
                        font.family: Theme.sansFamily
                        font.pixelSize: Theme.sizeBody
                        font.weight: Font.Medium
                        color: Theme.ink
                    }

                    Text {
                        text: Device.drops.totalSizeText + "  →  " + Device.mount
                        font.family: Theme.monoFamily
                        font.pixelSize: Theme.sizeSmall
                        color: Theme.inkFaint
                    }
                }

                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.gap

                    AppButton {
                        text: "Clear"
                        iconName: "close"
                        variant: "secondary"
                        onClicked: Device.clearDrops()
                    }

                    AppButton {
                        text: "Start"
                        iconName: "play"
                        variant: "primary"
                        accent: Theme.green
                        enabled: Device.connected && Device.drops.readyCount > 0
                        onClicked: Device.startStaged()
                    }
                }
            }
        }

        // Verified-and-waiting callout. This is the honest end of the install
        // story: the file is on the device and checked; the tap is on the Vita.
        Rectangle {
            width: parent.width
            height: root.readyTitle.length > 0 ? 60 : 0
            visible: height > 0
            clip: true
            radius: Theme.radius
            color: Theme.greenWash
            border.width: Theme.border
            border.color: Theme.greenDim

            Behavior on height { NumberAnimation { duration: Theme.normal } }

            Row {
                anchors.fill: parent
                anchors.leftMargin: Theme.pad
                anchors.rightMargin: Theme.pad
                spacing: Theme.gap

                Icon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: "check"
                    size: 20
                    color: Theme.green
                }

                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - 320
                    spacing: 3

                    Text {
                        text: root.readyTitle
                        font.family: Theme.sansFamily
                        font.pixelSize: Theme.sizeBody
                        font.weight: Font.Medium
                        color: Theme.ink
                        elide: Text.ElideRight
                        width: parent.width
                    }

                    Text {
                        text: root.readyPath + "  ·  " + root.readyStep
                        font.family: Theme.monoFamily
                        font.pixelSize: Theme.sizeSmall
                        color: Theme.green
                        elide: Text.ElideRight
                        width: parent.width
                    }
                }

                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 6

                    AppButton {
                        text: "Open VitaShell"
                        iconName: "vita"
                        variant: "secondary"
                        accent: Theme.green
                        // Only meaningful for a package; a theme is applied
                        // somewhere else entirely.
                        visible: !root.readyIsTheme
                        enabled: Device.companionAvailable
                        onClicked: Device.openVitaShell()
                    }

                    AppButton {
                        variant: "quiet"
                        iconName: "close"
                        onClicked: root.readyTitle = ""
                    }
                }
            }
        }

        SectionLabel {
            width: parent.width
            visible: Device.drops.count > 0
            text: "IDENTIFIED"
            accent: Theme.blue
        }

    }

    // The staged list: what the app thinks each file is.
    // Anchored rather than sized from inside the Column, so it always ends
    // exactly where the content area does.
    ClippedList {
        anchors.top: layout.bottom
        anchors.topMargin: Theme.gap
        anchors.left: layout.left
        anchors.right: layout.right
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.gapLarge * 1.4
        visible: Device.drops.count > 0

        ListView {
        id: stagedList
        anchors.fill: parent
        clip: true
        spacing: 6
        model: Device.drops

        delegate: Rectangle {
            width: ListView.view.width
            height: 74
            radius: Theme.radius
            color: rowHover.hovered ? Theme.surfaceAlt : Theme.surface
            border.width: Theme.border
            border.color: Theme.line

            Behavior on color { ColorAnimation { duration: Theme.fast } }

            StatusRail {
                height: parent.height
                color: Theme.accentColor(accent)
                alert: model.error.length > 0
            }

            Row {
                anchors.fill: parent
                anchors.leftMargin: Theme.pad
                anchors.rightMargin: Theme.gap
                anchors.topMargin: 10
                anchors.bottomMargin: 10
                spacing: Theme.gap

                CoverArt {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 54
                    height: 54
                    source: coverSource
                    titleId: model.titleId
                    title: model.title
                    matched: model.matched
                }

                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - 54 - 250
                    spacing: 5

                    Row {
                        spacing: 8

                        Text {
                            text: model.title
                            font.family: Theme.sansFamily
                            font.pixelSize: Theme.sizeBody
                            font.weight: Font.Medium
                            color: Theme.ink
                            elide: Text.ElideRight
                            width: Math.min(implicitWidth, parent.parent.width - 90)
                        }

                        Chip {
                            anchors.verticalCenter: parent.verticalCenter
                            text: model.kindLabel
                            accent: Theme.accentColor(accent)
                            selected: true
                            interactive: false
                        }
                    }

                    Text {
                        text: model.subtitle
                        font.family: Theme.monoFamily
                        font.pixelSize: Theme.sizeSmall
                        color: Theme.inkMuted
                        elide: Text.ElideRight
                        width: parent.width
                    }

                    Row {
                        spacing: 6

                        Icon {
                            anchors.verticalCenter: parent.verticalCenter
                            name: "chevron"
                            size: 10
                            color: Theme.inkFaint
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: model.destination
                            font.family: Theme.monoFamily
                            font.pixelSize: Theme.sizeMicro
                            color: Theme.blue
                            elide: Text.ElideRight
                            width: Math.min(implicitWidth, parent.parent.width - 30)
                        }
                    }
                }

                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 110
                    spacing: 4

                    Text {
                        anchors.right: parent.right
                        text: model.sizeText
                        font.family: Theme.monoFamily
                        font.pixelSize: Theme.sizeBody
                        color: Theme.ink
                    }

                    Row {
                        anchors.right: parent.right
                        visible: model.error.length > 0
                        spacing: 5

                        Icon {
                            anchors.verticalCenter: parent.verticalCenter
                            name: "alert"
                            size: 12
                            weight: 2.1
                            color: Theme.purple
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: model.error
                            font.family: Theme.sansFamily
                            font.pixelSize: Theme.sizeMicro
                            color: Theme.purple
                            elide: Text.ElideRight
                            width: Math.min(implicitWidth, 96)
                        }
                    }
                }

                AppButton {
                    anchors.verticalCenter: parent.verticalCenter
                    variant: "quiet"
                    iconName: "close"
                    onClicked: Device.drops.removeAt(index)
                }
            }

            HoverHandler { id: rowHover }
        }
    }
    }

    // External file drops land anywhere on the screen, not just on the plate:
    // aiming at a target is a chore, and the whole view is unambiguous.
    DropArea {
        id: dropArea
        anchors.fill: parent
        keys: ["text/uri-list"]

        onDropped: function (drop) {
            if (!drop.hasUrls) {
                drop.accepted = false
                return
            }
            var paths = []
            for (var i = 0; i < drop.urls.length; ++i)
                paths.push(drop.urls[i].toString())
            Device.addDrops(paths)
            drop.acceptProposedAction()
        }
    }

    FileDialog {
        id: fileDialog
        title: "Select packages or media"
        fileMode: FileDialog.OpenFiles
        nameFilters: ["Vita packages (*.vpk)", "Media (*.mp4 *.mkv *.avi *.mp3 *.flac *.jpg *.png)", "All files (*)"]
        onAccepted: {
            var paths = []
            for (var i = 0; i < selectedFiles.length; ++i)
                paths.push(selectedFiles[i].toString())
            Device.addDrops(paths)
        }
    }
}
