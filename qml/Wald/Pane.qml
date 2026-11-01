// Fläche (Pause, Inventar, Auswahl): Bretter im Rindenrahmen
import QtQuick
import QtQuick.Templates as T

T.Pane {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            contentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             contentHeight + topPadding + bottomPadding)
    padding: 24
    font.family: "Georgia"

    background: WoodPanel {}
}
