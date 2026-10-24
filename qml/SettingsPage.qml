// Einstellungen (settings.json, gelten beim nächsten Öffnen einer Welt). Jede Änderung wird sofort gespeichert.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Page {
    id: page
    title: "Einstellungen"
    StackView.onActivated: menu.page = "settings"

    header: Label {
        text: page.title
        padding: 10
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 8

        GridLayout {
            columns: 2
            columnSpacing: 16
            rowSpacing: 8
            Layout.fillWidth: true

            Label { text: "Name im LAN" }
            TextField {
                text: menu.settings.playerName
                placeholderText: menu.settings.defaultName
                Layout.preferredWidth: 260
                onEditingFinished: menu.setSetting("playerName", text)
            }

            Label { text: "Mausempfindlichkeit" }
            RowLayout {
                Slider {
                    id: sens
                    from: 0.2
                    to: 3.0
                    stepSize: 0.05
                    value: menu.settings.mouseSensitivity
                    Layout.preferredWidth: 260
                    onMoved: menu.setSetting("mouseSensitivity", value)
                }
                Label { text: sens.value.toFixed(2) + " ×" }
            }

            Label { text: "Sichtfeld" }
            RowLayout {
                Slider {
                    id: fov
                    from: 40
                    to: 100
                    stepSize: 1
                    value: menu.settings.fov
                    Layout.preferredWidth: 260
                    onMoved: menu.setSetting("fov", value)
                }
                Label { text: fov.value.toFixed(0) + " °" }
            }

            Label { text: "Schatten" }
            CheckBox {
                checked: menu.settings.shadows
                onToggled: menu.setSetting("shadows", checked)
            }

            Label { text: "Kanten" }
            CheckBox {
                checked: menu.settings.edges
                onToggled: menu.setSetting("edges", checked)
            }

            Label { text: "Raster" }
            CheckBox {
                checked: menu.settings.grid
                onToggled: menu.setSetting("grid", checked)
            }

            Label { text: "Automatisch speichern" }
            RowLayout {
                SpinBox {
                    id: autosave
                    from: 0
                    to: 60
                    value: menu.settings.autosaveMinutes
                    onValueModified: menu.setSetting("autosaveMinutes", value)
                }
                Label { text: autosave.value === 0 ? "nie" : "alle " + autosave.value + " min" }
            }

            Label { text: "Welten liegen in" }
            RowLayout {
                Label {
                    text: menu.worldsFolder
                    elide: Text.ElideMiddle
                    Layout.maximumWidth: 420
                }
                Button {
                    text: "Ordner öffnen"
                    onClicked: menu.openFolder(menu.worldsFolder)
                }
            }
        }

        Item { Layout.fillHeight: true }

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
