// Eine Taste auf der Seite Einstellungen → Steuerung: helles Holzklötzchen mit dem Tastennamen.
// waiting: wartet auf die neue Taste (grün leuchtend), bad: auch bei einer anderen Aktion belegt (roter Rand),
// fixed: nur zur Anzeige (Maus, Esc, Strg-Kürzel). Leer: kleines „+“ für eine zweite Taste
import QtQuick

Rectangle {
    id: cap
    property string text: ""
    property bool waiting: false
    property bool bad: false
    property bool fixed: false
    signal clicked()
    signal rightClicked()

    readonly property bool empty: text === "" && !waiting
    implicitWidth: Math.max(fixed ? 0 : 58, label.implicitWidth + 24)
    implicitHeight: 28
    radius: 5
    border.width: 1
    border.color: "#3a2412"
    opacity: empty && !mouse.containsMouse ? 0.45 : 1
    gradient: Gradient {
        GradientStop { position: 0; color: cap.waiting ? "#6f9a40" : cap.fixed || cap.empty ? "#6b4422" : mouse.containsMouse ? "#c9975c" : "#b5854f" }
        GradientStop { position: 1; color: cap.waiting ? "#3f6020" : cap.fixed || cap.empty ? "#4a2f1a" : "#8a5f33" }
    }

    Rectangle {  // Unterkante, wie eine echte Taste
        z: -1
        anchors.fill: parent
        anchors.topMargin: 2
        anchors.bottomMargin: -2
        radius: parent.radius
        color: "#24150a"
    }
    Rectangle {  // grünes Leuchten beim Warten, roter Rand bei doppelter Belegung
        anchors.fill: parent
        anchors.margins: -3
        radius: parent.radius + 3
        color: "transparent"
        border.width: 2
        border.color: cap.waiting ? "#b6e07a" : "#e0614a"
        visible: cap.waiting || cap.bad
    }
    Text {
        id: label
        anchors.centerIn: parent
        text: cap.waiting ? "Taste drücken …" : cap.empty ? "+" : cap.text
        font.family: "Georgia"
        font.pixelSize: 14
        font.bold: !cap.waiting
        font.italic: cap.waiting
        color: cap.fixed ? "#cdbb92" : "#fff4dc"
        style: Text.Outline
        styleColor: "#3a2412"
    }
    MouseArea {
        id: mouse
        anchors.fill: parent
        enabled: !cap.fixed
        hoverEnabled: true
        cursorShape: cap.fixed ? Qt.ArrowCursor : Qt.PointingHandCursor
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onClicked: (m) => m.button === Qt.RightButton ? cap.rightClicked() : cap.clicked()
    }
}
