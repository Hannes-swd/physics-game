// Hintergrund für Eingabefelder (TextField, SpinBox, ComboBox): Brett mit eingelassener Schreibfläche
import QtQuick

Item {
    id: field
    property bool active: false
    property bool hovered: false
    opacity: enabled ? 1 : 0.55

    Rectangle {
        x: 2; y: 4; width: parent.width; height: parent.height
        radius: 5
        color: "#000000"
        opacity: 0.35
    }
    Wood {
        anchors.fill: parent
        radius: 5
        seed: 77
        topColor: "#c79a63"
        middleColor: "#b5854f"
        bottomColor: "#8f6337"
        grainColor: "#6b4422"
        grainAmount: 0.3
        knots: false
        shade: field.hovered ? 0.4 : 0
    }
    Rectangle {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        anchors.topMargin: 5
        anchors.bottomMargin: 5
        radius: 3
        color: "#3a2614"
        opacity: 0.6
    }
    Rectangle {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        anchors.topMargin: 5
        anchors.bottomMargin: 5
        radius: 3
        color: "transparent"
        border.color: field.active ? "#b6e07a" : "#5a3a1e"
        border.width: field.active ? 2 : 1
    }
    Rectangle { x: 4; anchors.verticalCenter: parent.verticalCenter; width: 4; height: 4; radius: 2; color: "#3a2412" }
    Rectangle { x: parent.width - 8; anchors.verticalCenter: parent.verticalCenter; width: 4; height: 4; radius: 2; color: "#3a2412" }
}
