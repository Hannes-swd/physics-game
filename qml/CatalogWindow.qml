// Auswahl im Spiel: Bauvorlagen (B) und Rezepte zum Herstellen (C), im Stil "Wald" wie das Hauptmenü (qml/Wald/).
// "catalog" ist das C++-Objekt aus src/Menu.cpp: Überschrift, ein Kasten je Vorlage mit
// einer kleinen Zeichnung (Flächen schon fertig projiziert, von hinten nach vorne), Name und was es braucht.
// Klick auf einen Kasten: auswählen, das Hologramm hängt dann vor der Figur und wird mit links gesetzt.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Kein eigenes Fenster mit Rahmen: deckt das Spielfenster ganz ab (src/Menu.cpp setzt die Größe), das Spiel scheint
// leicht grau abgedunkelt durch, die Auswahl steht fest in der Mitte
ApplicationWindow {
    id: window
    width: 900
    height: 640
    visible: false
    title: catalog.title
    flags: Qt.Tool | Qt.FramelessWindowHint
    color: "transparent"
    background: Rectangle { color: "#59000000" }

    onClosing: (close) => {  // Fenster schließen = Auswahl zu
        close.accepted = false
        catalog.close()
    }

    Shortcut {
        sequences: [catalog.closeKey, "Esc"]
        onActivated: catalog.close()
    }

    Pane {
        anchors.centerIn: parent
        width: Math.min(900, parent.width)
        height: Math.min(640, parent.height)
        padding: 12

        ColumnLayout {
            anchors.fill: parent
            spacing: 8

            Label {
                text: catalog.title
            }

            GridView {
                id: grid
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                cellWidth: 172
                cellHeight: 200
                model: catalog.items
                ScrollBar.vertical: ScrollBar {}
                // beim Öffnen: zum zuletzt gewählten Kasten
                Connections {
                    target: catalog
                    function onChanged() { Qt.callLater(() => grid.positionViewAtIndex(Math.max(0, catalog.current), GridView.Contain)) }
                }

                delegate: Rectangle {
                    id: tile
                    required property var modelData
                    required property int index
                    width: grid.cellWidth - 8
                    height: grid.cellHeight - 8
                    color: mouse.containsMouse ? palette.light : palette.button
                    border.width: index === catalog.current ? 2 : 1
                    border.color: index === catalog.current ? palette.highlight : palette.dark

                    // Zeichnung: die Flächen nacheinander füllen (die hinteren zuerst)
                    Canvas {
                        id: picture
                        x: 6
                        y: 6
                        width: parent.width - 12
                        height: 130
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
                            target: catalog
                            function onChanged() { picture.requestPaint() }
                        }
                    }

                    Column {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.margins: 6
                        Label {
                            width: parent.width
                            text: tile.modelData.label
                            elide: Text.ElideRight
                            horizontalAlignment: Text.AlignHCenter
                        }
                        Label {
                            width: parent.width
                            text: tile.modelData.info
                            font.pixelSize: 11
                            wrapMode: Text.Wrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                            horizontalAlignment: Text.AlignHCenter
                        }
                    }

                    MouseArea {
                        id: mouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: catalog.choose(tile.index)
                    }
                    ToolTip.visible: mouse.containsMouse && tile.modelData.info !== ""
                    ToolTip.delay: 400
                    ToolTip.text: tile.modelData.label + " – " + tile.modelData.info
                }
            }

            Label {
                text: "Klick: auswählen – dann hängt es als Hologramm vor der Figur (R drehen, Maus links setzen, Mausrad wechseln, rechts fertig).  " +
                      catalog.closeKey + " / Esc: schließen"
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
        }
    }
}
