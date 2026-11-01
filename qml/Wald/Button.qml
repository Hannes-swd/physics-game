// Button als liegender Baumstamm: Rinde, rechts die Schnittfläche mit Jahresringen, oben ein Moospolster.
// Maus darüber: heller mit grünem Schein, gedrückt: dunkler und eingesunken
import QtQuick
import QtQuick.Templates as T

T.Button {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    readonly property bool lit: control.hovered || control.visualFocus || control.checked
    leftPadding: 24
    rightPadding: 34
    topPadding: control.down ? 13 : 10
    bottomPadding: control.down ? 7 : 10
    hoverEnabled: true
    font.family: "Georgia"
    font.pixelSize: 17
    font.bold: true

    contentItem: Text {
        text: control.text
        font: control.font
        color: control.enabled ? "#f3e6c6" : "#9b8f74"
        style: Text.Outline
        styleColor: "#1d1007"
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Item {
        implicitWidth: 100
        implicitHeight: 48
        opacity: control.enabled ? 1 : 0.55

        Rectangle {  // grüner Schein
            anchors.fill: parent
            anchors.margins: -4
            radius: height / 2
            color: "#b6e07a"
            opacity: control.lit && control.enabled ? 0.28 : 0
            Behavior on opacity { NumberAnimation { duration: 120 } }
        }
        Rectangle {  // Schatten
            x: 4
            y: 6
            width: parent.width - 8
            height: parent.height - 4
            radius: height / 2
            color: "#000000"
            opacity: control.down ? 0.2 : 0.4
        }
        Item {
            id: log
            width: parent.width
            height: parent.height - 4
            y: control.down ? 3 : 0

            Rectangle {  // hinteres Ende des Stamms
                width: log.height * 0.5
                height: log.height
                radius: width / 2
                color: "#24150a"
            }
            Wood {
                x: log.height * 0.18
                width: log.width - log.height * 0.43
                height: log.height
                seed: control.text.length * 7 + 3
                shade: control.down ? -1 : control.lit ? 1 : 0
            }
            Slice {
                x: log.width - width
                width: log.height * 0.5
                height: log.height
                seed: control.text.length + 1
                shade: control.down ? -1 : control.lit ? 1 : 0
            }
            Moss {
                x: log.width * 0.42
                y: -6
                width: Math.min(64, log.width * 0.3)
                height: 11
                seed: control.text.length * 3 + 1
            }
        }
    }
}
