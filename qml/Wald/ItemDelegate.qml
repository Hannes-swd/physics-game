// Listeneintrag (Welten, Spiele im LAN, Auswahlfeld): ausgewählt leuchtet er moosgrün
import QtQuick
import QtQuick.Templates as T

T.ItemDelegate {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)
    padding: 10
    leftPadding: 14
    hoverEnabled: true
    font.family: "Georgia"
    font.pixelSize: 15

    contentItem: Text {
        text: control.text
        font: control.font
        color: control.highlighted ? "#1b1008" : control.enabled ? "#f3e6c6" : "#9b8f74"
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
        wrapMode: Text.Wrap
    }

    background: Rectangle {
        implicitWidth: 100
        implicitHeight: 40
        radius: 4
        color: control.highlighted ? "#8fb35a" : control.down ? "#5e3d22" : control.hovered ? "#4a2f1a" : "transparent"
        border.color: control.visualFocus ? "#b6e07a" : "transparent"
    }
}
