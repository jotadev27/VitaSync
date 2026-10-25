import QtQuick
import VitaSync
import VitaSync.Ui
import "LucideIcons.js" as Lucide

// Keep the exact upstream artwork and render it at physical pixel resolution.
Item {
    id: root
    property string name: "file"
    property color color: Theme.inkMuted
    property int size: 20
    property real weight: 2
    implicitWidth: size
    implicitHeight: size
    Accessible.ignored: true

    SvgIcon {
        anchors.centerIn: parent
        width: root.size
        height: root.size
        svg: (Lucide.icons[root.name] || Lucide.icons.file)
                 .replace(/currentColor/g, root.color.toString())
                 .replace('stroke-width="2"', 'stroke-width="' + root.weight + '"')
    }
}
