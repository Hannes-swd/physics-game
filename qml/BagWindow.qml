// Inventar (E), Herstellen (C) und Bauen (B) in einem Fenster im Stil "Wald" wie das Hauptmenü (qml/Wald/).
// Links drei Holzmarken (wie die Werkzeugleiste im Spiel), rechts der gewählte Bereich. "bag" ist das C++-Objekt aus
// src/Menu.cpp. Tasten: die des offenen Bereichs (oder Esc) schließt, die eines anderen wechselt dorthin.
//  - Inventar: Raster, Teile als Rechtecke darin (Fläche ~ Gewicht, Form ~ die des Teils), Gewichtszeile. Rechte
//    Maustaste auf ein Teil (oder mit rechts aus dem Fenster ziehen und loslassen): herausnehmen, es hängt dann vor der
//    Figur und wird mit links gesetzt.
//  - Herstellen / Bauen: ein Kasten je Vorlage mit Zeichnung, Name und was es braucht. Klick: das Hologramm hängt dann
//    vor der Figur und wird mit links gesetzt.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Kein eigenes Fenster mit Rahmen: deckt das Spielfenster ganz ab (src/Menu.cpp setzt die Größe), das Spiel scheint
// leicht grau abgedunkelt durch, der Inhalt steht fest in der Mitte
ApplicationWindow {
    id: window
    width: 1000
    height: 680
    visible: false
    title: areas[bag.tab].label
    flags: Qt.Tool | Qt.FramelessWindowHint
    color: "transparent"
    background: Rectangle { color: "#59000000" }

    readonly property var areas: [
        { label: "Inventar", icon: "M7 8V6.5A5 5 0 0 1 17 6.5V8M5 9.5A1.5 1.5 0 0 1 6.5 8h11A1.5 1.5 0 0 1 19 9.5V19a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2ZM9 13.5h6v3h-6Z" },
        { label: "Herstellen", icon: "M4.5 19.5L12 12M10.4 9.4L13.4 6.4L17.6 10.6L14.6 13.6ZM13 20.5h7.5M14.2 20.5V18h5v2.5" },
        { label: "Bauen", icon: "M3.5 11L12 4L20.5 11M5.5 9.4V20h13V9.4M10 20v-5h4v5M5.5 14.5h4M14 14.5h4.5" },
        // nur mit Cheats: jedes Ding zum Nehmen (ein Kasten mit Plus)
        { label: "Alles", icon: "M4.5 7.5h15v12h-15ZM4.5 7.5L7 4.5h10l2.5 3M12 10.5v6M9 13.5h6" }
    ]

    onClosing: (close) => {  // Fenster schließen = zu
        close.accepted = false
        bag.close()
    }
    onActiveChanged: if (active) keyCatcher.forceActiveFocus()

    // Tasten nach der Belegung (Einstellungen -> Steuerung)
    Item {
        id: keyCatcher
        focus: true
        Keys.onPressed: (e) => {
            if (e.key === Qt.Key_Escape) {
                e.accepted = true
                bag.close()
                return
            }
            const t = bag.keyTab(e.key, e.modifiers)
            if (t < 0) return
            e.accepted = true
            if (t === bag.tab) bag.close()
            else bag.switchTo(t)
        }
    }

    Pane {
        anchors.centerIn: parent
        // wächst mit dem Spielfenster wie das Hauptmenü (ausgelegt für 1000 x 680)
        readonly property real zoom: Math.max(1, Math.min(parent.width / 1000, parent.height / 680))
        scale: zoom
        width: Math.min(900, parent.width / zoom)
        height: Math.min(600, parent.height / zoom)
        padding: 22

        RowLayout {
            anchors.fill: parent
            spacing: 18

            // ---- Seitenleiste: die drei Bereiche
            ColumnLayout {
                Layout.alignment: Qt.AlignTop
                Layout.topMargin: 6
                spacing: 14
                Repeater {
                    model: window.areas
                    ColumnLayout {
                        required property var modelData
                        required property int index
                        visible: index < 3 || bag.cheats
                        spacing: 4
                        WoodToken {
                            Layout.alignment: Qt.AlignHCenter
                            icon: parent.modelData.icon
                            selected: bag.tab === parent.index
                            keyText: bag.keyTexts[parent.index] || ""
                            onClicked: bag.switchTo(parent.index)
                        }
                        Label {
                            Layout.alignment: Qt.AlignHCenter
                            text: parent.modelData.label
                            font.pixelSize: 13
                            font.bold: bag.tab === parent.index
                            color: bag.tab === parent.index ? "#fff4dc" : "#cdbb92"
                        }
                    }
                }
            }
            Rectangle {
                Layout.fillHeight: true
                width: 2
                color: "#1b1008"
                opacity: 0.7
            }

            // ---- der gewählte Bereich
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        text: window.areas[bag.tab].label + (bag.tab === 3 ? " (Cheats)" : "")
                        font.pixelSize: 20
                        font.bold: true
                        color: "#fff4dc"
                    }
                    Item { Layout.fillWidth: true }
                    Label {
                        visible: bag.tab === 0 || bag.tab === 3
                        text: "Gewicht " + bag.summary
                        color: "#cdbb92"
                    }
                }

                StackLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    currentIndex: bag.tab === 0 ? 0 : bag.tab === 3 ? 2 : 1

                    // Inventar: die Fläche so groß wie möglich, Zellen quadratisch
                    Item {
                        id: area
                        readonly property real cell: Math.min(width / bag.cols, height / bag.rows)

                        Rectangle {
                            id: grid
                            width: area.cell * bag.cols
                            height: area.cell * bag.rows
                            color: "#5a1b1008"
                            border.width: 2
                            border.color: "#1b1008"

                            Repeater {  // Rasterlinien
                                model: bag.cols - 1
                                Rectangle {
                                    required property int index
                                    x: (index + 1) * area.cell
                                    width: 1
                                    height: grid.height
                                    color: "#1b1008"
                                    opacity: 0.55
                                }
                            }
                            Repeater {
                                model: bag.rows - 1
                                Rectangle {
                                    required property int index
                                    y: (index + 1) * area.cell
                                    width: grid.width
                                    height: 1
                                    color: "#1b1008"
                                    opacity: 0.55
                                }
                            }

                            Repeater {
                                model: bag.items
                                delegate: Rectangle {
                                    id: box
                                    required property var modelData
                                    x: modelData.x * area.cell + 2
                                    y: modelData.y * area.cell + 2
                                    width: modelData.w * area.cell - 4
                                    height: modelData.h * area.cell - 4
                                    radius: 4
                                    border.width: 1
                                    border.color: "#24150a"
                                    gradient: Gradient {
                                        GradientStop { position: 0; color: mouse.containsMouse ? "#b5854f" : "#9b6c3f" }
                                        GradientStop { position: 1; color: mouse.containsMouse ? "#7d5634" : "#6b4422" }
                                    }
                                    clip: true
                                    readonly property bool showName: height > 40

                                    // Zeichnung des Teils (vom Spiel ausgerechnet, Flächen von hinten nach vorne)
                                    Canvas {
                                        id: picture
                                        anchors.fill: parent
                                        anchors.margins: 3
                                        anchors.bottomMargin: box.showName ? 16 : 3
                                        onWidthChanged: requestPaint()
                                        onHeightChanged: requestPaint()
                                        onPaint: {
                                            const ctx = getContext("2d")
                                            ctx.reset()
                                            ctx.lineWidth = 0.6
                                            ctx.lineJoin = "round"
                                            for (const p of box.modelData.polys) {
                                                const pts = p.pts
                                                ctx.beginPath()
                                                ctx.moveTo(pts[0] * width, pts[1] * height)
                                                for (let k = 2; k < pts.length; k += 2) ctx.lineTo(pts[k] * width, pts[k + 1] * height)
                                                ctx.closePath()
                                                ctx.fillStyle = p.color
                                                ctx.fill()
                                                ctx.strokeStyle = Qt.darker(p.color, 1.6)
                                                ctx.stroke()
                                            }
                                        }
                                        Connections {
                                            target: bag
                                            function onInventoryChanged() { picture.requestPaint() }
                                        }
                                    }

                                    Label {
                                        visible: box.showName
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.bottom: parent.bottom
                                        anchors.margins: 3
                                        text: box.modelData.name
                                        font.pixelSize: 11
                                        elide: Text.ElideRight
                                        horizontalAlignment: Text.AlignHCenter
                                    }

                                    MouseArea {
                                        id: mouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        acceptedButtons: Qt.RightButton
                                        onReleased: bag.takeOut(box.modelData.key)
                                    }
                                    ToolTip.visible: mouse.containsMouse
                                    ToolTip.text: box.modelData.name + " – " + box.modelData.info
                                }
                            }
                        }
                    }

                    // Herstellen / Bauen: ein Kasten je Vorlage
                    GridView {
                        id: tiles
                        clip: true
                        cellWidth: Math.floor(width / Math.max(1, Math.floor(width / 166)))
                        cellHeight: 196
                        model: bag.tiles
                        ScrollBar.vertical: ScrollBar {}
                        // beim Öffnen: zum zuletzt gewählten Kasten
                        Connections {
                            target: bag
                            function onCatalogChanged() { Qt.callLater(() => tiles.positionViewAtIndex(Math.max(0, bag.current), GridView.Contain)) }
                        }

                        delegate: Item {
                            id: tile
                            required property var modelData
                            required property int index
                            width: tiles.cellWidth
                            height: tiles.cellHeight
                            readonly property bool chosen: index === bag.current

                            Rectangle {
                                anchors.fill: parent
                                anchors.margins: 5
                                radius: 6
                                border.width: tile.chosen ? 2 : 1
                                border.color: tile.chosen ? "#b6e07a" : "#1b1008"
                                gradient: Gradient {
                                    GradientStop { position: 0; color: tileMouse.containsMouse ? "#7d5634" : "#6b4422" }
                                    GradientStop { position: 1; color: tileMouse.containsMouse ? "#5a3a1e" : "#4a2f1a" }
                                }
                                Rectangle {  // heller Schein am oberen Rand
                                    x: 6
                                    y: 1
                                    width: parent.width - 12
                                    height: 1
                                    color: "#c9975c"
                                    opacity: 0.35
                                }

                                // Zeichnung: die Flächen nacheinander füllen (die hinteren zuerst)
                                Canvas {
                                    id: tilePic
                                    x: 6
                                    y: 6
                                    width: parent.width - 12
                                    height: 112
                                    onWidthChanged: requestPaint()
                                    onPaint: {
                                        const ctx = getContext("2d")
                                        ctx.reset()
                                        ctx.lineWidth = 0.6
                                        ctx.lineJoin = "round"
                                        for (const p of tile.modelData.polys) {
                                            const pts = p.pts
                                            ctx.beginPath()
                                            ctx.moveTo(pts[0] * width, pts[1] * height)
                                            for (let k = 2; k < pts.length; k += 2) ctx.lineTo(pts[k] * width, pts[k + 1] * height)
                                            ctx.closePath()
                                            ctx.fillStyle = p.color
                                            ctx.fill()
                                            ctx.strokeStyle = Qt.darker(p.color, 1.6)
                                            ctx.stroke()
                                        }
                                    }
                                    Connections {
                                        target: bag
                                        function onCatalogChanged() { tilePic.requestPaint() }
                                    }
                                }

                                Column {
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.bottom: parent.bottom
                                    anchors.margins: 7
                                    spacing: 2
                                    Label {
                                        width: parent.width
                                        text: tile.modelData.label
                                        color: "#fff4dc"
                                        elide: Text.ElideRight
                                        horizontalAlignment: Text.AlignHCenter
                                    }
                                    Label {
                                        width: parent.width
                                        text: tile.modelData.info
                                        color: "#cdbb92"
                                        font.pixelSize: 11
                                        wrapMode: Text.Wrap
                                        maximumLineCount: 2
                                        elide: Text.ElideRight
                                        horizontalAlignment: Text.AlignHCenter
                                    }
                                }
                            }

                            MouseArea {
                                id: tileMouse
                                anchors.fill: parent
                                anchors.margins: 5
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: bag.choose(tile.index)
                            }
                            ToolTip.visible: tileMouse.containsMouse && tile.modelData.info !== ""
                            ToolTip.delay: 400
                            ToolTip.text: tile.modelData.label + " – " + tile.modelData.info
                        }
                    }

                    // Alles (nur mit Cheats): suchen, auswählen, Gewicht und Anzahl wählen, ins Inventar
                    ColumnLayout {
                        id: all
                        spacing: 8
                        property var chosen: null
                        readonly property var shown: bag.giveList.filter(e => {
                            const q = search.text.trim().toLowerCase()
                            return q === "" || e.label.toLowerCase().includes(q) || e.id.toLowerCase().includes(q) || e.group.toLowerCase().includes(q)
                        })

                        TextField {
                            id: search
                            Layout.fillWidth: true
                            placeholderText: "Suchen … (z. B. Seil, Apfel, Eiche, Stahl)"
                        }

                        GridView {
                            id: giveGrid
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            cellWidth: Math.floor(width / Math.max(1, Math.floor(width / 170)))
                            cellHeight: 70
                            model: all.shown
                            ScrollBar.vertical: ScrollBar {}
                            delegate: Item {
                                id: entry
                                required property var modelData
                                width: giveGrid.cellWidth
                                height: giveGrid.cellHeight
                                readonly property bool picked: all.chosen !== null && all.chosen.id === modelData.id
                                Rectangle {
                                    anchors.fill: parent
                                    anchors.margins: 4
                                    radius: 5
                                    border.width: entry.picked ? 2 : 1
                                    border.color: entry.picked ? "#b6e07a" : "#1b1008"
                                    gradient: Gradient {
                                        GradientStop { position: 0; color: entryMouse.containsMouse ? "#7d5634" : "#6b4422" }
                                        GradientStop { position: 1; color: entryMouse.containsMouse ? "#5a3a1e" : "#4a2f1a" }
                                    }
                                    // Bild: Hergestelltes als Zeichnung, Material und Essen als Kugel in ihrer Farbe
                                    Canvas {
                                        id: pic
                                        x: 6
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: 50
                                        height: 50
                                        onPaint: {
                                            const ctx = getContext("2d")
                                            ctx.reset()
                                            const polys = entry.modelData.polys
                                            if (polys && polys.length > 0) {
                                                ctx.lineWidth = 0.6
                                                ctx.lineJoin = "round"
                                                for (const p of polys) {
                                                    const pts = p.pts
                                                    ctx.beginPath()
                                                    ctx.moveTo(pts[0] * width, pts[1] * height)
                                                    for (let k = 2; k < pts.length; k += 2) ctx.lineTo(pts[k] * width, pts[k + 1] * height)
                                                    ctx.closePath()
                                                    ctx.fillStyle = p.color
                                                    ctx.fill()
                                                    ctx.strokeStyle = Qt.darker(p.color, 1.6)
                                                    ctx.stroke()
                                                }
                                                return
                                            }
                                            const c = entry.modelData.color || "#999999"
                                            const r = width * 0.4, cx = width / 2, cy = height / 2
                                            const g = ctx.createRadialGradient(cx - r * 0.35, cy - r * 0.4, r * 0.1, cx, cy, r)
                                            g.addColorStop(0, String(Qt.lighter(c, 1.6)))
                                            g.addColorStop(0.45, c)
                                            g.addColorStop(1, String(Qt.darker(c, 2.2)))
                                            ctx.fillStyle = g
                                            ctx.beginPath()
                                            ctx.arc(cx, cy, r, 0, 2 * Math.PI)
                                            ctx.fill()
                                            ctx.strokeStyle = "rgba(20, 10, 4, 0.7)"
                                            ctx.lineWidth = 1
                                            ctx.stroke()
                                        }
                                        Connections {
                                            target: bag
                                            function onGiveChanged() { pic.requestPaint() }
                                        }
                                    }
                                    Column {
                                        anchors.verticalCenter: parent.verticalCenter
                                        anchors.left: pic.right
                                        anchors.right: parent.right
                                        anchors.leftMargin: 8
                                        anchors.rightMargin: 8
                                        Label {
                                            width: parent.width
                                            text: entry.modelData.label
                                            color: "#fff4dc"
                                            elide: Text.ElideRight
                                        }
                                        Label {
                                            width: parent.width
                                            text: entry.modelData.group + " · " + entry.modelData.kg.toFixed(entry.modelData.kg < 10 ? 2 : 1) + " kg"
                                            color: "#cdbb92"
                                            font.pixelSize: 11
                                            elide: Text.ElideRight
                                        }
                                    }
                                }
                                MouseArea {
                                    id: entryMouse
                                    anchors.fill: parent
                                    anchors.margins: 4
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        all.chosen = entry.modelData
                                        weight.text = entry.modelData.kg.toFixed(entry.modelData.kg < 10 ? 2 : 1)
                                    }
                                    onDoubleClicked: bag.give(entry.modelData.id, 0, 1, "cube")  // Doppelklick: gleich eins nehmen
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            Label {
                                text: all.chosen ? all.chosen.label : "Nichts gewählt"
                                color: all.chosen ? "#fff4dc" : "#9b8f74"
                                font.bold: true
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Label { text: "Gewicht" }
                            TextField {
                                id: weight
                                Layout.preferredWidth: 90
                                horizontalAlignment: Text.AlignRight
                                enabled: all.chosen !== null
                                validator: RegularExpressionValidator { regularExpression: /[0-9]{0,6}([.,][0-9]{0,3})?/ }
                            }
                            Label { text: "kg" }
                            Label {
                                text: "Form"
                                visible: all.chosen !== null && all.chosen.group === "Material"
                            }
                            ComboBox {
                                id: form
                                visible: all.chosen !== null && all.chosen.group === "Material"
                                model: ["Würfel", "Kugel", "Brett", "Stab"]
                                Layout.preferredWidth: 130
                            }
                            Label { text: "Anzahl" }
                            SpinBox {
                                id: count
                                from: 1
                                to: 50
                                value: 1
                                enabled: all.chosen !== null
                            }
                            Button {
                                text: "Ins Inventar"
                                enabled: all.chosen !== null
                                onClicked: bag.give(all.chosen.id, parseFloat(weight.text.replace(",", ".")) || 0, count.value,
                                                    ["cube", "sphere", "plank", "rod"][form.currentIndex])
                            }
                        }
                    }
                }

                // Hinweis mit den Tasten der Bereiche
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    font.pixelSize: 13
                    color: "#cdbb92"
                    readonly property string others: [0, 1, 2].filter(i => i !== bag.tab).map(i => bag.keyTexts[i] || "").join(" / ")
                    readonly property string own: bag.tab === 3 ? "Esc schließen" : (bag.keyTexts[bag.tab] || "") + " / Esc schließen"
                    text: (bag.tab === 0 ? "Rechtsklick (oder mit rechts hinausziehen): herausnehmen – dann vor der Figur drehen und mit links setzen"
                           : bag.tab === 3 ? "Klick wählt, Gewicht und Anzahl einstellen, „Ins Inventar“ (Doppelklick: gleich eins). Mit Cheats kein Gewichtslimit"
                                           : "Klick: als Hologramm vor die Figur hängen (drehen, Maus links setzen, rechts fertig)") +
                          "  ·  " + others + " anderer Bereich  ·  " + own
                }
            }
        }
    }
}
