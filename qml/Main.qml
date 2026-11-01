// Hauptmenü der Werkstatt, gestaltet vom eigenen Stil "Wald" (qml/Wald/: Baumstamm-Buttons, Holzbretter, Wald).
// "menu" ist das C++-Objekt aus src/Menu.cpp: Welten, Einstellungen, Spiele im LAN, Befehle der Fernsteuerung.
// Seiten: MainPage (Start), WorldsPage (Spielen), MultiplayerPage, SettingsPage. Jede Seite meldet sich mit menu.page,
// offene Dialoge mit menu.dialog: so kann man per Befehl ("menu") auslesen, wo man ist.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window
    width: 1000
    height: 680
    visible: true
    title: "Werkstatt"

    onClosing: menu.quit()  // Fenster schließen = Spiel beenden

    // Seite öffnen: main, worlds, multiplayer, settings. immediate: ohne Übergang (Befehle, --menu-shot)
    function openPage(name, immediate) {
        if (menu.page === name && stack.depth === (name === "main" ? 1 : 2)) return
        const op = immediate ? StackView.Immediate : StackView.Transition
        stack.pop(null, op)
        if (name === "worlds") stack.push(worldsPage, {}, op)
        else if (name === "new-world") stack.push(worldsPage, {}, op).openCreate()  // Weltliste mit "Neue Welt"
        else if (name === "multiplayer") stack.push(multiplayerPage, {}, op)
        else if (name === "settings") stack.push(settingsPage, {}, op)
    }

    // Ladeseite, bis das Spielfenster da ist (Menu.cpp ruft das auf, wenn eine Welt geöffnet oder beigetreten wird)
    function showLoading(heading, name) {
        stack.push(loadingPage, { heading: heading, name: name }, StackView.Immediate)
    }

    Connections {
        target: menu
        function onPageRequested(name) { window.openPage(name, true) }
    }

    // Die Seiten sind für 1000 x 680 gebaut. Größere Fenster vergrößern alles im selben Verhältnis (statt die Seiten
    // nur breiter zu ziehen), die Fläche wird dabei so umgerechnet, dass sie das Fenster genau füllt
    Item {
        readonly property real zoom: Math.max(1, Math.min(window.width / 1000, window.height / 680))
        width: window.width / zoom
        height: window.height / zoom
        scale: zoom
        transformOrigin: Item.TopLeft

        StackView {
            id: stack
            anchors.fill: parent
            initialItem: MainPage {
                onPlayClicked: window.openPage("worlds", false)
                onMultiplayerClicked: window.openPage("multiplayer", false)
                onSettingsClicked: window.openPage("settings", false)
            }
        }
    }

    Component {
        id: worldsPage
        WorldsPage {}
    }
    Component {
        id: multiplayerPage
        MultiplayerPage {}
    }
    Component {
        id: settingsPage
        SettingsPage {}
    }
    Component {
        id: loadingPage
        LoadingPage {}
    }
}
