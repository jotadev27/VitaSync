pragma Singleton
import QtQuick

// The visual identity, in one file.
//
// The reference object is an instrument panel, not a dashboard: enamel on
// steel, read at a glance, nothing glowing for its own sake. Palette is
// white / black / green / blue / purple plus structural greys, and the
// accents do real work -- blue carries identity and navigation, green means
// "this went through", purple flags anything that needs a second look.
// Structure comes from hairlines, plane changes and a single accent edge,
// never from a gradient wash or a drop shadow.
QtObject {
    id: theme

    // --- planes -----------------------------------------------------------
    // Two real tiers, not one fill repeated: `base` is the recessed ground the
    // app sits in, `surface` is a panel raised out of it. A neutral graphite
    // rather than a blue-black, so the blue accents read as blue instead of
    // dissolving into the field behind them.
    readonly property color base:       "#0C0E12"
    readonly property color surface:    "#14171D"
    readonly property color surfaceAlt: "#1A1E26"
    readonly property color raised:     "#222833"
    readonly property color line:       "#262C36"
    readonly property color lineStrong: "#39424F"

    // --- ink --------------------------------------------------------------
    // Deliberately not pure white: #E6EAF0 against #0C0E12 reads as clean
    // without the glare of #FFFFFF on black.
    readonly property color ink:        "#E6EAF0"
    readonly property color inkMuted:   "#9AA5B4"
    readonly property color inkFaint:   "#626D7C"

    // --- accents ----------------------------------------------------------
    // Enamel, not LED. Each accent is pulled down in chroma so that only one
    // thing on screen is ever the brightest thing on screen.
    readonly property color blue:       "#4A90C8"
    readonly property color blueDim:    "#2E5F86"
    readonly property color blueWash:   "#121E28"
    readonly property color green:      "#4A9E70"
    readonly property color greenDim:   "#2C6547"
    readonly property color greenWash:  "#101F18"
    readonly property color purple:     "#8267B8"
    readonly property color purpleDim:  "#55427A"
    readonly property color purpleWash: "#171325"

    // --- metrics ----------------------------------------------------------
    // One control height everywhere. Fields, buttons, chips and list rows all
    // measure the same, which is what keeps the boxes visually even.
    readonly property int  controlHeight: 38
    readonly property int  rowHeight:     40
    readonly property int  railWidth:     200
    /// The label line above a text box. Shared so a control beside a field can
    /// offset itself by exactly the same amount the field does.
    readonly property int  labelHeight:   18
    // Controls get a small radius; large planes get none. A panel that is
    // rounded on every corner reads as a card, which is the look this is not.
    readonly property int  radius:        2
    readonly property int  border:        1

    // --- spacing ----------------------------------------------------------
    // A 4px scale rather than three ad-hoc numbers, so vertical rhythm is
    // deliberate instead of accidental.
    readonly property int  space1: 4
    readonly property int  space2: 8
    readonly property int  space3: 12
    readonly property int  space4: 20
    readonly property int  space5: 32

    readonly property int  gap:      space2
    readonly property int  gapLarge: space4
    readonly property int  pad:      space3

    // --- type -------------------------------------------------------------
    // Sans for labels, mono for anything the machine produced: paths, Title
    // IDs, byte counts. The split is the main typographic signal in the app.
    readonly property string sansFamily: "Inter, Roboto, Segoe UI, DejaVu Sans, sans-serif"
    readonly property string monoFamily: "JetBrains Mono, Roboto Mono, DejaVu Sans Mono, Consolas, monospace"

    readonly property int sizeDisplay: 24
    readonly property int sizeTitle:   16
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
