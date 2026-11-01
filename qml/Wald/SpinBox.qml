// Zahlenfeld: Brett wie das Eingabefeld, links und rechts kleine Holzscheiben für - und +
import QtQuick
import QtQuick.Templates as T

T.SpinBox {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            contentItem.implicitWidth + 2 * padding + up.implicitIndicatorWidth + down.implicitIndicatorWidth)
    implicitHeight: Math.max(implicitContentHeight + topPadding + bottomPadding,
                             implicitBackgroundHeight,
                             up.implicitIndicatorHeight,
                             down.implicitIndicatorHeight)

    padding: 6
    leftPadding: padding + (control.mirrored ? (up.indicator ? up.indicator.width : 0) : (down.indicator ? down.indicator.width : 0))
    rightPadding: padding + (control.mirrored ? (down.indicator ? down.indicator.width : 0) : (up.indicator ? up.indicator.width : 0))
    hoverEnabled: true
    font.family: "Georgia"
    font.pixelSize: 16

    validator: IntValidator {
        locale: control.locale.name
        bottom: Math.min(control.from, control.to)
        top: Math.max(control.from, control.to)
    }

    contentItem: TextInput {
        z: 2
        text: control.displayText
        font: control.font
        color: "#fff4dc"
        selectionColor: "#5f8a35"
        selectedTextColor: "#fff4dc"
        horizontalAlignment: Qt.AlignHCenter
        verticalAlignment: Qt.AlignVCenter
        readOnly: !control.editable
        validator: control.validator
        inputMethodHints: control.inputMethodHints
        clip: width < implicitWidth
    }

    component Knob: Item {
        id: knob
        property string sign
        property bool pressed
        property bool hovered
        implicitWidth: 34
        implicitHeight: 38
        Slice {
            anchors.fill: parent
            anchors.margins: 3
            seed: knob.sign === "+" ? 4 : 8
            shade: knob.pressed ? -1 : knob.hovered ? 1 : 0
        }
        Text {
            anchors.centerIn: parent
            text: knob.sign
            font.pixelSize: 20
            font.bold: true
            color: "#3b2412"
        }
    }

    up.indicator: Knob {
        x: control.mirrored ? 0 : control.width - width
        height: control.height
        sign: "+"
        pressed: control.up.pressed
        hovered: control.up.hovered
        opacity: enabled ? 1 : 0.5
    }

    down.indicator: Knob {
        x: control.mirrored ? control.width - width : 0
        height: control.height
        sign: "−"
        pressed: control.down.pressed
        hovered: control.down.hovered
        opacity: enabled ? 1 : 0.5
    }

    background: PlankField {
        implicitWidth: 150
        implicitHeight: 42
        active: control.activeFocus
        hovered: control.hovered
        enabled: control.enabled
    }
}
