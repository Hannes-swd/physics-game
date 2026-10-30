// Inventar im Spiel (E im Spiel-Modus). Rohbau wie das Hauptmenü: Qt Quick Controls im Stil "Basic", ohne eigene
// Gestaltung. "inventory" ist das C++-Objekt aus src/Menu.cpp: Raster (cols x rows), Teile als Rechtecke darin (die
// Fläche entspricht dem Gewicht, die Form ungefähr der des Teils), Gewichtszeile. Rechte Maustaste auf ein Teil (oder
// mit rechts aus dem Fenster ziehen und loslassen): herausnehmen, es hängt dann vor der Figur und wird mit links gesetzt.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window
    width: 720
    height: 520
    visible: false
    title: "Inventar"

    onClosing: (close) => {  // Fenster schließen = Inventar zu
        close.accepted = false
        inventory.close()
    }

    Shortcut {
        sequences: ["E", "Esc"]
        onActivated: inventory.close()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
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

                        Column {
                            anchors.centerIn: parent
                            width: parent.width - 6
                            Label {
                                width: parent.width
                                text: modelData.name
                                elide: Text.ElideRight
                                horizontalAlignment: Text.AlignHCenter
                            }
                            Label {
                                width: parent.width
                                text: modelData.info
                                font.pixelSize: 11
                                elide: Text.ElideRight
                                horizontalAlignment: Text.AlignHCenter
                                visible: parent.parent.height > 36
                            }
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
