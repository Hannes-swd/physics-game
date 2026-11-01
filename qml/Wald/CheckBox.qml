// Checkbox: kleines Brett, das Häkchen ist ein Zweig mit Blatt
import QtQuick
import QtQuick.Templates as T

T.CheckBox {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding,
                             implicitIndicatorHeight + topPadding + bottomPadding)
    padding: 6
    spacing: 10
    hoverEnabled: true
    font.family: "Georgia"
    font.pixelSize: 16

    indicator: Item {
        implicitWidth: 28
        implicitHeight: 28
        x: control.leftPadding
        y: control.topPadding + (control.availableHeight - height) / 2
        opacity: control.enabled ? 1 : 0.5

        Rectangle { x: 2; y: 3; width: parent.width; height: parent.height; radius: 4; color: "#000000"; opacity: 0.35 }
        Wood {
            anchors.fill: parent
            radius: 4
            seed: 5
            topColor: "#c79a63"; middleColor: "#b5854f"; bottomColor: "#8f6337"; grainColor: "#6b4422"
            grainAmount: 0.4
            knots: false
            shade: control.hovered ? 0.5 : control.down ? -0.5 : 0
        }
        Rectangle {
            anchors.fill: parent
            radius: 4
            color: "transparent"
            border.color: control.visualFocus ? "#b6e07a" : "#5a3a1e"
            border.width: 2
        }
        Canvas {
            anchors.fill: parent
            visible: control.checkState !== Qt.Unchecked
            property bool partial: control.checkState === Qt.PartiallyChecked
            onPartialChanged: requestPaint()
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                const s = width / 28
                ctx.lineCap = "round"
                ctx.lineJoin = "round"
                ctx.strokeStyle = "#3d5e20"
                ctx.lineWidth = 3.6 * s
                ctx.beginPath()
                if (partial) { ctx.moveTo(7 * s, 14 * s); ctx.lineTo(21 * s, 14 * s) }
                else { ctx.moveTo(6 * s, 14 * s); ctx.lineTo(12 * s, 21 * s); ctx.lineTo(23 * s, 6 * s) }
                ctx.stroke()
                if (!partial) {  // Blatt am Ende des Zweigs
                    ctx.fillStyle = "#8fb35a"
                    ctx.beginPath()
                    ctx.moveTo(23 * s, 6 * s)
                    ctx.quadraticCurveTo(29 * s, 4 * s, 28 * s, -1 * s)
                    ctx.quadraticCurveTo(22 * s, 0, 23 * s, 6 * s)
                    ctx.fill()
                }
            }
        }
    }

    contentItem: Text {
        leftPadding: control.indicator && !control.mirrored ? control.indicator.width + control.spacing : 0
        rightPadding: control.indicator && control.mirrored ? control.indicator.width + control.spacing : 0
        text: control.text
        font: control.font
        color: control.enabled ? "#f3e6c6" : "#9b8f74"
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
}
