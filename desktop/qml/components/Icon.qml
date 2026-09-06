import QtQuick
import QtQuick.Shapes
import VitaSync.Ui

// The whole icon set, drawn as stroked vector paths on a 24x24 grid.
//
// Keeping the geometry in QML rather than in SVG assets means the icons need
// no image-format plugin, scale cleanly on every target, and can take a colour
// from a binding -- which is what lets the palette carry meaning rather than
// decoration.
Item {
    id: root

    property string name: "file"
    property color color: Theme.inkMuted
    property int size: 18
    property real weight: 1.6
    /// Icons that read better as a filled mark than an outline.
    readonly property bool filled: name === "dot" || name === "record"

    implicitWidth: size
    implicitHeight: size

    readonly property string pathData: paths[name] !== undefined ? paths[name] : paths["file"]

    readonly property var paths: ({
        // navigation
        "link": "M9.5 14.5 L14.5 9.5 M11 7.5 L13 5.5 A3.5 3.5 0 0 1 18.5 11 L16.5 13 M13 16.5 L11 18.5 A3.5 3.5 0 0 1 5.5 13 L7.5 11",
        "package": "M12 3 L20 7.2 L20 16.8 L12 21 L4 16.8 L4 7.2 Z M4 7.2 L12 11.4 L20 7.2 M12 11.4 L12 21",
        "folder": "M3.5 6.5 L10 6.5 L11.8 9 L20.5 9 L20.5 18 L3.5 18 Z",
        "grid": "M4 4 L10 4 L10 10 L4 10 Z M14 4 L20 4 L20 10 L14 10 Z M4 14 L10 14 L10 20 L4 20 Z M14 14 L20 14 L20 20 L14 20 Z",
        "queue": "M4 7 L20 7 M4 12 L20 12 M4 17 L14 17",
        "bulb": "M9.5 18.5 L14.5 18.5 M10.5 21 L13.5 21 M12 3 A6 6 0 0 1 15.5 13.8 L15.5 16 L8.5 16 L8.5 13.8 A6 6 0 0 1 12 3 Z",

        // devices
        "card": "M3 6 L21 6 L21 18 L3 18 Z M7 6 L7 10 M11 6 L11 10 M15 6 L15 10",
        "chip": "M7 7 L17 7 L17 17 L7 17 Z M10 3 L10 7 M14 3 L14 7 M10 17 L10 21 M14 17 L14 21 M3 10 L7 10 M3 14 L7 14 M17 10 L21 10 M17 14 L21 14",
        "usb": "M12 20 L12 6 M12 6 L9.5 9.5 M12 6 L14.5 9.5 M12 14 L7.5 11 L7.5 8 M12 16.5 L16.5 13.5 L16.5 10.5",
        "cart": "M6 4 L18 4 L18 20 L6 20 Z M9 8 L15 8 M9 12 L15 12",
        "vita": "M2.5 8 L21.5 8 A2 2 0 0 1 21.5 16 L2.5 16 A2 2 0 0 1 2.5 8 Z M8 10.5 L16 10.5 L16 13.5 L8 13.5 Z",

        // content
        "video": "M3.5 6.5 L14.5 6.5 L14.5 17.5 L3.5 17.5 Z M14.5 10.5 L20.5 7.5 L20.5 16.5 L14.5 13.5 Z",
        "photo": "M3.5 5.5 L20.5 5.5 L20.5 18.5 L3.5 18.5 Z M3.5 15 L8.5 10.5 L13 15 M13 15 L16 12.5 L20.5 16.5 M15.5 9 A1 1 0 1 1 15.6 9",
        "music": "M9 18 L9 6 L19 4 L19 16 M6 20.5 A3 2.5 0 1 0 9 18 M16 18.5 A3 2.5 0 1 0 19 16",
        "save": "M4 4.5 L16.5 4.5 L20 8 L20 19.5 L4 19.5 Z M8 4.5 L8 10 L15 10 L15 4.5 M7.5 14 L16.5 14 M7.5 17 L16.5 17",
        "theme": "M12 3.5 A8.5 8.5 0 1 0 12 20.5 A2.2 2.2 0 0 0 12 16 A2.2 2.2 0 0 1 12 11.6 L15 11.6 A5.5 5.5 0 0 0 12 3.5 Z M8 8 A0.9 0.9 0 1 1 8.1 8 M12.5 6.6 A0.9 0.9 0 1 1 12.6 6.6 M6.6 12.6 A0.9 0.9 0 1 1 6.7 12.6",
        "binary": "M5 4.5 L15 4.5 L19 8.5 L19 19.5 L5 19.5 Z M15 4.5 L15 8.5 L19 8.5 M8.5 13 L11 13 L11 16.5 L8.5 16.5 Z M13 13 L15.5 13 M13 16.5 L15.5 16.5",
        "file": "M5.5 3.5 L14.5 3.5 L18.5 7.5 L18.5 20.5 L5.5 20.5 Z M14.5 3.5 L14.5 7.5 L18.5 7.5",

        // actions
        "download": "M12 4 L12 15 M7.5 10.5 L12 15 L16.5 10.5 M4.5 19.5 L19.5 19.5",
        "upload": "M12 20 L12 9 M7.5 13.5 L12 9 L16.5 13.5 M4.5 4.5 L19.5 4.5",
        "trash": "M4.5 7 L19.5 7 M9.5 7 L9.5 4.5 L14.5 4.5 L14.5 7 M6.5 7 L7.5 20 L16.5 20 L17.5 7 M10.5 10.5 L10.5 16.5 M13.5 10.5 L13.5 16.5",
        "pencil": "M4 20 L4.8 16 L16 4.8 L19.2 8 L8 19.2 Z M14.5 6.5 L17.5 9.5",
        "newfolder": "M3.5 6.5 L10 6.5 L11.8 9 L20.5 9 L20.5 18 L3.5 18 Z M12 11.5 L12 15.5 M10 13.5 L14 13.5",
        "refresh": "M20 12 A8 8 0 1 1 16.5 5.4 M20.5 4 L20.5 9.5 L15 9.5",
        "close": "M6 6 L18 18 M18 6 L6 18",
        "check": "M5 12.5 L10 17.5 L19 6.5",
        "alert": "M12 3.5 L21.5 20 L2.5 20 Z M12 9.5 L12 14 M12 16.8 L12 17.2",
        "play": "M7.5 4.5 L19 12 L7.5 19.5 Z",
        "chevron": "M9.5 5.5 L16 12 L9.5 18.5",
        "chevronDown": "M5.5 9.5 L12 16 L18.5 9.5",
        "up": "M12 20 L12 5 M5.5 11.5 L12 5 L18.5 11.5",
        "select": "M4 4 L4 8 M4 4 L8 4 M20 4 L16 4 M20 4 L20 8 M4 20 L4 16 M4 20 L8 20 M20 20 L20 16 M20 20 L16 20 M8.5 8.5 L15.5 8.5 L15.5 15.5 L8.5 15.5 Z",
        "gear": "M12 8.5 A3.5 3.5 0 1 0 12.1 8.5 M12 2.5 L12 5 M12 19 L12 21.5 M2.5 12 L5 12 M19 12 L21.5 12 M5.3 5.3 L7 7 M17 17 L18.7 18.7 M18.7 5.3 L17 7 M7 17 L5.3 18.7",
        "power": "M12 3.5 L12 11 M6.8 6.8 A7.5 7.5 0 1 0 17.2 6.8",
        "search": "M11 4 A7 7 0 1 1 10.9 4 M16 16 L20.5 20.5",
        "dot": "M12 7.5 A4.5 4.5 0 1 1 11.9 7.5",
        "shield": "M12 3 L20 6 L20 12 A9 9 0 0 1 12 21 A9 9 0 0 1 4 12 L4 6 Z M8.5 12 L11 14.5 L15.5 9.5"
    })

    // Paths are authored on a 24x24 grid and scaled as a unit, so stroke
    // weight stays optically even at every size.
    // The path is scaled, not the item. An Item transform on a Shape is not
    // reliably bounded by an ancestor's clip -- it left fragments of a
    // half-scrolled row painted below the list -- whereas scaling the geometry
    // keeps the Shape honestly the size it claims to be.
    Shape {
        width: root.size
        height: root.size
        preferredRendererType: Shape.GeometryRenderer
        opacity: root.enabled ? 1 : 0.45

        ShapePath {
            strokeColor: root.color
            // Kept proportional so the stroke stays optically even across sizes.
            strokeWidth: root.weight * (root.size / 24)
            fillColor: root.filled ? root.color : "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            scale: Qt.size(root.size / 24, root.size / 24)

            PathSvg { path: root.pathData }
        }
    }
}
