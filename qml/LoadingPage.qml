// Ladeseite: erscheint sofort, wenn eine Welt geöffnet oder einem LAN-Spiel beigetreten wird, und bleibt stehen, bis
// das Spielfenster sein erstes Bild zeigt (dort geht der Ladebildschirm mit dem Fortschritt weiter).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Page {
    id: page
    property string heading: ""
    property string name: ""

    StackView.onActivated: menu.page = "loading"

    ColumnLayout {
        anchors.centerIn: parent
        width: 420
        spacing: 10

        Label {
            text: page.heading
            Layout.alignment: Qt.AlignHCenter
        }
        Label {
            visible: page.name !== ""
            text: page.name
            elide: Text.ElideMiddle
            Layout.alignment: Qt.AlignHCenter
            Layout.maximumWidth: 420
        }
        BusyIndicator {
            running: true
            Layout.alignment: Qt.AlignHCenter
        }
    }
}
