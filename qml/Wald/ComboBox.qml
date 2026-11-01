// Auswahlfeld: Brett wie das Eingabefeld, Pfeil aus Holz, die Liste klappt als dunkles Brett auf
import QtQuick
import QtQuick.Templates as T

T.ComboBox {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding,
                             implicitIndicatorHeight + topPadding + bottomPadding)

    leftPadding: 22
    rightPadding: 22 + (indicator ? indicator.width : 0)
    topPadding: 8
    bottomPadding: 8
    hoverEnabled: true
    font.family: "Georgia"
    font.pixelSize: 16

    delegate: ItemDelegate {
        required property var model
        required property int index
        width: ListView.view.width
        text: model[control.textRole]
        highlighted: control.highlightedIndex === index
        hoverEnabled: control.hoverEnabled
    }

    indicator: Canvas {
        x: control.mirrored ? 18 : control.width - width - 18
        y: control.topPadding + (control.availableHeight - height) / 2
        width: 14
        height: 9
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            ctx.fillStyle = "#f3e6c6"
            ctx.strokeStyle = "#1d1007"
            ctx.lineWidth = 1.5
            ctx.beginPath()
            ctx.moveTo(1, 1); ctx.lineTo(width - 1, 1); ctx.lineTo(width / 2, height - 1)
            ctx.closePath()
            ctx.fill(); ctx.stroke()
        }
    }

    contentItem: T.TextField {
        text: control.editable ? control.editText : control.displayText
        enabled: control.editable
        autoScroll: control.editable
        readOnly: control.down
        inputMethodHints: control.inputMethodHints
        validator: control.validator
        selectByMouse: control.selectTextByMouse
        font: control.font
        color: "#fff4dc"
        selectionColor: "#5f8a35"
        verticalAlignment: Text.AlignVCenter
    }

    background: PlankField {
        implicitWidth: 180
        implicitHeight: 42
        active: control.visualFocus || control.down
        hovered: control.hovered
        enabled: control.enabled
    }

    popup: T.Popup {
        y: control.height
        width: control.width
        height: Math.min(contentItem.implicitHeight + 8, control.Window.height - topMargin - bottomMargin)
        topMargin: 6
        bottomMargin: 6
        padding: 4
        // die Liste wächst wie die Seite, an der linken oberen Ecke des Felds festgehalten
        transformOrigin: T.Popup.TopLeft
        scale: Math.max(1, control.Overlay.overlay ? Math.min(control.Overlay.overlay.width / 1000, control.Overlay.overlay.height / 680) : 1)

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.delegateModel
            currentIndex: control.highlightedIndex
            highlightMoveDuration: 0
            T.ScrollIndicator.vertical: T.ScrollIndicator {}
        }

        background: Rectangle {
            radius: 5
            color: "#2e1c0f"
            border.color: "#7d5634"
            border.width: 2
        }
    }
}
