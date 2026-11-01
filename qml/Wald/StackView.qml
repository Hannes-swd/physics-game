// Seitenwechsel: kurzes Überblenden statt Wischen (das Wischen ragte aus dem Holzfenster heraus)
import QtQuick
import QtQuick.Templates as T

T.StackView {
    id: control

    popEnter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 140 } }
    popExit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 100 } }
    pushEnter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 140 } }
    pushExit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 100 } }
    replaceEnter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 140 } }
    replaceExit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 100 } }
}
