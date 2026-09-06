import QtQuick
import QtQuick.Dialogs
import VitaSync
import VitaSync.Ui
import "../components"

// Link: the address, the port, and the state of the line. Two fields, one
// button, and whatever the device told us about itself once it answers.
Item {
    id: root

    readonly property bool ipLooksValid: Device.hostValid

    Column {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Theme.gapLarge * 1.4
        spacing: Theme.gapLarge

        SectionLabel {
            width: parent.width
            text: "DEVICE"
            accent: Theme.blue
            trailing: Device.connected ? "linked" : "offline"
        }

        // Two independent ways to reach the same device. Picking one never
        // disables the other -- switching just drops whichever link is live,
        // the same as pressing Disconnect first.
        Row {
            spacing: 6

            Chip {
                text: "WI-FI"
                iconName: "link"
                selected: Device.connectionMode === "ftp"
                onClicked: Device.connectionMode = "ftp"
            }
            Chip {
                text: "USB"
                iconName: "usb"
                selected: Device.connectionMode === "usb"
                onClicked: {
                    Device.connectionMode = "usb"
                    Device.refreshUsbCandidates()
                }
            }
        }

        // The address row. Both boxes are the same component at the same
        // height, so they line up by construction rather than by eye.
        Row {
            visible: Device.connectionMode === "ftp"
            spacing: Theme.gap

            InputField {
                id: hostField
                width: 250
                label: "IP ADDRESS"
                placeholder: "192.168.0.00"
                text: Device.host
                showValidity: true
                valid: root.ipLooksValid || Device.host.length === 0
                    ? Device.hostValid || Device.host.length === 0
                    : false
                inputMethodHints: Qt.ImhPreferNumbers
                onEdited: function (value) { Device.host = value }
                onAccepted: if (Device.hostValid) Device.connectToVita()
            }

            InputField {
                id: portField
                width: 110
                label: "PORT"
                placeholder: "1337"
                text: Device.port.toString()
                showValidity: true
                valid: Device.port > 0 && Device.port <= 65535
                validator: IntValidator { bottom: 1; top: 65535 }
                inputMethodHints: Qt.ImhDigitsOnly
                onEdited: function (value) { Device.port = parseInt(value) || 0 }
                onAccepted: if (Device.hostValid) Device.connectToVita()
            }

            AppButton {
                anchors.bottom: hostField.controlBottom
                text: Device.connected ? "Disconnect" : "Connect"
                iconName: Device.connected ? "power" : "link"
                variant: Device.connected ? "secondary" : "primary"
                accent: Device.connected ? Theme.purple : Theme.blue
                enabled: Device.connected || Device.hostValid
                busy: Device.connecting
                onClicked: Device.connected ? Device.disconnectFromVita() : Device.connectToVita()
            }
        }

        // Recent addresses, one tap to reuse. Each entry is "host:port", so
        // picking one restores the port it actually worked on too, not just
        // the bare IP.
        Row {
            visible: Device.connectionMode === "ftp" && Device.recentHosts.length > 0
            spacing: 6

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "RECENT"
                font.family: Theme.sansFamily
                font.pixelSize: Theme.sizeMicro
                font.weight: Font.Bold
                font.letterSpacing: 1.2
                color: Theme.inkFaint
                rightPadding: 6
            }

            Repeater {
                model: Device.recentHosts

                Chip {
                    anchors.verticalCenter: parent.verticalCenter
                    text: modelData
                    selected: modelData === (Device.host + ":" + Device.port)
                    onClicked: {
                        Device.selectRecentHost(modelData)
                        // Property assignment, not a binding: these fields
                        // need this explicit push after an imperative change,
                        // the same as every other "pick a recent value" spot.
                        hostField.text = Device.host
                        portField.text = Device.port.toString()
                    }
                }
            }

            // Only worth offering once the list is actually long enough to
            // want tidying -- not on every session with one or two entries.
            Chip {
                anchors.verticalCenter: parent.verticalCenter
                visible: Device.recentHosts.length > 5
                text: "Clean"
                iconName: "trash"
                accent: Theme.purple
                onClicked: Device.clearRecentHosts()
            }
        }

        // USB: no address to type -- VitaShell's USB mode hands the card to
        // the OS as a normal drive, so connecting is picking which mounted
        // volume is the Vita. See docs/usb.md.
        Column {
            width: parent.width
            visible: Device.connectionMode === "usb"
            spacing: Theme.gap

            Row {
                spacing: Theme.gap

                AppButton {
                    text: "Scan"
                    iconName: "refresh"
                    onClicked: Device.refreshUsbCandidates()
                }

                AppButton {
                    text: Device.connected ? "Disconnect" : "Connect"
                    iconName: Device.connected ? "power" : "usb"
                    variant: Device.connected ? "secondary" : "primary"
                    accent: Device.connected ? Theme.purple : Theme.blue
                    enabled: Device.connected || Device.usbRootPath.length > 0
                        || Device.usbCandidates.length > 0
                    busy: Device.connecting
                    onClicked: Device.connected ? Device.disconnectFromVita() : Device.connectUsb()
                }
            }

            // Detected volumes: id.dat plus a VitaShell install folder, which
            // is what tells a Vita card apart from an unrelated USB drive.
            Row {
                visible: Device.usbCandidates.length > 0
                spacing: 6

                Repeater {
                    model: Device.usbCandidates

                    Chip {
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.label + " · " + modelData.sizeText
                        iconName: modelData.verified ? "check" : "alert"
                        accent: modelData.verified ? Theme.green : Theme.purple
                        selected: modelData.rootPath === Device.usbRootPath
                        onClicked: Device.usbRootPath = modelData.rootPath
                    }
                }
            }

            EmptyState {
                width: parent.width
                visible: Device.usbCandidates.length === 0
                iconName: "usb"
                text: "No USB-connected Vita — plug in, pick USB in VitaShell, then Scan"
            }

            // Manual override for a volume the scan missed -- same pattern as
            // pointing the app at a cover-pack or download folder below.
            Row {
                spacing: Theme.gap

                InputField {
                    id: usbField
                    width: 380
                    label: "USB VOLUME (MANUAL)"
                    placeholder: "the mounted drive, if Scan found nothing"
                    text: Device.usbRootPathDisplay
                    onEdited: function (value) { Device.usbRootPath = value }
                }

                AppButton {
                    anchors.bottom: usbField.controlBottom
                    iconName: "folder"
                    text: "Browse"
                    onClicked: usbDialog.open()
                }
            }
        }

        // Once linked: what answered, and what it can do.
        Rectangle {
            width: parent.width
            height: Device.connected ? deviceInfo.implicitHeight + 2 * Theme.pad : 0
            visible: Device.connected
            radius: Theme.radius
            color: Theme.surface
            border.width: Theme.border
            border.color: Theme.line
            clip: true

            Rectangle {
                width: 2
                height: parent.height
                color: Theme.green
            }

            Column {
                id: deviceInfo
                anchors.left: parent.left
                anchors.leftMargin: Theme.pad + 4
                anchors.right: parent.right
                anchors.rightMargin: Theme.pad
                anchors.verticalCenter: parent.verticalCenter
                spacing: 7

                Row {
                    spacing: 8

                    Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "vita"
                        size: 16
                        color: Theme.green
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: Device.deviceLabel
                        font.family: Theme.monoFamily
                        font.pixelSize: Theme.sizeBody
                        color: Theme.ink
                        elide: Text.ElideRight
                        width: Math.min(implicitWidth, deviceInfo.width - 40)
                    }
                }

                Row {
                    spacing: 6

                    Repeater {
                        model: Device.mountPoints

                        Chip {
                            text: modelData.mount
                            iconName: modelData.iconName
                            selected: Device.mount === modelData.mount
                            onClicked: Device.mount = modelData.mount
                        }
                    }
                }
            }
        }

        SectionLabel {
            width: parent.width
            text: "LIBRARY"
            accent: Theme.green
            trailing: Device.metadata.titleCount + " titles · " + Device.metadata.coverCount + " covers"
        }

        // Cover pack: the only way art gets into an app that never goes online.
        Row {
            spacing: Theme.gap

            InputField {
                id: coverField
                width: 380
                label: "COVER PACK FOLDER"
                placeholder: "folder of TITLEID.png"
                hint: "Optional. Box art for your games, named by Title ID."
                text: Device.coverPackPathDisplay
                onEdited: function (value) { Device.coverPackPath = value }
            }

            AppButton {
                anchors.bottom: coverField.controlBottom
                iconName: "folder"
                text: "Browse"
                onClicked: coverDialog.open()
            }
        }

        Row {
            spacing: Theme.gap

            InputField {
                id: downloadField
                width: 380
                label: "DOWNLOAD FOLDER"
                placeholder: "where files from the Vita land"
                hint: "Where saves, photos and videos pulled off the Vita are put."
                text: Device.downloadDirDisplay
                onEdited: function (value) { Device.downloadDir = value }
            }

            AppButton {
                anchors.bottom: downloadField.controlBottom
                iconName: "download"
                text: "Browse"
                onClicked: downloadDialog.open()
            }
        }
    }

    FolderDialog {
        id: coverDialog
        title: "Select cover pack folder"
        onAccepted: {
            Device.coverPackPath = selectedFolder
            coverField.text = Device.coverPackPathDisplay
        }
    }

    FolderDialog {
        id: downloadDialog
        title: "Select download folder"
        onAccepted: Device.downloadDir = selectedFolder
    }

    FolderDialog {
        id: usbDialog
        title: "Select the Vita's mounted USB volume"
        onAccepted: {
            Device.usbRootPath = selectedFolder
            usbField.text = Device.usbRootPathDisplay
        }
    }
}
