// Knopfleiste unten im Dialog: Baumstamm-Buttons nebeneinander, ohne eigenen Hintergrund
import QtQuick
import QtQuick.Templates as T

T.DialogButtonBox {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            (control.count === 1 ? implicitContentWidth * 2 : implicitContentWidth) + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)
    contentWidth: (contentItem as ListView)?.contentWidth ?? 0

    spacing: 10
    padding: 20
    topPadding: 4
    bottomPadding: 22
    alignment: count === 1 ? Qt.AlignRight : undefined

    delegate: Button {
        width: control.count === 1 ? control.availableWidth / 2 : undefined
    }

    contentItem: ListView {
        implicitWidth: contentWidth
        model: control.contentModel
        spacing: control.spacing
        orientation: ListView.Horizontal
        boundsBehavior: Flickable.StopAtBounds
        snapMode: ListView.SnapToItem
    }

    background: Item {}
}
