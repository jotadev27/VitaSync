import QtQuick
import VitaSync.Ui

// A list that really does stay inside its own bounds.
//
// Qt Quick Shapes are not bounded by an ancestor's scissor clip in this Qt
// build: a delegate that sits outside the viewport still paints its icons,
// which left glyph fragments scattered under every scrolling list. Rendering
// the whole thing into a layer is the one thing that bounds them.
//
// The layer needs something opaque behind it -- its untouched pixels composite
// as black rather than as nothing on some backends -- so the backdrop below is
// part of the fix, not decoration.
Item {
    id: root

    default property alias content: holder.data
    property alias view: holder

    clip: true
    layer.enabled: true

    Rectangle {
        anchors.fill: parent
        color: Theme.base
    }

    Item {
        id: holder
        anchors.fill: parent
    }
}
