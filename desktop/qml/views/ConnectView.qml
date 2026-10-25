import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import VitaSync
import VitaSync.Ui
import "../components"

Item {
    id: root
    property bool manualUsb: false
    property bool rescanFeedback: false
    Timer {
        id: rescanFeedbackTimer
        interval: 900
        onTriggered: {
            if (Device.usbScanning) restart()
            else root.rescanFeedback = false
        }
    }
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
        padding: 24
        clip: true
        contentWidth: availableWidth
        contentHeight: layout.implicitHeight
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        Column {
            id: layout
            width: Math.min(scroll.availableWidth, 760)
            spacing: 24
            Column {
                width: parent.width
                spacing: 8
                Text {
                    text: "Connect your PS Vita"
                    color: Theme.ink
                    font.family: Theme.sansFamily
                    font.pixelSize: Theme.sizeDisplay
                    font.weight: Font.Medium
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
                height: connection.implicitHeight + 40
                color: Theme.surface
                radius: 10
                border.color: Theme.line
                clip: true
                Column {
                    id: connection
                    x: 20
                    y: 20
                    width: parent.width - 40
                    spacing: 20
                    Rectangle {
                        id: modeSelector
                        width: Math.min(parent.width, 320)
                        height: 46
                        radius: 8
                        color: Theme.base
                        border.color: Theme.line
                        Rectangle {
                            x: 4 + (Device.connectionMode === "usb" ? (parent.width - 8) / 2 : 0)
                            y: 4
                            width: (parent.width - 8) / 2
                            height: parent.height - 8
                            radius: 5
                            color: Theme.primaryFill
                            Behavior on x { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
                        }
                        Row {
                            anchors.fill: parent
                            anchors.margins: 4
                            Repeater {
                                model: [{mode: "ftp", title: "Wi-Fi / FTP", icon: "wifi"},
                                        {mode: "usb", title: "USB cable", icon: "usb"}]
                                Item {
                                    id: modeOption
                                    width: (modeSelector.width - 8) / 2
                                    height: modeSelector.height - 8
                                    activeFocusOnTab: true
                                    Accessible.role: Accessible.RadioButton
                                    Accessible.name: modelData.title
                                    Accessible.checkable: true
                                    Accessible.checked: Device.connectionMode === modelData.mode
                                    Accessible.onPressAction: Device.connectionMode = modelData.mode
                                    Keys.onSpacePressed: Device.connectionMode = modelData.mode
                                    Keys.onReturnPressed: Device.connectionMode = modelData.mode
                                    Rectangle {
                                        anchors.fill: parent
                                        radius: 5
                                        color: "transparent"
                                        border.width: parent.activeFocus ? 2 : 0
                                        border.color: Theme.blue
                                    }
                                    Row {
                                        anchors.centerIn: parent
                                        spacing: 9
                                        Icon {
                                            name: modelData.icon
                                            size: 18
                                            color: Device.connectionMode === modelData.mode ? Theme.primaryInk : Theme.inkMuted
                                            anchors.verticalCenter: parent.verticalCenter
                                        }
                                        Text {
                                            text: modelData.title
                                            font.family: Theme.sansFamily
                                            font.pixelSize: Theme.sizeBody
                                            font.weight: Font.Medium
                                            color: Device.connectionMode === modelData.mode ? Theme.primaryInk : Theme.inkMuted
                                        }
                                    }
                                    TapHandler {
                                        onTapped: {
                                            modeOption.forceActiveFocus(Qt.MouseFocusReason)
                                            Device.connectionMode = modelData.mode
                                        }
                                    }
                                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                                }
                            }
                        }
                    }
                    Item {
                        id: modeContent
                        width: parent.width
                        height: Device.connectionMode === "ftp" ? ftpPanel.implicitHeight : usbPanel.implicitHeight
                        clip: true
                        Behavior on height { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
                    Column {
                        id: ftpPanel
                        opacity: Device.connectionMode === "ftp" ? 1 : 0
                        visible: opacity > 0
                        enabled: Device.connectionMode === "ftp"
                        Behavior on opacity { NumberAnimation { duration: 120 } }
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
                        id: usbPanel
                        opacity: Device.connectionMode === "usb" ? 1 : 0
                        visible: opacity > 0
                        enabled: Device.connectionMode === "usb"
                        Behavior on opacity { NumberAnimation { duration: 120 } }
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
                                    text: "Open VitaShell in USB mode and press SELECT."
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
                            text: "No drive yet? Check the cable and USB storage selected in VitaShell."
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
                                text: root.rescanFeedback ? "Scanning…" : "Rescan"
                                iconName: "refresh"
                                width: 128
                                busy: root.rescanFeedback
                                spinIcon: true
                                enabled: !root.rescanFeedback && !Device.connected
                                onClicked: { root.rescanFeedback = true; rescanFeedbackTimer.restart(); Device.refreshUsbCandidates() }
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
                        mono: false
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
                        mono: false
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
