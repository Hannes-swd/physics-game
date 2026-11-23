// Ladeseite: erscheint sofort, wenn eine Welt geöffnet oder einem LAN-Spiel beigetreten wird, und bleibt stehen, bis
// das Spielfenster sein erstes Bild zeigt (dort geht der Ladebildschirm mit dem Fortschritt weiter, src/AppWorld.cpp).
// Beide sehen gleich aus: dunkelgrün, unten links klein was passiert und groß der Weltname, unten ein dünner Balken
// (hier unbestimmt: ein Stück läuft durch)
import QtQuick
import QtQuick.Controls

Page {
    id: page
    property string heading: ""
    property string name: ""

    StackView.onActivated: menu.page = "loading"

    background: Rectangle {
        gradient: Gradient {
            orientation: Gradient.Vertical
            GradientStop { position: 0; color: "#16221a" }
            GradientStop { position: 1; color: "#090d0a" }
        }
    }

    Column {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.leftMargin: 64
        anchors.bottomMargin: 70
        spacing: 8

        Text {
            visible: page.name !== ""
            text: page.heading.toUpperCase()
            font.family: "Segoe UI"
            font.pixelSize: 14
            font.letterSpacing: 3
            color: "#8fb35a"
        }
        Text {
            text: page.name !== "" ? page.name : page.heading
            width: Math.min(implicitWidth, page.width - 128)
            elide: Text.ElideRight
            font.family: "Segoe UI Semibold"
            font.pixelSize: 56
            color: "#f0eee4"
        }
        Text {
            text: "Wird vorbereitet …"
            font.family: "Segoe UI"
            font.pixelSize: 18
            color: "#a8bea0"
        }
    }

    Rectangle {  // Balken unten
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 4
        color: "#1e2c20"
        clip: true
        Rectangle {
            id: run
            width: parent.width * 0.3
            height: parent.height
            color: "#b6e07a"
            NumberAnimation on x {
                from: -run.width
                to: run.parent.width
                duration: 2200
                loops: Animation.Infinite
            }
        }
    }
}
