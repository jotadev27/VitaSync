import QtQuick
import VitaSync
import VitaSync.Ui
import "../components"

// Transfers: what is moving, how fast, and whether it arrived intact.
Item {
    id: root

    Column {
        anchors.fill: parent
        anchors.margins: 24
        spacing: Theme.gapLarge

        SectionLabel {
            width: parent.width
            text: "Transfer queue"
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
                        text: "Overall progress"
                        font.family: Theme.sansFamily
                        font.pixelSize: Theme.sizeMicro
                        font.weight: Font.Medium
                        font.letterSpacing: 0
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

            Item { width: parent.width - clearButton.width - parent.spacing; height: 1 }

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
            spacing: 0
            model: Device.transfers

            delegate: TransferRow {
                width: ListView.view.width
                title: model.title
                detail: model.nextStep.length > 0 && model.jobState === 6
                      ? model.nextStep : (model.message.length > 0 ? model.message : model.subtitle)
                quantity: model.fileProgress.length > 0 ? model.fileProgress : model.doneText + " / " + model.totalText
                rate: model.rateText
                statusText: model.stateLabel
                statusTone: model.jobState === 7 ? "ok" : (model.jobState === 8 ? "error" : (model.jobState === 9 ? "warn" : "info"))
                directionIcon: model.kind === 1 || model.kind === 4 ? "download" : (model.kind === 3 ? "package" : "upload")
                progress: model.progress
                indeterminate: model.jobState === 1 || model.jobState === 2 || model.jobState === 3
                finished: model.isFinished
                failed: model.isFailed
                verified: model.verified
                canAct: model.jobState !== 7
                onRetryRequested: Device.transfers.retryJob(model.jobId)
                onCancelRequested: Device.transfers.cancelJob(model.jobId)
            }
            EmptyState {
                anchors.centerIn: parent
                visible: jobs.count === 0
                iconName: "queue"
                text: "No transfers yet"
            }
        }
        }
    }
}
