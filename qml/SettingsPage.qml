// Einstellungen (settings.json, gelten beim nächsten Öffnen einer Welt). Jede Änderung wird sofort gespeichert.
// Dieselbe Seite steht im Pausenfenster des Spiels (PauseWindow.qml, menu.inGame): dort gilt alles sofort.
// Drei Reiter: Allgemein, Grafik, Steuerung (Tastenbelegung: Klick auf eine Taste, dann die neue drücken)
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Page {
    id: page
    title: "Einstellungen"
    property int tab: 0  // 0 Allgemein, 1 Grafik, 2 Steuerung
    readonly property var tabPages: ["settings", "graphics", "controls"]
    StackView.onActivated: menu.page = tabPages[tab]
    onTabChanged: {
        stopCapture()
        if (StackView.status === StackView.Active) menu.page = tabPages[tab]
    }

    // ---- Steuerung: welche Taste gerade neu belegt wird (Aktion, 0 / 1)
    property string capId: ""
    property int capSlot: -1
    property string capNote: ""
    function capture(id, slot) {
        capId = id
        capSlot = slot
        capNote = ""
        catcher.forceActiveFocus()
    }
    function stopCapture() {
        capId = ""
        capSlot = -1
    }

    // fängt die nächste Taste ab (auch Esc, Tab und die Kürzel des Fensters), solange auf eine gewartet wird
    Item {
        id: catcher
        Keys.onShortcutOverride: (e) => { if (page.capId !== "") e.accepted = true }
        Keys.onPressed: (e) => {
            if (page.capId === "") return
            e.accepted = true
            if (e.key === Qt.Key_Escape) {
                page.stopCapture()
                return
            }
            const name = menu.keyName(e.key, e.modifiers)
            if (name === "") {
                page.capNote = "Diese Taste geht leider nicht – bitte eine andere drücken (Esc bricht ab)"
                return
            }
            menu.setKey(page.capId, page.capSlot, name)
            page.stopCapture()
        }
    }

    header: Label {
        text: page.title
        padding: 10
        horizontalAlignment: Text.AlignHCenter
        font.pixelSize: 24
        font.bold: true
    }

    ColumnLayout {
        // auf breiten Bildschirmen nicht über die ganze Breite: in der Mitte (Steuerung etwas breiter für zwei Spalten)
        width: Math.min(parent.width - 20, page.tab === 2 ? 900 : 820)
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.margins: 10
        anchors.topMargin: 0
        spacing: 10

        TabBar {
            id: bar
            Layout.alignment: Qt.AlignHCenter
            currentIndex: page.tab
            onCurrentIndexChanged: page.tab = currentIndex
            TabButton { text: "Allgemein" }
            TabButton { text: "Grafik" }
            TabButton { text: "Steuerung" }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: page.tab

            // ---- Allgemein
            ScrollView {
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
                        Layout.fillWidth: true
                        Label {
                            text: menu.worldsFolder
                            elide: Text.ElideMiddle
                            Layout.fillWidth: true
                            Layout.maximumWidth: 420
                        }
                        Button {
                            text: "Ordner öffnen"
                            onClicked: menu.openFolder(menu.worldsFolder)
                        }
                    }
                }
            }

            // ---- Grafik (im Hauptmenü: gilt beim nächsten Öffnen einer Welt; im Spiel sofort)
            ScrollView {
                contentWidth: availableWidth
                clip: true

                GridLayout {
                    columns: 2
                    columnSpacing: 16
                    rowSpacing: 8
                    width: parent.width

                    Label {
                        visible: !menu.inGame
                        text: "Gilt beim nächsten Öffnen einer Welt."
                        color: "#cdbb92"
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

            // ---- Steuerung: Bereiche in zwei Spalten (schmal: untereinander), je Aktion bis zu zwei Tasten
            ScrollView {
                id: keyScroll
                contentWidth: availableWidth
                clip: true

                GridLayout {
                    width: keyScroll.availableWidth - 14  // Platz für die Bildlaufleiste
                    columns: width > 700 ? 2 : 1
                    columnSpacing: 44
                    rowSpacing: 0

                    Repeater {
                        model: menu.keyColumns
                        ColumnLayout {
                            required property var modelData
                            Layout.alignment: Qt.AlignTop
                            Layout.fillWidth: true
                            Layout.preferredWidth: 100  // beide Spalten gleich breit
                            spacing: 0

                            Repeater {
                                model: parent.modelData
                                delegate: Item {
                                    id: row
                                    required property var modelData
                                    readonly property bool isGroup: modelData.kind === "group"
                                    readonly property bool isKey: modelData.kind === "key"
                                    function at(list, i) { return list ? list[i] : "" }
                                    readonly property string conflict: at(modelData.conflicts, 0) !== "" ? at(modelData.conflicts, 0)
                                                                                                        : at(modelData.conflicts, 1)
                                    Layout.fillWidth: true
                                    implicitHeight: isGroup ? 40 : 34

                                    // Bereich: Überschrift in Moosgrün mit Linie
                                    Label {
                                        visible: row.isGroup
                                        anchors.left: parent.left
                                        anchors.bottom: parent.bottom
                                        anchors.bottomMargin: 5
                                        text: row.modelData.label.toUpperCase()
                                        color: "#b6e07a"
                                        font.pixelSize: 13
                                        font.letterSpacing: 1.5
                                    }
                                    Rectangle {
                                        visible: row.isGroup
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.bottom: parent.bottom
                                        anchors.bottomMargin: 2
                                        height: 1
                                        color: "#b6e07a"
                                        opacity: 0.3
                                    }

                                    // Aktion: Name, Hinweis bei doppelter Belegung, Tasten
                                    RowLayout {
                                        visible: !row.isGroup
                                        anchors.fill: parent
                                        spacing: 8
                                        Label {
                                            text: row.modelData.label
                                            elide: Text.ElideRight
                                            Layout.fillWidth: true
                                        }
                                        Label {
                                            visible: row.isKey && row.conflict !== ""
                                            text: "auch: " + row.conflict
                                            color: "#ef9a7f"
                                            font.pixelSize: 12
                                            elide: Text.ElideRight
                                            Layout.maximumWidth: 150
                                        }
                                        KeyCap {
                                            visible: !row.isKey
                                            fixed: true
                                            text: row.modelData.text || ""
                                        }
                                        Repeater {
                                            model: row.isKey ? 2 : 0
                                            KeyCap {
                                                required property int index
                                                text: row.at(row.modelData.texts, index)
                                                waiting: page.capId === row.modelData.id && page.capSlot === index
                                                bad: row.at(row.modelData.conflicts, index) !== ""
                                                onClicked: page.capture(row.modelData.id, index)
                                                onRightClicked: {
                                                    page.stopCapture()
                                                    menu.setKey(row.modelData.id, index, "")
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 16
            Label {
                visible: page.tab === 2
                text: page.capNote !== "" ? page.capNote
                                          : "Klick auf eine Taste, dann die neue drücken · Esc bricht ab · Rechtsklick entfernt sie"
                color: page.capNote !== "" ? "#ef9a7f" : "#cdbb92"
                font.pixelSize: 13
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            Item { visible: page.tab !== 2; Layout.fillWidth: true }
            Button {
                visible: page.tab === 2
                text: "Standard"
                onClicked: {
                    page.stopCapture()
                    menu.resetKeys()
                }
            }
            Button {
                text: "Zurück"
                onClicked: page.StackView.view.pop()
            }
        }
    }
}
