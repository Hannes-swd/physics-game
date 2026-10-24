// Weltliste: Name und wann die Welt zuletzt geöffnet wurde, zuletzt geöffnete oben.
// Einmal klicken = auswählen (dann Spielen / Im LAN öffnen / Bearbeiten / Löschen), Doppelklick = sofort öffnen.
// Tasten: Pfeile wählen, Enter öffnet, Entf löscht.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Page {
    id: page
    title: "Spielen"

    // Auswahl über den Ordner (bleibt richtig, wenn sich die Liste ändert). Sie liegt in menu.selectedDir, damit auch
    // Befehle ("menu select 2") sie sehen und setzen können
    readonly property var current: {
        for (let i = 0; i < menu.worlds.length; ++i)
            if (menu.worlds[i].dir === menu.selectedDir) return menu.worlds[i]
        return null
    }
    readonly property bool canOpen: current !== null && !current.broken

    function open(lan) {
        if (canOpen) menu.play(current.dir, lan)
    }
    function move(step) {
        const n = menu.worlds.length
        if (n === 0) return
        let i = -1
        for (let k = 0; k < n; ++k)
            if (menu.worlds[k].dir === menu.selectedDir) i = k
        i = Math.max(0, Math.min(n - 1, i < 0 ? 0 : i + step))
        menu.selectedDir = menu.worlds[i].dir
        list.positionViewAtIndex(i, ListView.Contain)
    }

    StackView.onActivated: menu.page = "worlds"
    Component.onCompleted: {
        menu.refresh()
        list.forceActiveFocus()
    }
    Component.onDestruction: menu.dialog = ""  // die Dialoge gehören zu dieser Seite
    function openCreate() { createDialog.open() }

    Connections {  // Befehle schließen offene Dialoge (z.B. vor einem Seitenwechsel)
        target: menu
        function onCloseDialogs() {
            createDialog.close()
            editDialog.close()
            deleteDialog.close()
        }
    }

    header: Label {
        text: page.title
        padding: 10
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 8

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            focus: true
            model: menu.worlds
            ScrollBar.vertical: ScrollBar {}

            delegate: ItemDelegate {
                id: row
                required property var modelData
                width: ListView.view.width
                text: modelData.name  // wird nicht gezeigt (eigenes contentItem), aber Bedienungshilfen lesen den Namen
                highlighted: modelData.dir === menu.selectedDir
                contentItem: RowLayout {
                    Label {
                        text: row.modelData.name + (row.modelData.broken ? "  (beschädigt)" : "")
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    Label { text: row.modelData.typeText }
                    Label {
                        text: "zuletzt geöffnet: " + row.modelData.lastPlayed
                    }
                }
                onClicked: {
                    menu.selectedDir = modelData.dir
                    list.forceActiveFocus()
                }
                onDoubleClicked: {
                    menu.selectedDir = modelData.dir
                    page.open(false)
                }
            }

            Keys.onUpPressed: page.move(-1)
            Keys.onDownPressed: page.move(1)
            Keys.onReturnPressed: page.open(false)
            Keys.onEnterPressed: page.open(false)
            Keys.onDeletePressed: if (page.current) deleteDialog.open()

            Label {
                anchors.centerIn: parent
                visible: list.count === 0
                text: "Noch keine Welt. Mit „Neue Welt“ legst du eine an."
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Button {
                text: "Spielen"
                enabled: page.canOpen
                onClicked: page.open(false)
            }
            Button {
                text: "Im LAN öffnen"
                enabled: page.canOpen
                onClicked: page.open(true)
            }
            Button {
                text: "Bearbeiten"
                enabled: page.canOpen
                onClicked: editDialog.openFor(page.current)
            }
            Button {
                text: "Löschen"
                enabled: page.current !== null
                onClicked: deleteDialog.open()
            }
            Item { Layout.fillWidth: true }
            Button {
                text: "Neue Welt"
                onClicked: createDialog.open()
            }
            Button {
                text: "Zurück"
                onClicked: page.StackView.view.pop()
            }
        }
    }

    // ---------------------------------------------------------------- Neue Welt
    Dialog {
        id: createDialog
        title: "Neue Welt"
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 440
        property string error: ""

        // Welttyp: Index 0 = normal (Gelände mit Biomen), 1 = flach (Ebene wie bisher)
        readonly property bool normal: typeBox.currentIndex === 0

        function create() {
            const biome = biomeBox.currentIndex <= 0 ? "all" : menu.biomes[biomeBox.currentIndex - 1].id
            const r = menu.createWorld(nameField.text, seedField.text, normal ? "normal" : "flat", normal ? biome : "all")
            if (r.error !== "") {
                error = r.error
                return
            }
            close()
            menu.selectedDir = r.dir
            list.forceActiveFocus()
        }

        onClosed: menu.dialog = ""
        onOpened: {
            menu.dialog = "new world"
            nameField.text = "Neue Welt"
            seedField.text = menu.randomSeed()  // steht schon fest, lässt sich aber ändern
            typeBox.currentIndex = 0
            biomeBox.currentIndex = 0
            error = ""
            nameField.selectAll()
            nameField.forceActiveFocus()
        }

        ColumnLayout {
            anchors.fill: parent
            Label { text: "Name" }
            TextField {
                id: nameField
                Layout.fillWidth: true
                onAccepted: createDialog.create()
            }
            Label { text: "Seed" }
            RowLayout {
                Layout.fillWidth: true
                TextField {
                    id: seedField
                    Layout.fillWidth: true
                    placeholderText: "Zahl oder Text (leer = zufällig)"
                    selectByMouse: true
                    onAccepted: createDialog.create()
                }
                Button {
                    text: "Zufällig"
                    onClicked: seedField.text = menu.randomSeed()
                }
            }
            Label { text: "Welttyp" }
            ComboBox {
                id: typeBox
                Layout.fillWidth: true
                model: ["Normal (Gelände mit Biomen)", "Flach (Ebene wie bisher)"]
            }
            Label {
                text: "Biome"
                enabled: createDialog.normal
            }
            ComboBox {
                id: biomeBox
                Layout.fillWidth: true
                enabled: createDialog.normal
                model: {
                    const m = ["Alle Biome"]
                    for (let i = 0; i < menu.biomes.length; ++i) m.push("Nur " + menu.biomes[i].label)
                    return m
                }
            }
            Label {
                text: "Gleicher Seed = gleiche Welt: dieselben Hügel und Biome an denselben Stellen."
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            Label {
                visible: createDialog.error !== ""
                text: createDialog.error
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
        }

        footer: DialogButtonBox {
            Button {
                text: "Erstellen"
                enabled: nameField.text.trim() !== ""
                DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
                onClicked: createDialog.create()
            }
            Button {
                text: "Abbrechen"
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
        }
    }

    // ---------------------------------------------------------------- Bearbeiten
    Dialog {
        id: editDialog
        title: "Welt bearbeiten"
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 440
        property var world: null
        property string error: ""
        onOpened: menu.dialog = "edit world"
        onClosed: menu.dialog = ""

        function openFor(w) {
            world = w
            renameField.text = w.name
            error = ""
            open()
            renameField.selectAll()
            renameField.forceActiveFocus()
        }
        function apply() {
            const dir = world.dir
            const e = menu.renameWorld(dir, renameField.text)
            if (e !== "") {
                error = e
                return
            }
            close()
            menu.selectedDir = dir
            list.forceActiveFocus()
        }

        ColumnLayout {
            anchors.fill: parent
            Label { text: "Name" }
            TextField {
                id: renameField
                Layout.fillWidth: true
                onAccepted: editDialog.apply()
            }
            Label { text: "Seed" }
            TextField {
                readOnly: true
                selectByMouse: true
                Layout.fillWidth: true
                text: editDialog.world ? editDialog.world.seed + (editDialog.world.seedText !== "" ? "  (aus „" + editDialog.world.seedText + "“)" : "") : ""
            }
            Label {
                text: editDialog.world ? "Erstellt: " + editDialog.world.created + "\nGespielt: " + editDialog.world.playTime
                                         + "\nWelt: " + editDialog.world.typeText : ""
            }
            Button {
                text: "Ordner öffnen"
                onClicked: menu.openFolder(editDialog.world.dir)
            }
            Label {
                visible: editDialog.error !== ""
                text: editDialog.error
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
        }

        footer: DialogButtonBox {
            Button {
                text: "Speichern"
                enabled: renameField.text.trim() !== ""
                DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
                onClicked: editDialog.apply()
            }
            Button {
                text: "Abbrechen"
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
        }
    }

    // ---------------------------------------------------------------- Löschen
    Dialog {
        id: deleteDialog
        title: "Welt löschen"
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 440
        property string error: ""
        onOpened: {
            error = ""
            menu.dialog = "delete world"
        }
        onClosed: menu.dialog = ""

        ColumnLayout {
            anchors.fill: parent
            Label {
                text: page.current ? "„" + page.current.name + "“ wirklich löschen?\nDie Welt kommt in den Papierkorb." : ""
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            Label {
                visible: deleteDialog.error !== ""
                text: deleteDialog.error
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
        }

        footer: DialogButtonBox {
            Button {
                text: "Löschen"
                DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
                onClicked: {
                    const e = menu.deleteWorld(page.current.dir)
                    if (e !== "") {
                        deleteDialog.error = e
                        return
                    }
                    deleteDialog.close()
                    menu.selectedDir = ""
                    list.forceActiveFocus()
                }
            }
            Button {
                text: "Abbrechen"
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
        }
    }
}
