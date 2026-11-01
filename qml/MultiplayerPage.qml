// Mehrspieler im LAN: offene Spiele im Netz (werden gesucht, solange die Seite offen ist) oder Adresse eintippen.
// Einmal klicken = auswählen, Doppelklick = sofort beitreten. Eine eigene Welt öffnet man für andere in der Weltliste
// ("Im LAN öffnen").
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Page {
    id: page
    title: "Mehrspieler"
    property string selectedAddress: ""

    function join(address) {
        menu.setSetting("playerName", nameField.text)
        menu.join(address)
    }

    StackView.onActivated: menu.page = "multiplayer"
    Component.onCompleted: menu.setSearching(true)
    Component.onDestruction: menu.setSearching(false)

    header: Label {
        text: page.title
        padding: 10
        horizontalAlignment: Text.AlignHCenter
        font.pixelSize: 24
        font.bold: true
    }

    ColumnLayout {
        // auf breiten Bildschirmen nicht über die ganze Breite: in der Mitte, höchstens 900 breit
        width: Math.min(parent.width - 20, 900)
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.margins: 10
        spacing: 8

        RowLayout {
            Label { text: "Dein Name" }
            TextField {
                id: nameField
                text: menu.settings.playerName
                placeholderText: menu.settings.defaultName
                Layout.preferredWidth: 260
                onEditingFinished: menu.setSetting("playerName", text)
            }
        }

        Label { text: "Offene Spiele im Netz" }
        ListView {
            id: games
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: menu.lanGames
            ScrollBar.vertical: ScrollBar {}

            delegate: ItemDelegate {
                id: row
                required property var modelData
                width: ListView.view.width
                text: modelData.name  // wird nicht gezeigt (eigenes contentItem), aber Bedienungshilfen lesen den Namen
                highlighted: modelData.address === page.selectedAddress
                contentItem: RowLayout {
                    Label {
                        text: row.modelData.name
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    Label { text: row.modelData.address }
                    Label { text: row.modelData.players + " Spieler" }
                }
                onClicked: {
                    page.selectedAddress = modelData.address
                    addressField.text = modelData.address
                }
                onDoubleClicked: page.join(modelData.address)
            }

            Label {
                anchors.centerIn: parent
                visible: games.count === 0
                text: "Suche im Netz ... (noch kein offenes Spiel gefunden)"
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Label { text: "Adresse" }
            TextField {
                id: addressField
                Layout.fillWidth: true
                placeholderText: "z. B. 192.168.1.20 (anderer Port: 192.168.1.20:7790)"
                onAccepted: if (text.trim() !== "") page.join(text)
            }
            Button {
                text: "Beitreten"
                enabled: addressField.text.trim() !== ""
                onClicked: page.join(addressField.text)
            }
        }

        Label {
            text: "Eine eigene Welt für andere öffnen: unter „Spielen“ die Welt auswählen und „Im LAN öffnen“."
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }

        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            Button {
                text: "Zurück"
                onClicked: page.StackView.view.pop()
            }
        }
    }
}
