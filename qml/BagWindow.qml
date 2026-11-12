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
        { label: "Bauen", icon: "M3.5 11L12 4L20.5 11M5.5 9.4V20h13V9.4M10 20v-5h4v5M5.5 14.5h4M14 14.5h4.5" }
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
                        text: window.areas[bag.tab].label
                        font.pixelSize: 20
                        font.bold: true
                        color: "#fff4dc"
                    }
                    Item { Layout.fillWidth: true }
                    Label {
                        visible: bag.tab === 0
                        text: "Gewicht " + bag.summary
                        color: "#cdbb92"
                    }
                }

                StackLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    currentIndex: bag.tab === 0 ? 0 : 1

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
                }

                // Hinweis mit den Tasten der Bereiche
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    font.pixelSize: 13
                    color: "#cdbb92"
                    readonly property string others: [0, 1, 2].filter(i => i !== bag.tab).map(i => bag.keyTexts[i] || "").join(" / ")
                    readonly property string own: (bag.keyTexts[bag.tab] || "") + " / Esc schließen"
                    text: (bag.tab === 0 ? "Rechtsklick (oder mit rechts hinausziehen): herausnehmen – dann vor der Figur drehen und mit links setzen"
                                         : "Klick: als Hologramm vor die Figur hängen (drehen, Maus links setzen, rechts fertig)") +
                          "  ·  " + others + " anderer Bereich  ·  " + own
                }
            }
        }
    }
}
