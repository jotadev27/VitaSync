import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import VitaSync
import VitaSync.Ui
import "../components"

Item {
    id: root
    property bool manualUsb: false
    Connections {
        target: Device
        function onConnectionInfoChanged() {
            if (!hostField.editing) hostField.text = Device.host
            if (!portField.editing) portField.text = Device.port.toString()
        }
        function onUsbRootPathChanged() { usbField.text = Device.usbRootPathDisplay }
        function onSettingsChanged() {
            downloadField.text = Device.downloadDirDisplay
            coverField.text = Device.coverPackPathDisplay
        }
    }
    ScrollView {
        id: scroll
        anchors.fill: parent
        padding: 32
        clip: true
        contentWidth: availableWidth
        contentHeight: layout.implicitHeight
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        Column {
            id: layout
            width: Math.min(scroll.availableWidth, 820)
            spacing: 28
            Column {
                width: parent.width
                spacing: 8
                Text {
                    text: "Connect your PS Vita"
                    color: Theme.ink
                    font.family: Theme.sansFamily
                    font.pixelSize: Theme.sizeDisplay
                    font.weight: Font.DemiBold
                }
                Text {
                    width: parent.width
                    text: "Open VitaShell, choose FTP or USB in START, then press SELECT."
                    color: Theme.inkMuted
                    font.family: Theme.sansFamily
                    font.pixelSize: Theme.sizeBody
                    wrapMode: Text.WordWrap
                }
            }
            Rectangle {
                width: parent.width
                height: connection.implicitHeight + 48
                color: Theme.surface
                radius: 10
                border.color: Theme.line
                Column {
                    id: connection
                    x: 24
                    y: 24
                    width: parent.width - 48
                    spacing: 24
                    Row {
                        spacing: 8
                        AppButton {
                            text: "Wi-Fi / FTP"
                            iconName: "wifi"
                            accent: Theme.inkMuted
                            variant: Device.connectionMode === "ftp" ? "primary" : "quiet"
                            onClicked: Device.connectionMode = "ftp"
                        }
                        AppButton {
                            text: "USB cable"
                            iconName: "usb"
                            accent: Theme.inkMuted
                            variant: Device.connectionMode === "usb" ? "primary" : "quiet"
                            onClicked: Device.connectionMode = "usb"
                        }
                    }
                    Column {
                        visible: Device.connectionMode === "ftp"
                        width: parent.width
                        spacing: 18
                        Text {
                            width: parent.width
                            text: "Enter the address shown in VitaShell. Both devices must use the same network."
                            color: Theme.inkMuted
                            font.family: Theme.sansFamily
                            font.pixelSize: Theme.sizeSmall
                            wrapMode: Text.WordWrap
                        }
                        Row {
                            width: parent.width
                            spacing: 12
                            InputField {
                                id: hostField
                                width: parent.width - portField.width - ftpConnect.width - 24
                                label: "IP address"
                                placeholder: "127.0.0.1"
                                text: Device.host
                                showValidity: true
                                valid: Device.hostValid || Device.host.length === 0
                                enabled: !Device.connected && !Device.connecting
                                inputMethodHints: Qt.ImhPreferNumbers
                                onEdited: function(value) { Device.host = value }
                                onAccepted: if (Device.hostValid && !Device.connecting) Device.connectToVita()
                            }
                            InputField {
                                id: portField
                                width: 90
                                label: "Port"
                                text: Device.port.toString()
                                placeholder: "1337"
                                showValidity: true
                                valid: Device.port > 0 && Device.port <= 65535
                                enabled: !Device.connected && !Device.connecting
                                validator: IntValidator { bottom: 1; top: 65535 }
                                inputMethodHints: Qt.ImhDigitsOnly
                                onEdited: function(value) { Device.port = parseInt(value) || 0 }
                                onAccepted: if (Device.hostValid && !Device.connecting) Device.connectToVita()
                            }
                            AppButton {
                                id: ftpConnect
                                y: hostField.controlY
                                text: Device.connected ? "Disconnect" : (Device.connecting ? "Cancel" : "Connect")
                                iconName: Device.connected || Device.connecting ? "power" : "link"
                                variant: Device.connected || Device.connecting ? "secondary" : "primary"
                                enabled: Device.connected || Device.connecting || Device.hostValid
                                busy: Device.connecting
                                onClicked: Device.connected || Device.connecting ? Device.disconnectFromVita() : Device.connectToVita()
                            }
                        }
                        Column {
                            visible: Device.recentHosts.length > 0
                            width: parent.width
                            spacing: 8
                            Row {
                                width: parent.width
                                Text {
                                    width: parent.width - clearRecent.width
                                    text: "Recent connections"
                                    color: Theme.inkMuted
                                    font.family: Theme.sansFamily
                                    font.pixelSize: Theme.sizeSmall
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                AppButton {
                                    id: clearRecent
                                    text: "Clear"
                                    variant: "quiet"
                                    onClicked: Device.clearRecentHosts()
                                }
                            }
                            Flow {
                                width: parent.width
                                spacing: 8
                                enabled: !Device.connected && !Device.connecting
                                Repeater {
                                    model: Device.recentHosts
                                    Chip {
                                        text: modelData
                                        selected: modelData === Device.host + ":" + Device.port
                                        onClicked: {
                                            Device.selectRecentHost(modelData)
                                            hostField.text = Device.host
                                            portField.text = Device.port.toString()
                                        }
                                    }
                                }
                            }
                        }
                    }
                    Column {
                        visible: Device.connectionMode === "usb"
                        width: parent.width
                        spacing: 16
                        Row {
                            width: parent.width
                            spacing: 12
                            Icon {
                                name: Device.usbCandidates.length > 0 ? "check" : "usb"
                                size: 24
                                color: Device.usbCandidates.length > 0 ? Theme.green : Theme.inkMuted
                            }
                            Column {
                                width: parent.width - 36
                                spacing: 6
                                Text {
                                    width: parent.width
                                    text: Device.connected ? "Vita connected over USB"
                                        : (Device.usbCandidates.length > 0 ? "Vita storage detected" : "Waiting for Vita storage…")
                                    color: Theme.ink
                                    font.family: Theme.sansFamily
                                    font.pixelSize: Theme.sizeBody
                                    font.weight: Font.DemiBold
                                    wrapMode: Text.WordWrap
                                }
                                Text {
                                    width: parent.width
                                    text: "With USB selected in VitaShell, press SELECT. Your drive will appear here automatically."
                                    color: Theme.inkMuted
                                    font.family: Theme.sansFamily
                                    font.pixelSize: Theme.sizeSmall
                                    wrapMode: Text.WordWrap
                                }
                            }
                        }
                        Repeater {
                            model: Device.usbCandidates
                            Rectangle {
                                width: connection.width
                                height: 64
                                radius: Theme.radius
                                color: modelData.rootPath === Device.usbRootPath ? Theme.surfaceAlt : "transparent"
                                border.color: modelData.rootPath === Device.usbRootPath ? Theme.lineStrong : Theme.line
                                Icon {
                                    anchors.left: parent.left
                                    anchors.leftMargin: 14
                                    anchors.verticalCenter: parent.verticalCenter
                                    name: "card"
                                    size: 22
                                }
                                Column {
                                    anchors.left: parent.left
                                    anchors.leftMargin: 50
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: parent.width - 94
                                    spacing: 4
                                    Text {
                                        width: parent.width
                                        text: modelData.label + "  ·  " + modelData.rootPath
                                        color: Theme.ink
                                        font.family: Theme.sansFamily
                                        font.pixelSize: Theme.sizeBody
                                        elide: Text.ElideMiddle
                                    }
                                    Text {
                                        text: modelData.freeText + " free of " + modelData.sizeText
                                        color: Theme.inkMuted
                                        font.family: Theme.sansFamily
                                        font.pixelSize: Theme.sizeSmall
                                    }
                                }
                                Icon {
                                    visible: modelData.rootPath === Device.usbRootPath
                                    anchors.right: parent.right
                                    anchors.rightMargin: 14
                                    anchors.verticalCenter: parent.verticalCenter
                                    name: "check"
                                    color: Theme.green
                                    size: 18
                                }
                                HoverHandler { cursorShape: Qt.PointingHandCursor }
                                TapHandler { enabled: !Device.connected; onTapped: Device.usbRootPath = modelData.rootPath }
                            }
                        }
                        Text {
                            width: parent.width
                            visible: Device.usbCandidates.length === 0 && !Device.connected
                            text: "If the drive also fails to appear in your file explorer, check the USB cable and the USB device selected in VitaShell."
                            color: Theme.inkFaint
                            font.family: Theme.sansFamily
                            font.pixelSize: Theme.sizeSmall
                            wrapMode: Text.WordWrap
                        }
                        Row {
                            spacing: 8
                            AppButton {
                                text: Device.connected ? "Disconnect" : "Connect USB"
                                iconName: Device.connected ? "power" : "usb"
                                variant: Device.connected ? "secondary" : "primary"
                                onClicked: Device.connected ? Device.disconnectFromVita() : Device.connectUsb()
                            }
                            AppButton {
                                text: "Rescan"
                                iconName: "refresh"
                                busy: Device.usbScanning
                                enabled: !Device.usbScanning && !Device.connected
                                onClicked: Device.refreshUsbCandidates()
                            }
                            AppButton {
                                text: root.manualUsb ? "Hide manual selection" : "Select drive manually"
                                variant: "quiet"
                                onClicked: root.manualUsb = !root.manualUsb
                            }
                        }
                        Row {
                            visible: root.manualUsb
                            width: parent.width
                            spacing: 12
                            InputField {
                                id: usbField
                                width: parent.width - usbBrowse.width - 12
                                label: "Vita drive"
                                placeholder: "Drive root, for example E:/"
                                text: Device.usbRootPathDisplay
                                enabled: !Device.connected
                                onEdited: function(value) { Device.usbRootPath = value }
                            }
                            AppButton {
                                id: usbBrowse
                                y: usbField.controlY
                                text: "Browse"
                                iconName: "folder"
                                enabled: !Device.connected
                                onClicked: usbDialog.open()
                            }
                        }
                    }
                    Column {
                        visible: Device.connected
                        width: parent.width
                        spacing: 12
                        Text {
                            width: parent.width
                            text: Device.deviceLabel
                            color: Theme.green
                            font.family: Theme.sansFamily
                            font.pixelSize: Theme.sizeBody
                            elide: Text.ElideRight
                        }
                        Flow {
                            width: parent.width
                            spacing: 8
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
            }
            Column {
                width: parent.width
                spacing: 20
                SectionLabel {
                    width: parent.width
                    text: "Local folders"
                    trailing: Device.metadata.titleCount + " titles in the offline library"
                }
                Row {
                    width: parent.width
                    spacing: 12
                    InputField {
                        id: downloadField
                        width: parent.width - downloadBrowse.width - 12
                        label: "Save files downloaded from your Vita"
                        text: Device.downloadDirDisplay
                        onEdited: function(value) { Device.downloadDir = value }
                    }
                    AppButton {
                        id: downloadBrowse
                        y: downloadField.controlY
                        text: "Browse"
                        iconName: "folder"
                        onClicked: downloadDialog.open()
                    }
                }
                Row {
                    width: parent.width
                    spacing: 12
                    InputField {
                        id: coverField
                        width: parent.width - coverBrowse.width - 12
                        label: "Cover artwork (optional)"
                        placeholder: "Folder containing TITLEID.png images"
                        text: Device.coverPackPathDisplay
                        onEdited: function(value) { Device.coverPackPath = value }
                    }
                    AppButton {
                        id: coverBrowse
                        y: coverField.controlY
                        text: "Browse"
                        iconName: "folder"
                        onClicked: coverDialog.open()
                    }
                }
                Text {
                    width: parent.width
                    text: "Recent connections are saved on this computer after a successful connection."
                    color: Theme.inkFaint
                    font.family: Theme.sansFamily
                    font.pixelSize: Theme.sizeSmall
                    wrapMode: Text.WordWrap
                }
            }
        }
    }
    FolderDialog {
        id: usbDialog
        title: "Select the root of the Vita USB drive"
        onAccepted: Device.usbRootPath = selectedFolder
    }
    FolderDialog {
        id: coverDialog
        title: "Select cover artwork folder"
        onAccepted: Device.coverPackPath = selectedFolder
    }
    FolderDialog {
        id: downloadDialog
        title: "Choose where to save downloaded files"
        onAccepted: Device.downloadDir = selectedFolder
    }
}
