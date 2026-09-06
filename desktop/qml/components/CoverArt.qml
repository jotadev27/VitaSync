import QtQuick
import VitaSync.Ui

// Box art for a package.
//
// When the local database has the art we show it. When it does not, we draw a
// deterministic placeholder from the Title ID rather than a generic grey box:
// the same game always gets the same plate, so the list stays scannable, and a
// corner notch marks it as unmatched without spending a sentence saying so.
Item {
    id: root

    property string source: ""
    property string titleId: ""
    property string title: ""
    property bool matched: false
    property color accent: Theme.blue

    implicitWidth: 54
    implicitHeight: 54

    readonly property int hash: {
        var h = 0
        var key = titleId.length > 0 ? titleId : title
        for (var i = 0; i < key.length; ++i)
            h = ((h << 5) - h + key.charCodeAt(i)) | 0
        return Math.abs(h)
    }

    readonly property string initials: {
        var key = title.length > 0 ? title : titleId
        var words = key.replace(/[^A-Za-z0-9 ]/g, " ").split(" ").filter(function (w) { return w.length > 0 })
        if (words.length === 0)
            return "??"
        if (words.length === 1)
            return String(words[0]).substring(0, 2).toUpperCase()
        return String(words[0].charAt(0) + words[1].charAt(0)).toUpperCase()
    }

    Rectangle {
        id: plate
        anchors.fill: parent
        radius: Theme.radius
        color: Theme.surfaceAlt
        border.width: Theme.border
        border.color: Theme.line
        clip: true

        // Placeholder: two flat bands whose split point comes from the hash.
        // Flat colour, no gradient -- it should read as a label, not as art.
        Column {
            anchors.fill: parent
            visible: cover.status !== Image.Ready

            Rectangle {
                width: parent.width
                height: parent.height * (0.34 + (root.hash % 20) / 100)
                color: {
                    var palette = [Theme.blueWash, Theme.purpleWash, Theme.greenWash]
                    return palette[root.hash % 3]
                }
            }
            Rectangle {
                width: parent.width
                height: parent.height - parent.children[0].height
                color: Theme.surface
            }
        }

        Text {
            anchors.centerIn: parent
            visible: cover.status !== Image.Ready
            text: root.initials
            font.family: Theme.monoFamily
            font.pixelSize: Math.max(13, root.height * 0.3)
            font.weight: Font.Bold
            font.letterSpacing: 1
            color: {
                var palette = [Theme.blue, Theme.purple, Theme.green]
                return palette[root.hash % 3]
            }
            opacity: 0.85
        }

        Image {
            id: cover
            anchors.fill: parent
            source: root.source
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            cache: true
            visible: status === Image.Ready
        }
    }

    // Unmatched marker: a small notch in the top-right, purple like everything
    // else that wants a second look.
    Rectangle {
        visible: !root.matched
        anchors.top: parent.top
        anchors.right: parent.right
        width: 7
        height: 7
        color: Theme.purple
        opacity: 0.9
    }
}
