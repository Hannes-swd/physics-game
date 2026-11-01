// Fensterrahmen: Bretter in einem Rahmen aus Rinde, mit Moos an den Ecken
import QtQuick

Item {
    id: panel
    property int seed: 11
    readonly property real frame: 12

    Rectangle {  // Schatten
        anchors.fill: parent
        anchors.topMargin: 6
        anchors.leftMargin: 3
        anchors.rightMargin: -3
        anchors.bottomMargin: -6
        radius: 12
        color: "#000000"
        opacity: 0.45
    }
    Wood {
        anchors.fill: parent
        radius: 12
        seed: panel.seed
        knots: false
    }
    Column {
        x: panel.frame
        y: panel.frame
        width: panel.width - 2 * panel.frame
        height: panel.height - 2 * panel.frame
        clip: true
        Repeater {
            model: Math.max(1, Math.ceil((panel.height - 2 * panel.frame) / 46))
            Wood {
                required property int index
                width: parent.width
                height: 46
                radius: 0
                seed: panel.seed * 31 + index
                topColor: index % 2 ? "#a77a46" : "#b5854f"
                middleColor: index % 2 ? "#a07042" : "#ad7d48"
                bottomColor: "#7e5530"
                grainColor: "#5a3a1e"
                grainAmount: 0.35
                knots: index % 3 === 1
                Rectangle { width: parent.width; height: 1; color: "#3a2412"; opacity: 0.8 }
            }
        }
    }
    Rectangle {  // die Bretter abdunkeln, damit heller Text darauf lesbar bleibt
        x: panel.frame
        y: panel.frame
        width: panel.width - 2 * panel.frame
        height: panel.height - 2 * panel.frame
        color: "#1b1008"
        opacity: 0.55
        border.color: "#1b1008"
        border.width: 2
    }
    Moss { x: -4; y: -6; width: Math.min(140, panel.width * 0.3); height: 22; seed: panel.seed }
    Moss { x: panel.width - width + 4; y: panel.height - 16; width: Math.min(110, panel.width * 0.25); height: 18; seed: panel.seed + 3 }
}
