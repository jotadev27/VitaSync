import QtQuick
import QtQuick.Window
import VitaSync
import VitaSync.Ui
import "components"
import "views"

// The window: a rail, a bar, one view at a time, and two overlays that only
// appear when asked for. No chrome beyond that.
Window {
    id: window

    width: 1180
    height: 760
    minimumWidth: 900
    minimumHeight: 560
    visible: true
    title: "VitaSync"
    color: Theme.base

    property int currentIndex: 0

    /// Set from the command line so a capture can target any screen.
    property string startView: "link"

    Component.onCompleted: {
        var order = ["link", "install", "browse", "transfers"]
        var index = order.indexOf(startView)
        if (index >= 0)
            currentIndex = index
    }

    Connections {
        target: Device
        function onNotify(message, tone) {
            toast.show(message, tone)
        }
        // Pressing Start moves to Transfers, where the work is. Nothing pulls
        // the user back afterwards: a finished job says what it needs there,
        // and yanking the screen out from under someone mid-scroll is worse
        // than making them click once.
        function onTransfersStarted(queued) {
            window.currentIndex = 3
        }
    }

    Row {
        anchors.fill: parent
        spacing: 0

        NavRail {
            id: rail
            height: parent.height
            currentIndex: window.currentIndex
            dropBadge: Device.drops.count
            transferBadge: Device.transfers.activeCount + Device.transfers.pendingCount
            alertBadge: Device.transfers.failedCount
            onNavigate: function (index) { window.currentIndex = index }
            onFaqRequested: faq.open = !faq.open
        }

        Column {
            width: parent.width - rail.width
            height: parent.height
            spacing: 0

            TopBar {
                id: topBar
                width: parent.width
                onDetailsRequested: details.open = !details.open
            }

            // Views are kept alive rather than reloaded so a queue in flight
            // never loses its scroll position or its in-progress state.
            Item {
                id: stage
                width: parent.width
                // Whatever is on screen stops here. The height below is
                // arithmetic over siblings that resize themselves, and a view
                // that briefly believes it has more room must not paint over
                // the strip underneath it while it finds out otherwise.
                clip: true
                height: parent.height - topBar.height - details.height - statusStrip.height

                ConnectView {
                    anchors.fill: parent
                    visible: window.currentIndex === 0
                    enabled: visible
                }

                InstallView {
                    anchors.fill: parent
                    visible: window.currentIndex === 1
                    enabled: visible
                }

                BrowseView {
                    anchors.fill: parent
                    visible: window.currentIndex === 2
                    enabled: visible
                }

                TransfersView {
                    anchors.fill: parent
                    visible: window.currentIndex === 3
                    enabled: visible
                }
            }

            DetailsDrawer {
                id: details
                width: parent.width
            }

            // The bottom strip: transient messages on the left, the one number
            // that matters on the right.
            Rectangle {
                id: statusStrip
                width: parent.width
                height: 34
                color: Theme.surface

                Rectangle {
                    anchors.top: parent.top
                    width: parent.width
                    height: Theme.border
                    color: Theme.line
                }

                ToastStrip {
                    id: toast
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.gap
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - 320
                }

                Row {
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.gapLarge
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 14

                    Row {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 6
                        visible: Device.transfers.busy

                        Icon {
                            anchors.verticalCenter: parent.verticalCenter
                            name: "upload"
                            size: 12
                            color: Theme.green
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: Math.round(Device.transfers.overallProgress * 100) + "%"
                            font.family: Theme.monoFamily
                            font.pixelSize: Theme.sizeSmall
                            color: Theme.green
                        }
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: Device.currentPath
                        font.family: Theme.monoFamily
                        font.pixelSize: Theme.sizeMicro
                        color: Theme.inkFaint
                        elide: Text.ElideLeft
                        width: Math.min(implicitWidth, 260)
                    }
                }
            }
        }
    }

    FaqPanel {
        id: faq
        anchors.fill: parent
    }

    // Keyboard: the shortcuts a file manager is expected to have.
    Shortcut { sequence: "Ctrl+A"; onActivated: if (window.currentIndex === 2) Device.browser.selectAll() }
    Shortcut { sequence: "Escape"; onActivated: { if (faq.open) faq.open = false; else Device.browser.clearSelection() } }
    Shortcut { sequence: "F5"; onActivated: Device.refresh() }
    Shortcut { sequence: "Ctrl+L"; onActivated: window.currentIndex = 0 }
    Shortcut { sequence: "F1"; onActivated: faq.open = !faq.open }
}
