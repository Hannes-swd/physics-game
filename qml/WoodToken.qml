// Runde Holzmarke wie in der Werkzeugleiste des Spiels (src/Icons.cpp): Holz mit hellem Ring, das Symbol hineingraviert
// (Strichzeichnung als SVG-Pfad im 24er-Raster). Gewählt: hellere Marke, moosgrüner Ring und Schein. Unten rechts die Taste
import QtQuick
import QtQuick.Shapes

Item {
    id: token
    property bool selected: false
    property string icon: ""
    property string keyText: ""
    signal clicked()

    width: 64
    height: 64

    Rectangle {  // Schein (gewählt grün, Maus darüber schwach)
        anchors.fill: parent
        anchors.margins: -7
        radius: width / 2
        color: "#b6e07a"
        opacity: token.selected ? 0.3 : mouse.containsMouse ? 0.12 : 0
        Behavior on opacity { NumberAnimation { duration: 120 } }
    }
    Rectangle {  // Schatten
        anchors.fill: parent
        anchors.topMargin: 3
        anchors.bottomMargin: -3
        radius: width / 2
        color: "#000000"
        opacity: 0.45
    }
    Canvas {  // Holz: hell etwas über der Mitte, dunkel zum Rand; dunkler Reif, innen ein heller (gewählt: moosgrün)
        anchors.fill: parent
        property bool sel: token.selected
        onSelChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const r = width / 2
            const g = ctx.createRadialGradient(r, r * 0.76, 0, r, r * 0.76, r * 1.3)
            g.addColorStop(0, sel ? "#9b6c3f" : "#8a6038")
            g.addColorStop(0.45, sel ? "#7d5634" : "#6b4422")
            g.addColorStop(1, "#3a2412")
            ctx.fillStyle = g
            ctx.beginPath()
            ctx.arc(r, r, r, 0, 2 * Math.PI)
            ctx.fill()
            ctx.lineWidth = 2
            ctx.strokeStyle = "#24150a"
            ctx.beginPath()
            ctx.arc(r, r, r - 1, 0, 2 * Math.PI)
            ctx.stroke()
            ctx.lineWidth = 1.6
            ctx.strokeStyle = sel ? "#b6e07a" : "rgba(201, 151, 92, 0.4)"
            ctx.beginPath()
            ctx.arc(r, r, r - 2.9, 0, 2 * Math.PI)
            ctx.stroke()
        }
    }

    // Symbol, graviert: heller Schein unten, dunkle Kante oben, darauf die Linie
    component Engraving: Shape {
        property color color
        width: 24
        height: 24
        anchors.centerIn: parent
        scale: token.width * 0.56 / 24
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: parent.color
            strokeWidth: 1.45
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: token.icon }
        }
    }
    Engraving { color: "#2effecc8"; anchors.verticalCenterOffset: 1.0 }
    Engraving { color: "#e6140a04"; anchors.verticalCenterOffset: -0.8 }
    Engraving { color: token.selected ? "#fff4dc" : "#e4d6b6" }

    Rectangle {  // Taste
        visible: token.keyText !== ""
        x: token.width - width / 2 - 8
        y: token.height - height / 2 - 8
        width: Math.max(19, keyLabel.implicitWidth + 9)
        height: 19
        radius: height / 2
        color: token.selected ? "#5f8a35" : "#24150a"
        border.width: 1
        border.color: token.selected ? "#b6e07a" : "#80c9975c"
        Text {
            id: keyLabel
            anchors.centerIn: parent
            text: token.keyText
            color: "#fff4dc"
            font.family: "Georgia"
            font.pixelSize: 11
            font.bold: true
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: token.clicked()
    }
}
