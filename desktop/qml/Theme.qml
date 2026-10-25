pragma Singleton
import QtQuick
import VitaSync

QtObject {
    id: theme
    readonly property bool dark: Device.darkMode
    readonly property color base:       dark ? "#0B1422" : "#FFFFFF"
    readonly property color surface:    dark ? "#111F32" : "#F7F8FA"
    readonly property color surfaceAlt: dark ? "#172B43" : "#EEF0F3"
    readonly property color raised:     dark ? "#203C59" : "#E5E8ED"
    readonly property color line:       dark ? "#233C55" : "#E1E4E9"
    readonly property color lineStrong: dark ? "#45647F" : "#A4ACB8"
    readonly property color ink:        dark ? "#F5F9FF" : "#20252D"
    readonly property color inkMuted:   dark ? "#BCCDDD" : "#505966"
    readonly property color inkFaint:   dark ? "#94ACC1" : "#657080"
    readonly property color scrim:      dark ? "#000000" : "#20252D"

    // PlayStation blue carries selection and actions. Other hues are feedback.
    readonly property color blue:       dark ? "#59B8FF" : "#0066B8"
    readonly property color blueDim:    dark ? "#176DAC" : "#DCEBFA"
    readonly property color blueWash:   dark ? "#123657" : "#EDF4FB"
    readonly property color green:      dark ? "#83D98C" : "#28723B"
    readonly property color greenDim:   dark ? "#35763D" : "#286C36"
    readonly property color greenWash:  dark ? "#183722" : "#EAF5ED"
    readonly property color purple:     dark ? "#FFBD69" : "#925000"
    readonly property color purpleDim:  dark ? "#81501D" : "#925000"
    readonly property color purpleWash: dark ? "#3B2B18" : "#FFF3DF"
    readonly property color primaryFill: "#0070D1"
    readonly property color primaryInk: "#FFFFFF"
    readonly property int controlHeight: 40
    readonly property int rowHeight:     44
    readonly property int railWidth:     208
    readonly property int labelHeight:   24
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
    readonly property int sizeDisplay: 24
    readonly property int sizeTitle:   18
    readonly property int sizeBody:    14
    readonly property int sizeSmall:   13
    readonly property int sizeMicro:   12
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
