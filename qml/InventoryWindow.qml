// Inventar im Spiel (E im Spiel-Modus), im Stil "Wald" wie das Hauptmenü (qml/Wald/).
// "inventory" ist das C++-Objekt aus src/Menu.cpp: Raster (cols x rows), Teile als Rechtecke darin (die
// Fläche entspricht dem Gewicht, die Form ungefähr der des Teils), Gewichtszeile. Rechte Maustaste auf ein Teil (oder
// mit rechts aus dem Fenster ziehen und loslassen): herausnehmen, es hängt dann vor der Figur und wird mit links gesetzt.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Kein eigenes Fenster mit Rahmen: deckt das Spielfenster ganz ab (src/Menu.cpp setzt die Größe), das Spiel scheint
// leicht grau abgedunkelt durch, das Inventar steht fest in der Mitte
ApplicationWindow {
    id: window
    width: 720
    height: 520
    visible: false
    title: "Inventar"
    flags: Qt.Tool | Qt.FramelessWindowHint
    color: "transparent"
    background: Rectangle { color: "#59000000" }

    onClosing: (close) => {  // Fenster schließen = Inventar zu
        close.accepted = false
        inventory.close()
    }

    Shortcut {
        sequences: ["E", "Esc"]
        onActivated: inventory.close()
    }

    Pane {
        anchors.centerIn: parent
        width: Math.min(720, parent.width)
        height: Math.min(520, parent.height)
        padding: 12

        ColumnLayout {
            anchors.fill: parent
            spacing: 8

            Label {
                text: "Inventar  " + inventory.summary
            }

            // Die Fläche: so groß wie möglich, Zellen quadratisch
            Item {
                id: area
                Layout.fillWidth: true
                Layout.fillHeight: true
                readonly property real cell: Math.min(width / inventory.cols, height / inventory.rows)

                Rectangle {
                    id: grid
                    width: area.cell * inventory.cols
                    height: area.cell * inventory.rows
                    color: "transparent"
                    border.width: 2
                    border.color: palette.mid

                    Repeater {
                        model: inventory.items
                        delegate: Rectangle {
                            required property var modelData
                            x: modelData.x * area.cell + 2
                            y: modelData.y * area.cell + 2
                            width: modelData.w * area.cell - 4
                            height: modelData.h * area.cell - 4
                            color: mouse.containsMouse ? palette.light : palette.button
                            border.width: 1
                            border.color: palette.dark
                            clip: true

                            id: box
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
                                    target: inventory
                                    function onChanged() { picture.requestPaint() }
                                }
                            }

                            Label {
                                visible: box.showName
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                anchors.margins: 3
                                text: modelData.name
                                font.pixelSize: 11
                                elide: Text.ElideRight
                                horizontalAlignment: Text.AlignHCenter
                            }

                            MouseArea {
                                id: mouse
                                anchors.fill: parent
                                hoverEnabled: true
                                acceptedButtons: Qt.RightButton
                                onReleased: inventory.takeOut(modelData.key)
                            }
                            ToolTip.visible: mouse.containsMouse
                            ToolTip.text: modelData.name + " – " + modelData.info
                        }
                    }
                }
            }

            Label {
                text: "Rechtsklick (oder mit rechts hinausziehen): Teil herausnehmen – dann vor der Figur drehen (R, Shift+R, Mausrad) und mit Linksklick setzen.  E: schließen"
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
        }
    }
}
