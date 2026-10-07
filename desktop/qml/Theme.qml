pragma Singleton
import QtQuick

QtObject {
    id: theme
    readonly property color base:       "#191A1C"
    readonly property color surface:    "#202123"
    readonly property color surfaceAlt: "#292A2D"
    readonly property color raised:     "#323438"
    readonly property color line:       "#333538"
    readonly property color lineStrong: "#4A4D52"
    readonly property color ink:        "#EEEEED"
    readonly property color inkMuted:   "#AFB1B5"
    readonly property color inkFaint:   "#898D93"

    // Restrained blue for actions; status colours only carry feedback.
    readonly property color blue:       "#9AB7CD"
    readonly property color blueDim:    "#49677F"
    readonly property color blueWash:   "#26313B"
    readonly property color green:      "#99B6A2"
    readonly property color greenDim:   "#5E7967"
    readonly property color greenWash:  "#27322C"
    // Existing warning token, now a muted amber instead of purple.
    readonly property color purple:     "#D0AA91"
    readonly property color purpleDim:  "#876956"
    readonly property color purpleWash: "#382C27"
    readonly property color primaryFill: "#49677F"
    readonly property color primaryInk: "#FFFFFF"
    readonly property int controlHeight: 40
    readonly property int rowHeight:     44
    readonly property int railWidth:     216
    readonly property int labelHeight:   22
    readonly property int radius:        6
    readonly property int border:        1
    readonly property int space1: 4
    readonly property int space2: 8
    readonly property int space3: 12
    readonly property int space4: 20
    readonly property int space5: 32
    readonly property int gap:      space2
    readonly property int gapLarge: space4
    readonly property int pad:      16
    readonly property string sansFamily: Qt.platform.os === "windows" ? "Segoe UI" : "Noto Sans"
    readonly property string monoFamily: Qt.platform.os === "windows" ? "Consolas" : "DejaVu Sans Mono"
    readonly property int sizeDisplay: 26
    readonly property int sizeTitle:   18
    readonly property int sizeBody:    14
    readonly property int sizeSmall:   12
    readonly property int sizeMicro:   11
    readonly property int fast:   110
    readonly property int normal: 180

    function toneColor(tone) {
        switch (tone) {
        case "ok": return theme.green
        case "busy": case "info": return theme.blue
        case "warn": case "error": return theme.purple
        default: return theme.inkMuted
        }
    }
    function toneIsAlert(tone) { return tone === "warn" || tone === "error" }
    function toneIcon(tone) {
        switch (tone) {
        case "ok": return "check"
        case "warn": case "error": return "alert"
        case "busy": return "refresh"
        default: return "dot"
        }
    }
    function accentColor(name) {
        switch (name) {
        case "blue": return theme.blue
        case "green": return theme.green
        case "purple": return theme.purple
        default: return theme.inkMuted
        }
    }
}
