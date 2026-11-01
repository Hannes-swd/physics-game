// Fenster: Wald im Hintergrund (Pause und Inventar setzen ihren eigenen, durchsichtigen Hintergrund). Die Palette gilt
// für alles, was nicht eigens gestaltet ist (z.B. die Zellen im Inventar)
import QtQuick
import QtQuick.Templates as T

T.ApplicationWindow {
    color: "#1b2418"
    font.family: "Georgia"

    palette.window: "#2e1c0f"
    palette.windowText: "#f3e6c6"
    palette.base: "#3a2614"
    palette.alternateBase: "#4a2f1a"
    palette.text: "#f3e6c6"
    palette.button: "#5e3d22"
    palette.buttonText: "#f3e6c6"
    palette.light: "#7d5634"
    palette.midlight: "#6b4628"
    palette.mid: "#4a2f1a"
    palette.dark: "#24150a"
    palette.shadow: "#000000"
    palette.highlight: "#8fb35a"
    palette.highlightedText: "#1b1008"
    palette.placeholderText: "#d9c49a"
    palette.toolTipBase: "#2e1c0f"
    palette.toolTipText: "#f3e6c6"
    palette.brightText: "#ffffff"
    palette.link: "#b6e07a"

    background: Forest {}
}
