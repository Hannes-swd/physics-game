// Einstellungen (settings.json, gelten beim nächsten Öffnen einer Welt). Jede Änderung wird sofort gespeichert.
// Dieselbe Seite steht im Pausenfenster des Spiels (PauseWindow.qml, menu.inGame): dort gilt alles sofort.
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

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true

        GridLayout {
            columns: 2
            columnSpacing: 16
            rowSpacing: 8
            width: parent.width

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

            Label { text: "Biomgrenzen (Linien)" }
            CheckBox {
                checked: menu.settings.biomeBorders
                onToggled: menu.setSetting("biomeBorders", checked)
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

            // ---- Grafik (im Hauptmenü: gilt beim nächsten Öffnen einer Welt; im Spiel sofort)
            Label {
                text: menu.inGame ? "Grafik" : "Grafik (gilt beim nächsten Öffnen einer Welt)"
                topPadding: 12
                Layout.columnSpan: 2
            }

            Label { text: "Qualität" }
            ComboBox {
                // Niedrig .. Ultra setzen alle Werte darunter; ändert man einen davon, steht hier "Eigene"
                model: ["Niedrig", "Mittel", "Hoch", "Ultra", "Eigene"]
                currentIndex: menu.settings.graphicsPreset < 0 ? 4 : menu.settings.graphicsPreset
                Layout.preferredWidth: 260
                onActivated: (index) => { if (index < 4) menu.setSetting("graphicsPreset", index) }
            }

            Label { text: "Kantenglättung" }
            ComboBox {
                model: ["Aus", "2×", "4×", "8×"]
                currentIndex: [1, 2, 4, 8].indexOf(menu.settings.msaa)
                Layout.preferredWidth: 260
                onActivated: (index) => menu.setSetting("msaa", [1, 2, 4, 8][index])
            }

            Label { text: "Supersampling (schärfere Flächen)" }
            CheckBox {
                checked: menu.settings.supersampling
                onToggled: menu.setSetting("supersampling", checked)
            }

            Label { text: "Schattenqualität" }
            ComboBox {
                model: ["Niedrig", "Mittel", "Hoch", "Ultra"]
                currentIndex: menu.settings.shadowQuality
                Layout.preferredWidth: 260
                onActivated: (index) => menu.setSetting("shadowQuality", index)
            }

            Label { text: "Sichtweite" }
            RowLayout {
                Slider {
                    id: view
                    from: 0.5
                    to: 2.0
                    stepSize: 0.1
                    value: menu.settings.viewDistance
                    Layout.preferredWidth: 260
                    onMoved: menu.setSetting("viewDistance", value)
                }
                Label { text: Math.round(view.value * 100) + " %" }
            }

            Label { text: "Gras und Blumen" }
            RowLayout {
                Slider {
                    id: plants
                    from: 0
                    to: 1.5
                    stepSize: 0.1
                    value: menu.settings.plants
                    Layout.preferredWidth: 260
                    onMoved: menu.setSetting("plants", value)
                }
                Label { text: plants.value < 0.05 ? "aus" : Math.round(plants.value * 100) + " %" }
            }

            Label { text: "VSync (Bildrate an den Bildschirm binden)" }
            CheckBox {
                checked: menu.settings.vsync
                onToggled: menu.setSetting("vsync", checked)
            }
        }
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
