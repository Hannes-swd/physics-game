// Regler: ein Ast als Schiene, der eingestellte Teil bemoost, der Griff eine Holzscheibe
import QtQuick
import QtQuick.Templates as T

T.Slider {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitHandleWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitHandleHeight + topPadding + bottomPadding)
    padding: 6
    hoverEnabled: true

    handle: Item {
        x: control.leftPadding + control.visualPosition * (control.availableWidth - width)
        y: control.topPadding + (control.availableHeight - height) / 2
        implicitWidth: 26
        implicitHeight: 30
        Rectangle {
            x: 2; y: 4; width: parent.width; height: parent.height
            radius: width / 2
            color: "#000000"
            opacity: 0.4
        }
        Slice {
            anchors.fill: parent
            seed: 9
            shade: control.pressed ? -1 : control.hovered || control.visualFocus ? 1 : 0
        }
    }

    background: Item {
        x: control.leftPadding
        y: control.topPadding + (control.availableHeight - height) / 2
        implicitWidth: 200
        implicitHeight: 12
        width: control.availableWidth
        height: implicitHeight
        opacity: control.enabled ? 1 : 0.5

        Rectangle {
            anchors.fill: parent
            anchors.margins: -1
            radius: height / 2
            color: "#24150a"
        }
        Wood {
            anchors.fill: parent
            radius: height / 2
            seed: 21
            knots: false
        }
        Rectangle {  // Moos bis zum Griff
            y: 1
            height: parent.height / 2
            width: control.visualPosition * parent.width
            radius: height / 2
            color: "#7fa64b"
            opacity: 0.9
        }
    }
}
