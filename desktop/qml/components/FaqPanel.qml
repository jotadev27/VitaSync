import QtQuick
import VitaSync
import VitaSync.Ui

// The lightbulb panel: the one place in VitaSync where prose is allowed.
// Everything the rest of the interface refuses to spell out lives here, as
// question and answer, and only when the user asks for it.
Item {
    id: root

    property bool open: false

    anchors.fill: parent
    visible: open || scrim.opacity > 0

    Rectangle {
        id: scrim
        anchors.fill: parent
        color: Theme.base
        opacity: root.open ? 0.72 : 0
        Behavior on opacity { NumberAnimation { duration: Theme.normal } }

        // Only the header's close button dismisses the panel. The scrim
        // still has to swallow taps -- otherwise they would fall through to
        // whatever screen sits behind this modal overlay -- it just must not
        // treat one as a request to close.
        TapHandler {}
    }

    Rectangle {
        id: panel
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: Math.min(430, root.width)
        color: Theme.surface
        x: root.open ? 0 : width

        Behavior on x { NumberAnimation { duration: Theme.normal; easing.type: Easing.OutCubic } }

        Rectangle {
            anchors.left: parent.left
            width: Theme.border
            height: parent.height
            color: Theme.line
        }

        Item {
            id: header
            width: parent.width
            height: 56

            Row {
                anchors.left: parent.left
                anchors.leftMargin: Theme.gapLarge
                anchors.verticalCenter: parent.verticalCenter
                spacing: 10

                Icon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: "bulb"
                    size: 17
                    color: Theme.blue
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "QUESTIONS"
                    font.family: Theme.sansFamily
                    font.pixelSize: Theme.sizeMicro
                    font.weight: Font.Bold
                    font.letterSpacing: 1.6
                    color: Theme.blue
                }
            }

            AppButton {
                anchors.right: parent.right
                anchors.rightMargin: Theme.gap
                anchors.verticalCenter: parent.verticalCenter
                variant: "quiet"
                iconName: "close"
                onClicked: root.open = false
            }

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: Theme.border
                color: Theme.line
            }
        }

        ListView {
            id: list
            anchors.top: header.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: footer.top
            anchors.margins: Theme.gapLarge
            clip: true
            spacing: 2

            model: ListModel {
                ListElement {
                    question: "How do I connect?"
                    answer: "On the Vita, open VitaShell and press SELECT to start its FTP server. The screen shows an address and a port — type those into Link and press Connect. Both devices have to be on the same Wi-Fi network. Nothing else is needed, and nothing leaves your network."
                }
                ListElement {
                    question: "Where does a dropped file go?"
                    answer: "Packages are staged in ux0:/vpk on the card you have selected. Videos go to ux0:/video, photos to ux0:/picture, music to ux0:/music — those are the folders the Vita's own apps read from. Themes go to ux0:/customtheme or ux0:VitaShell/theme depending on which kind they are. Every staged item shows its destination before you press Start."
                }
                ListElement {
                    question: "Why do I still press X on the Vita?"
                    answer: "Because no jailbroken Vita exposes a remote install command. VitaShell's FTP server moves files; the actual install is done by VitaShell itself and needs the extended-permissions prompt confirmed on the device. Any tool claiming otherwise is either shipping its own on-device helper or simulating button presses. This app uploads the package, checks it landed intact, and then tells you it is ready — the last tap is yours."
                }
                ListElement {
                    question: "What is the AWAKE badge?"
                    answer: "It appears when the optional vitacompanion plugin is running on your Vita, which listens on port 1338. With it, the app can hold the console awake for the length of a long upload and open VitaShell for you. Without it everything still works — you just keep the Vita from sleeping yourself."
                }
                ListElement {
                    question: "How is a transfer verified?"
                    answer: "Before a package is uploaded its SHA-256 is computed locally. After the upload the app asks the device how large the stored file is and compares. FTP offers no remote hash, so a size match is the strongest end-to-end check available — the app reports exactly which of the two it managed, and never claims more."
                }
                ListElement {
                    question: "Why is a game marked unmatched?"
                    answer: "The purple notch means the Title ID was not in the offline database. The name and Title ID shown still come from the package's own param.sfo, so they are correct — only the catalogue entry is missing. Installing works normally."
                }
                ListElement {
                    question: "How do I get real box art?"
                    answer: "Point the app at a folder of cover images named by Title ID — PCSE00001.png and so on. Any Vita cover pack laid out that way is indexed on the spot, including subfolders. The setting is on the Link screen and is remembered."
                }
                ListElement {
                    question: "How does selecting many files work?"
                    answer: "Drag across empty space in the file list to sweep a selection box over rows. Click sets one, ctrl-click adds, shift-click extends a run. The count and total size sit above the toolbar, and every batch action reads that same selection."
                }
                ListElement {
                    question: "Can I download a whole folder?"
                    answer: "Yes. Select it and press Download. The app walks the folder on the Vita first so it knows the real file count and total, then mirrors the whole tree into your download folder, subfolders and all. Empty folders are recreated too, so a savedata copy keeps its shape."
                }
                ListElement {
                    question: "Does Browse show the real folder?"
                    answer: "Yes. The app moves the Vita's own working directory to the folder you clicked and then asks for a listing of where it is, rather than asking for a named folder. That distinction matters on a Vita: its FTP server accepts a folder name on a listing request but ignores it when it cannot resolve it, answering with wherever it happened to be instead — which showed every folder as the same list of memory cards. If a folder cannot be opened you now get an error, never somebody else's contents."
                }
                ListElement {
                    question: "How do I install a theme?"
                    answer: "Drop it on, same as a game. A theme is a folder rather than a single file, so the app unpacks the archive, sends every file, and tells you which tool finishes the job — because that differs between the two kinds of Vita theme."
                }
                ListElement {
                    question: "Can I install a game that is already unpacked?"
                    answer: "Yes. Drag the folder itself — the one named by the game's serial, like PCSE00001. The app reads the Title ID out of the folder's own param.sfo, sends the whole tree to ux0:/app under that ID, and then tells you to use Refresh LiveArea in VitaShell, which is what makes the console notice it. There is no install step and no X to press: the folder is already in its installed shape."
                }
                ListElement {
                    question: "Why are there two kinds of theme?"
                    answer: "They are unrelated things that share a name. A PS Vita home-screen theme is a folder containing theme.xml; it goes to ux0:/customtheme and is applied with Custom Themes Manager on the device. A VitaShell skin is a folder of colors.txt and PNGs; it goes to ux0:VitaShell/theme and is chosen inside VitaShell itself with START, then Restart VitaShell. The app tells the two apart by what is inside the archive and routes each one correctly."
                }
                ListElement {
                    question: "Does this need the internet?"
                    answer: "No. There is no account, no telemetry, no update check and no online lookup. The title database ships inside the app. Every byte the app moves goes between your computer and your Vita over the local network."
                }
                ListElement {
                    question: "Are my credentials stored?"
                    answer: "VitaShell's FTP server takes any user name and no real password, so there is normally nothing to store. If you do enter one it is kept in memory for the session and is never written to the settings file."
                }
                ListElement {
                    question: "What does purple mean?"
                    answer: "Purple marks anything that wants a second look. It is never the only signal, though: an alert also gets a triangle instead of its usual icon, a notched edge instead of a solid one, and the word FAILED where it applies — so the state reads the same whether or not you separate purple from green by eye. Green means it went through, blue is where you are. For the detail behind any of it, open the panel from the icon at the top right of the title bar; it holds the exact exchange with the device."
                }
            }

            delegate: Item {
                width: list.width
                height: column.implicitHeight + 18

                property bool expanded: false

                Column {
                    id: column
                    width: parent.width
                    y: 9
                    spacing: expanded ? 8 : 0

                    Row {
                        width: parent.width
                        spacing: 9

                        Icon {
                            anchors.verticalCenter: parent.verticalCenter
                            name: "chevron"
                            size: 12
                            color: expanded ? Theme.blue : Theme.inkFaint
                            rotation: expanded ? 90 : 0
                            Behavior on rotation { NumberAnimation { duration: Theme.fast } }
                        }

                        Text {
                            width: parent.width - 22
                            text: question
                            font.family: Theme.sansFamily
                            font.pixelSize: Theme.sizeBody
                            font.weight: Font.Medium
                            color: expanded ? Theme.blue : Theme.ink
                            wrapMode: Text.WordWrap
                            Behavior on color { ColorAnimation { duration: Theme.fast } }
                        }
                    }

                    Text {
                        visible: expanded
                        x: 21
                        width: parent.width - 21
                        text: answer
                        font.family: Theme.sansFamily
                        font.pixelSize: Theme.sizeSmall
                        color: Theme.inkMuted
                        lineHeight: 1.45
                        wrapMode: Text.WordWrap
                    }
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: Theme.border
                    color: Theme.line
                }

                TapHandler { onTapped: expanded = !expanded }
                HoverHandler { cursorShape: Qt.PointingHandCursor }
            }
        }

        Item {
            id: footer
            width: parent.width
            height: 40
            anchors.bottom: parent.bottom

            Rectangle {
                anchors.top: parent.top
                width: parent.width
                height: Theme.border
                color: Theme.line
            }

            Text {
                anchors.left: parent.left
                anchors.leftMargin: Theme.gapLarge
                anchors.verticalCenter: parent.verticalCenter
                text: "VitaSync " + Device.appVersion + "  ·  " + Device.metadata.titleCount + " titles offline"
                font.family: Theme.monoFamily
                font.pixelSize: Theme.sizeMicro
                color: Theme.inkFaint
            }
        }
    }
}
