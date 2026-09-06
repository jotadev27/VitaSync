import QtQuick
import VitaSync
import VitaSync.Ui
import "../components"

// Transfers: what is moving, how fast, and whether it arrived intact.
Item {
    id: root

    Column {
        anchors.fill: parent
        anchors.margins: Theme.gapLarge * 1.4
        spacing: Theme.gapLarge

        SectionLabel {
            width: parent.width
            text: "QUEUE"
            accent: Theme.green
            trailing: Device.transfers.activeCount > 0
                      ? Device.transfers.activeCount + " running · " + Device.transfers.pendingCount + " waiting"
                      : Device.transfers.count + " jobs"
        }

        // Overall progress: one bar for the whole queue, so the window can be
        // half-covered and still readable.
        Rectangle {
            width: parent.width
            height: Device.transfers.busy ? 58 : 0
            visible: height > 0
            clip: true
            radius: Theme.radius
            color: Theme.surface
            border.width: Theme.border
            border.color: Theme.line

            Behavior on height { NumberAnimation { duration: Theme.normal } }

            Column {
                anchors.fill: parent
                anchors.margins: Theme.pad
                spacing: 9

                Row {
                    width: parent.width

                    Text {
                        text: "ALL TRANSFERS"
                        font.family: Theme.sansFamily
                        font.pixelSize: Theme.sizeMicro
                        font.weight: Font.Bold
                        font.letterSpacing: 1.3
                        color: Theme.inkFaint
                    }

                    Item { width: parent.width - 200; height: 1 }

                    Text {
                        text: Math.round(Device.transfers.overallProgress * 100) + "%"
                        font.family: Theme.monoFamily
                        font.pixelSize: Theme.sizeSmall
                        color: Theme.green
                    }
                }

                MeterBar {
                    width: parent.width
                    value: Device.transfers.overallProgress
                    accent: Theme.green
                    segments: 48
                }
            }
        }

        Row {
            width: parent.width
            spacing: Theme.gap

            Item { width: parent.width - clearButton.width; height: 1 }

            AppButton {
                id: clearButton
                text: "Clear finished"
                iconName: "close"
                variant: "quiet"
                enabled: Device.transfers.count > 0
                onClicked: Device.transfers.clearFinished()
            }
        }

        ClippedList {
            width: parent.width
            height: parent.height - y

            ListView {
            id: jobs
            anchors.fill: parent
            clip: true
            // Card and gap are both very dark (Theme.surface / Theme.base),
            // so a thin gap reads as no gap at all -- the standard spacing
            // unit gives it enough area to actually register as one.
            spacing: Theme.gap
            model: Device.transfers

            delegate: Rectangle {
                id: jobCard
                width: ListView.view.width
                height: 76
                radius: Theme.radius
                color: Theme.surface
                // JobState: 0 Pending, 1 Checksumming, 2 Unpacking, 3 Scanning,
                // 4 Transferring, 5 Verifying, 6 AwaitingDevice, 7 Complete,
                // 8 Failed, 9 Cancelled.
                readonly property string stateTone: {
                    switch (jobState) {
                    case 6: return "info"     // AwaitingDevice
                    case 7: return "ok"       // Complete
                    case 8: return "error"    // Failed
                    case 9: return "warn"     // Cancelled
                    default: return "busy"
                    }
                }
                readonly property color stateAccent: Theme.toneColor(stateTone)

                // A failed card is outlined as well as railed, so it stands out
                // in the list without depending on the rail's colour alone.
                border.width: Theme.border
                border.color: isFailed ? Theme.purple : Theme.line

                StatusRail {
                    height: parent.height
                    tone: jobCard.stateTone
                }

                Column {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.pad + 4
                    anchors.rightMargin: Theme.pad
                    anchors.topMargin: 11
                    anchors.bottomMargin: 11
                    spacing: 8

                    Row {
                        width: parent.width
                        spacing: Theme.gap

                        // The kind glyph becomes an alert triangle the moment
                        // the job fails: shape carries the state, not only hue.
                        Icon {
                            anchors.verticalCenter: parent.verticalCenter
                            name: {
                                if (isFailed)
                                    return "alert"
                                if (kind === 1 || kind === 4)
                                    return "download"
                                if (kind === 3)
                                    return "package"      // a folder being installed
                                return "upload"
                            }
                            size: 16
                            weight: isFailed ? 2.1 : 1.6
                            color: jobCard.stateAccent
                        }

                        Column {
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - 300
                            spacing: 3

                            Text {
                                text: title
                                font.family: Theme.sansFamily
                                font.pixelSize: Theme.sizeBody
                                font.weight: Font.Medium
                                color: Theme.ink
                                elide: Text.ElideRight
                                width: parent.width
                            }

                            Row {
                                width: parent.width
                                spacing: 6

                                // An explicit word, so the state is readable
                                // even with every colour stripped out.
                                Rectangle {
                                    anchors.verticalCenter: parent.verticalCenter
                                    visible: isFailed
                                    width: failedLabel.implicitWidth + 10
                                    height: 14
                                    radius: 2
                                    color: "transparent"
                                    border.width: Theme.border
                                    border.color: Theme.purple

                                    Text {
                                        id: failedLabel
                                        anchors.centerIn: parent
                                        text: "FAILED"
                                        font.family: Theme.sansFamily
                                        font.pixelSize: Theme.sizeMicro
                                        font.weight: Font.Bold
                                        font.letterSpacing: 0.8
                                        color: Theme.purple
                                    }
                                }

                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: {
                                        if (nextStep.length > 0 && jobState === 6)
                                            return nextStep
                                        if (message.length > 0)
                                            return message
                                        return subtitle
                                    }
                                    font.family: Theme.monoFamily
                                    font.pixelSize: Theme.sizeMicro
                                    color: isFailed ? Theme.purple : Theme.inkFaint
                                    elide: Text.ElideRight
                                    width: parent.width - (isFailed ? failedLabel.width + 22 : 0)
                                }
                            }
                        }

                        Row {
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 8

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: rateText
                                font.family: Theme.monoFamily
                                font.pixelSize: Theme.sizeSmall
                                color: Theme.blue
                            }

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: fileProgress.length > 0
                                      ? fileProgress
                                      : doneText + " / " + totalText
                                font.family: Theme.monoFamily
                                font.pixelSize: Theme.sizeSmall
                                color: Theme.inkMuted
                            }

                            // Verified transfers get an unmistakable badge --
                            // a filled, outlined mark, not just a small glyph
                            // -- the same way FAILED is called out by more
                            // than colour alone. The FAQ explains what the
                            // app can and cannot guarantee.
                            Rectangle {
                                anchors.verticalCenter: parent.verticalCenter
                                visible: verified
                                width: 24
                                height: 24
                                radius: 12
                                color: Qt.rgba(Theme.green.r, Theme.green.g, Theme.green.b, 0.16)
                                border.width: Theme.border
                                border.color: Theme.green

                                Icon {
                                    anchors.centerIn: parent
                                    name: "shield"
                                    size: 15
                                    weight: 2.0
                                    color: Theme.green
                                }
                            }

                            AppButton {
                                anchors.verticalCenter: parent.verticalCenter
                                variant: "quiet"
                                iconName: isFinished ? "refresh" : "close"
                                accent: Theme.purple
                                visible: jobState !== 7
                                onClicked: isFinished ? Device.transfers.retryJob(jobId)
                                                      : Device.transfers.cancelJob(jobId)
                            }
                        }
                    }

                    Row {
                        width: parent.width
                        spacing: Theme.gap

                        MeterBar {
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - 108
                            value: progress
                            accent: jobCard.stateAccent
                            indeterminate: jobState === 1 || jobState === 2 || jobState === 3
                            segments: 56
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 100
                            horizontalAlignment: Text.AlignRight
                            text: stateLabel
                            font.family: Theme.sansFamily
                            font.pixelSize: Theme.sizeMicro
                            font.weight: Font.DemiBold
                            font.letterSpacing: 0.8
                            color: jobCard.stateAccent
                        }
                    }
                }
            }

            EmptyState {
                anchors.centerIn: parent
                visible: jobs.count === 0
                iconName: "queue"
                text: "QUEUE EMPTY"
            }
        }
        }
    }
}
