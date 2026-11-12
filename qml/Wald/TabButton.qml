// Reiter als Holzbrettchen: gewählt hell mit Moos obendrauf und grünem Schein, sonst dunkles Holz
import QtQuick
import QtQuick.Templates as T

T.TabButton {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)
    leftPadding: 22
    rightPadding: 22
    topPadding: 8
    bottomPadding: 9
    hoverEnabled: true
    font.family: "Georgia"
    font.pixelSize: 16
    font.bold: control.checked

    contentItem: Text {
        text: control.text
        font: control.font
        color: control.checked ? "#fff4dc" : control.hovered ? "#f3e6c6" : "#cdbb92"
        style: Text.Outline
        styleColor: "#1d1007"
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    background: Item {
        implicitWidth: 120
        implicitHeight: 40

        Rectangle {  // grüner Schein
            anchors.fill: parent
            anchors.margins: -3
            radius: 10
            color: "#b6e07a"
            opacity: control.checked ? 0.22 : 0
        }
        Wood {
            anchors.fill: parent
            radius: 7
            seed: 7
            knots: false
            grainAmount: 0.5
            topColor: control.checked ? "#b5854f" : "#4a2f1a"
            middleColor: control.checked ? "#9b6c3f" : "#3a2412"
            bottomColor: control.checked ? "#7e5530" : "#2e1c0f"
            shade: control.hovered && !control.checked ? 0.5 : 0
            onTopColorChanged: requestPaint()  // gewählt / nicht gewählt: neu zeichnen
        }
        Rectangle {
            anchors.fill: parent
            radius: 7
            color: "transparent"
            border.color: "#1b1008"
            border.width: 1
        }
        Moss {
            visible: control.checked
            x: 12
            y: -7
            width: parent.width - 24
            height: 12
            seed: 3
        }
    }
}
