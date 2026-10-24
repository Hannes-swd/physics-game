// Startseite: Spielen, Mehrspieler, Einstellungen, Beenden
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Page {
    id: page
    signal playClicked()
    signal multiplayerClicked()
    signal settingsClicked()

    StackView.onActivated: menu.page = "main"

    ColumnLayout {
        anchors.centerIn: parent
        width: 320
        spacing: 10

        Label {
            text: "Werkstatt"
            Layout.alignment: Qt.AlignHCenter
        }

        // Hinweis aus der letzten Sitzung (z.B. Beitritt fehlgeschlagen)
        Label {
            visible: menu.message !== ""
            text: menu.message
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }

        Button {
            text: "Spielen"
            Layout.fillWidth: true
            onClicked: { menu.clearMessage(); page.playClicked() }
        }
        Button {
            text: "Mehrspieler"
            Layout.fillWidth: true
            onClicked: { menu.clearMessage(); page.multiplayerClicked() }
        }
        Button {
            text: "Einstellungen"
            Layout.fillWidth: true
            onClicked: { menu.clearMessage(); page.settingsClicked() }
        }
        Button {
            text: "Beenden"
            Layout.fillWidth: true
            onClicked: menu.quit()
        }
    }
}
