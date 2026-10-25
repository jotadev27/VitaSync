import QtQuick
import VitaSync.Ui

Item {
    id: root
    property string title: ""
    property string detail: ""
    property string quantity: ""
    property string rate: ""
    property string statusText: ""
    property string statusTone: "busy"
    property string directionIcon: "upload"
    property real progress: 0
    property bool indeterminate: false
    property bool finished: false
    property bool failed: false
    property bool verified: false
    property bool canAct: true
    signal retryRequested()
    signal cancelRequested()
    readonly property real bodyRight: width - (canAct ? 44 : 0)

    height: finished ? 76 : 88
    Icon {
        x: 0; y: 14
        name: root.failed ? "alert" : root.directionIcon
        size: 18
        color: root.failed ? Theme.purple : Theme.inkMuted
    }
    Text {
        x: 32; y: 12
        width: Math.max(0, root.bodyRight - x - stateLine.implicitWidth - 16)
        text: root.title
        font.family: Theme.sansFamily
        font.pixelSize: Theme.sizeBody
        font.weight: Font.Medium
        color: Theme.ink
        elide: Text.ElideMiddle
    }
    Row {
        id: stateLine
        x: root.bodyRight - implicitWidth; y: 12
        spacing: 6
        Icon {
            visible: root.verified
            name: "check"; size: 16
            color: Theme.green
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            text: root.statusText
            font.family: Theme.sansFamily
            font.pixelSize: Theme.sizeSmall
            color: Theme.toneColor(root.statusTone)
        }
    }
    Text {
        id: amount
        x: root.bodyRight - width; y: 36
        text: (root.rate.length ? root.rate + " · " : "") + root.quantity
        font.family: Theme.sansFamily
        font.pixelSize: Theme.sizeSmall
        color: Theme.inkMuted
    }
    Text {
        x: 32; y: 36
        width: Math.max(0, amount.x - x - 16)
        text: root.detail
        font.family: Theme.sansFamily
        font.pixelSize: Theme.sizeSmall
        color: root.failed ? Theme.purple : Theme.inkFaint
        maximumLineCount: 2
        wrapMode: Text.WordWrap
        elide: Text.ElideRight
    }
    AppButton {
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        width: 32; height: 32
        visible: root.canAct
        variant: "quiet"
        iconName: root.finished ? "refresh" : "close"
        accessibleName: (root.finished ? "Retry " : "Cancel ") + root.title
        onClicked: root.finished ? root.retryRequested() : root.cancelRequested()
    }
    MeterBar {
        x: 32
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 12
        width: Math.max(0, root.bodyRight - x)
        height: 3
        visible: !root.finished
        value: root.progress
        indeterminate: root.indeterminate
        accent: Theme.blue
    }
    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width; height: 1
        color: Theme.line
    }
}
