// Dialog: Holzfenster mit Überschrift, der Rest abgedunkelt
import QtQuick
import QtQuick.Templates as T

T.Dialog {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            contentWidth + leftPadding + rightPadding,
                            implicitHeaderWidth,
                            implicitFooterWidth)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             contentHeight + topPadding + bottomPadding
                             + (implicitHeaderHeight > 0 ? implicitHeaderHeight + spacing : 0)
                             + (implicitFooterHeight > 0 ? implicitFooterHeight + spacing : 0))
    padding: 26
    // wächst mit dem Fenster wie die Seiten (Main.qml, ausgelegt für 1000 x 680)
    transformOrigin: T.Popup.TopLeft  // Qt rechnet die Vergrößerung beim Zentrieren schon ein
    scale: Math.max(1, control.Overlay.overlay ? Math.min(control.Overlay.overlay.width / 1000, control.Overlay.overlay.height / 680) : 1)
    topPadding: 10
    font.family: "Georgia"

    background: WoodPanel { seed: 23 }

    header: Text {
        text: control.title
        visible: control.title
        topPadding: 22
        leftPadding: 26
        rightPadding: 26
        font.family: "Georgia"
        font.pixelSize: 21
        font.bold: true
        color: "#f3e6c6"
        style: Text.Outline
        styleColor: "#1d1007"
        elide: Text.ElideRight
    }

    footer: DialogButtonBox {
        visible: count > 0
    }

    T.Overlay.modal: Rectangle { color: "#99000000" }
    T.Overlay.modeless: Rectangle { color: "#55000000" }
}
