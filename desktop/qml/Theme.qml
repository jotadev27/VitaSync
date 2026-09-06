pragma Singleton
import QtQuick

// The visual identity, in one file.
//
// Palette is white / black / green / blue / purple plus structural greys, and
// the accents do real work: blue carries identity and navigation, green means
// "this went through", purple flags anything that needs a second look. Nothing
// is styled with a gradient wash or a drop shadow -- structure comes from
// hairlines and flat planes, the way an instrument panel does.
QtObject {
    id: theme

    // --- planes -----------------------------------------------------------
    readonly property color base:       "#06090F"
    readonly property color surface:    "#0C121C"
    readonly property color surfaceAlt: "#111A28"
    readonly property color raised:     "#162234"
    readonly property color line:       "#1C2739"
    readonly property color lineStrong: "#2B3E59"

    // --- ink --------------------------------------------------------------
    // Deliberately not pure white: #E9EFF8 against #06090F reads as clean
    // without the glare of #FFFFFF on black.
    readonly property color ink:        "#E9EFF8"
    readonly property color inkMuted:   "#8FA4C0"
    readonly property color inkFaint:   "#5A6E8C"

    // --- accents ----------------------------------------------------------
    readonly property color blue:       "#2BB8F0"
    readonly property color blueDim:    "#177FB0"
    readonly property color blueWash:   "#0E2634"
    readonly property color green:      "#3FD79B"
    readonly property color greenDim:   "#1E8C63"
    readonly property color greenWash:  "#0B2620"
    readonly property color purple:     "#A874F7"
    readonly property color purpleDim:  "#6E45AE"
    readonly property color purpleWash: "#1B1330"

    // --- metrics ----------------------------------------------------------
    // One control height everywhere. Fields, buttons, chips and list rows all
    // measure the same, which is what keeps the boxes visually even.
    readonly property int  controlHeight: 38
    readonly property int  rowHeight:     40
    readonly property int  railWidth:     220
    readonly property int  radius:        3
    readonly property int  gap:           10
    readonly property int  gapLarge:      18
    readonly property int  pad:           14
    readonly property int  border:        1

    // --- type -------------------------------------------------------------
    // Sans for labels, mono for anything the machine produced: paths, Title
    // IDs, byte counts. The split is the main typographic signal in the app.
    readonly property string sansFamily: "Inter, Roboto, Segoe UI, DejaVu Sans, sans-serif"
    readonly property string monoFamily: "JetBrains Mono, Roboto Mono, DejaVu Sans Mono, Consolas, monospace"

    readonly property int sizeDisplay: 21
    readonly property int sizeTitle:   15
    readonly property int sizeBody:    13
    readonly property int sizeSmall:   11
    readonly property int sizeMicro:   10

    // --- motion -----------------------------------------------------------
    readonly property int fast:   110
    readonly property int normal: 180

    // --- tone ---------------------------------------------------------------
    // Purple is the alert colour, but colour is never the only carrier: an
    // alert also gets its own glyph and a hatched rail rather than a solid one,
    // so it is still distinguishable when hue is not available to the reader.

    function toneColor(tone) {
        switch (tone) {
        case "ok":    return theme.green
        case "busy":  return theme.blue
        case "info":  return theme.blue
        case "warn":  return theme.purple
        case "error": return theme.purple
        default:      return theme.inkMuted
        }
    }

    /// True for the tones that mean "this needs a second look".
    function toneIsAlert(tone) {
        return tone === "warn" || tone === "error"
    }

    /// The glyph that carries the tone's meaning without relying on hue.
    function toneIcon(tone) {
        switch (tone) {
        case "ok":    return "check"
        case "warn":  return "alert"
        case "error": return "alert"
        case "busy":  return "upload"
        default:      return "dot"
        }
    }

    function accentColor(name) {
        switch (name) {
        case "blue":   return theme.blue
        case "green":  return theme.green
        case "purple": return theme.purple
        default:       return theme.inkMuted
        }
    }
}
