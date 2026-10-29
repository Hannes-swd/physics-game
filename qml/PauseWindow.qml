// Pausenfenster im Spiel (ESC im Spiel-Modus). Rohbau wie das Hauptmenü: Qt Quick Controls im Stil "Basic", noch ohne
// eigene Gestaltung. "pause" ist das C++-Objekt aus src/Menu.cpp (Weiterspielen, Modus wechseln, Beenden), "menu" ist
// dasselbe wie im Hauptmenü (Einstellungen). Seiten: Pause, Beenden, SettingsPage (dieselbe wie im Hauptmenü).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window
    width: 640
    height: 600
    visible: false
    title: "Pause"

    onClosing: (close) => {  // Fenster schließen = weiterspielen
        close.accepted = false
        pause.resume()
    }

    // Seite öffnen: pause, settings, quit (Befehl "pause ...", beim Öffnen immer pause)
    function openPage(name) {
        stack.pop(null, StackView.Immediate)
        if (name === "settings") stack.push(settingsPage, {}, StackView.Immediate)
        else if (name === "quit") stack.push(quitPage, {}, StackView.Immediate)
    }

    Connections {
        target: menu
        function onPageRequested(name) { window.openPage(name) }
    }

    // ESC: eine Seite zurück, auf der ersten weiterspielen
    Shortcut {
        sequence: "Esc"
        onActivated: {
            if (stack.depth > 1) stack.pop()
            else pause.resume()
        }
    }

    StackView {
        id: stack
        anchors.fill: parent
        initialItem: Page {
            StackView.onActivated: menu.page = "pause"

            ColumnLayout {
                anchors.centerIn: parent
                width: 320
                spacing: 10

                Label {
                    text: "Pause"
                    Layout.alignment: Qt.AlignHCenter
                }
                Button {
                    text: "Weiterspielen"
                    Layout.fillWidth: true
                    onClicked: pause.resume()
                }
                Button {
                    text: "Modus wechseln"
                    Layout.fillWidth: true
                    onClicked: pause.switchMode()
                }
                Button {
                    text: "Einstellungen"
                    Layout.fillWidth: true
                    onClicked: stack.push(settingsPage)
                }
                Button {
                    text: "Beenden"
                    Layout.fillWidth: true
                    onClicked: stack.push(quitPage)
                }
            }
        }
    }

    Component {
        id: settingsPage
        SettingsPage {}
    }

    // Beenden: zurück ins Hauptmenü oder das Programm ganz beenden
    Component {
        id: quitPage
        Page {
            StackView.onActivated: menu.page = "quit"

            ColumnLayout {
                anchors.centerIn: parent
                width: 320
                spacing: 10

                Label {
                    visible: pause.note !== ""
                    text: pause.note
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
                Button {
                    visible: pause.fromMenu
                    text: "Zurück zum Hauptmenü"
                    Layout.fillWidth: true
                    onClicked: pause.toMenu()
                }
                Button {
                    text: "Spiel beenden"
                    Layout.fillWidth: true
                    onClicked: pause.quitGame()
                }
                Button {
                    text: "Abbrechen"
                    Layout.fillWidth: true
                    onClicked: stack.pop()
                }
            }
        }
    }
}
