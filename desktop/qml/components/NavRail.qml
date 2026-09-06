import QtQuick
import VitaSync
import VitaSync.Ui

// The left rail: identity at the top, destinations in the middle, and the
// lightbulb pinned to the bottom corner where it always is.
Rectangle {
    id: root

    property int currentIndex: 0
    property int transferBadge: 0
    property int dropBadge: 0
    /// Number of failed jobs. Drawn as a triangle rather than a counter box, so
    /// the rail says "something went wrong" by shape as well as by colour.
    property int alertBadge: 0
    signal navigate(int index)
    signal faqRequested()

    width: Theme.railWidth
    color: Theme.surface

    Rectangle {
        anchors.right: parent.right
        width: Theme.border
        height: parent.height
        color: Theme.line
    }

    Image {
        id: mark
        source: "qrc:/qt/qml/VitaSync/Ui/brand/appicon.png"
        width: 34
        height: 34
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: 15
        smooth: true
        mipmap: true
    }

    Rectangle {
        anchors.top: mark.bottom
        anchors.topMargin: 15
        anchors.horizontalCenter: parent.horizontalCenter
        width: 26
        height: Theme.border
        color: Theme.line
    }

    Column {
        id: items
        anchors.top: mark.bottom
        anchors.topMargin: 34
        width: parent.width
        spacing: 4

        Repeater {
            model: [
                { icon: "link",    label: "Link",     badge: 0 },
                { icon: "package", label: "Install",  badge: root.dropBadge },
                { icon: "folder",  label: "Browse",   badge: 0 },
                { icon: "queue",   label: "Transfer", badge: root.transferBadge }
            ]

            Item {
                id: navItem
                width: root.width
                height: 54

                readonly property bool active: root.currentIndex === index

                // Active state is an accent bar on the edge plus an accent
                // mark -- no filled pill, no shadow.
                Rectangle {
                    anchors.left: parent.left
                    width: 2
                    height: parent.height - 16
                    anchors.verticalCenter: parent.verticalCenter
                    color: Theme.blue
                    opacity: navItem.active ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: Theme.fast } }
                }

                Column {
                    anchors.centerIn: parent
                    spacing: 5

                    Icon {
                        anchors.horizontalCenter: parent.horizontalCenter
                        name: modelData.icon
                        size: 19
                        color: navItem.active ? Theme.blue
                                              : (hover.hovered ? Theme.ink : Theme.inkFaint)
                        Behavior on color { ColorAnimation { duration: Theme.fast } }
                    }

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: modelData.label
                        font.family: Theme.sansFamily
                        font.pixelSize: Theme.sizeMicro
                        font.letterSpacing: 0.5
                        color: navItem.active ? Theme.blue : Theme.inkFaint
                        Behavior on color { ColorAnimation { duration: Theme.fast } }
                    }
                }

                // Normal work: a square counter.
                Rectangle {
                    visible: modelData.badge > 0 && !(index === 3 && root.alertBadge > 0)
                    anchors.top: parent.top
                    anchors.topMargin: 8
                    anchors.right: parent.right
                    anchors.rightMargin: 12
                    width: Math.max(15, badgeText.implicitWidth + 8)
                    height: 15
                    radius: 2
                    color: Theme.green

                    Text {
                        id: badgeText
                        anchors.centerIn: parent
                        text: modelData.badge
                        font.family: Theme.monoFamily
                        font.pixelSize: Theme.sizeMicro
                        font.weight: Font.Bold
                        color: Theme.base
                    }
                }

                // Something failed: a triangle, which reads differently from
                // the counter at a glance and in greyscale.
                Icon {
                    visible: index === 3 && root.alertBadge > 0
                    anchors.top: parent.top
                    anchors.topMargin: 6
                    anchors.right: parent.right
                    anchors.rightMargin: 10
                    name: "alert"
                    size: 15
                    weight: 2.2
                    color: Theme.purple
                }

                HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: root.navigate(index) }
            }
        }
    }

    // The lightbulb. The only route to long-form text anywhere in the app.
    Item {
        id: bulbButton
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.leftMargin: 14
        anchors.bottomMargin: 14
        width: 40
        height: 40

        Rectangle {
            anchors.fill: parent
            radius: Theme.radius
            color: bulbHover.hovered ? Theme.surfaceAlt : "transparent"
            border.width: Theme.border
            border.color: bulbHover.hovered ? Theme.blue : Theme.line
            Behavior on border.color { ColorAnimation { duration: Theme.fast } }
        }

        Icon {
            anchors.centerIn: parent
            name: "bulb"
            size: 18
            color: bulbHover.hovered ? Theme.blue : Theme.inkFaint
            Behavior on color { ColorAnimation { duration: Theme.fast } }
        }

        HoverHandler { id: bulbHover; cursorShape: Qt.PointingHandCursor }
        TapHandler { onTapped: root.faqRequested() }
    }

    // A caption, not a block of text -- credit, not a signature that competes
    // with anything the app is actually trying to say.
    Text {
        anchors.left: bulbButton.right
        anchors.leftMargin: 8
        anchors.verticalCenter: bulbButton.verticalCenter
        text: "Design by jotadev27"
        font.family: Theme.sansFamily
        font.pixelSize: Theme.sizeMicro
        color: Theme.inkFaint
        elide: Text.ElideRight
        width: Math.max(0, root.width - bulbButton.x - bulbButton.width - 16 - 8)
    }
}
