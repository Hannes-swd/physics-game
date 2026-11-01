// Eingabefeld: ein gehobeltes Brett mit eingelassener Schreibfläche und zwei Nägeln. Fokus: grüner Rand
import QtQuick
import QtQuick.Templates as T

T.TextField {
    id: control

    implicitWidth: implicitBackgroundWidth + leftInset + rightInset
                   || Math.max(contentWidth, placeholder.implicitWidth) + leftPadding + rightPadding
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             contentHeight + topPadding + bottomPadding,
                             placeholder.implicitHeight + topPadding + bottomPadding)

    leftPadding: 22
    rightPadding: 22
    topPadding: 10
    bottomPadding: 10
    hoverEnabled: true
    font.family: "Georgia"
    font.pixelSize: 16
    color: "#fff4dc"
    selectionColor: "#5f8a35"
    selectedTextColor: "#fff4dc"
    placeholderTextColor: "#d9c49a"
    verticalAlignment: TextInput.AlignVCenter

    Text {
        id: placeholder
        x: control.leftPadding
        y: control.topPadding
        width: control.width - (control.leftPadding + control.rightPadding)
        height: control.height - (control.topPadding + control.bottomPadding)
        text: control.placeholderText
        font: control.font
        color: control.placeholderTextColor
        opacity: 0.75
        verticalAlignment: control.verticalAlignment
        visible: !control.length && !control.preeditText && (!control.activeFocus || control.horizontalAlignment !== Qt.AlignHCenter)
        elide: Text.ElideRight
    }

    background: PlankField {
        implicitWidth: 220
        implicitHeight: 44
        active: control.activeFocus
        hovered: control.hovered
        enabled: control.enabled
    }
}
