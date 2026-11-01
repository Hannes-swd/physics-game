// Holz mit Maserung (Rinde oder Brett), gezeichnet statt Bild: Verlauf von oben nach unten, Längsrillen, Astlöcher.
// shade: -1 dunkler (gedrückt) bis 1 heller (Maus darüber)
import QtQuick

Canvas {
    id: wood
    property color topColor: "#4a2f1a"
    property color middleColor: "#7d5634"
    property color bottomColor: "#2e1c0f"
    property color grainColor: "#24150a"
    property real grainAmount: 1      // Rillen pro Fläche (Rinde 1, Brett weniger)
    property bool knots: true
    property real radius: 6
    property real shade: 0
    property int seed: 1

    onShadeChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()

    function tint(c) {
        return shade >= 0 ? Qt.lighter(c, 1 + 0.25 * shade) : Qt.darker(c, 1 - 0.6 * shade)
    }

    onPaint: {
        const ctx = getContext("2d")
        ctx.reset()
        const w = width, h = height
        if (w <= 0 || h <= 0) return
        let s = (seed * 9301 + 49297) % 2147483647
        const rnd = () => { s = (s * 16807) % 2147483647; return s / 2147483647 }

        ctx.save()
        ctx.beginPath()
        ctx.roundedRect(0, 0, w, h, radius, radius)
        ctx.clip()

        const g = ctx.createLinearGradient(0, 0, 0, h)
        g.addColorStop(0, String(tint(topColor)))
        g.addColorStop(0.3, String(tint(middleColor)))
        g.addColorStop(0.6, String(Qt.darker(tint(middleColor), 1.15)))
        g.addColorStop(1, String(tint(bottomColor)))
        ctx.fillStyle = g
        ctx.fillRect(0, 0, w, h)

        // Längsrillen
        const n = Math.round(w * h / 160 * grainAmount)
        ctx.strokeStyle = String(grainColor)
        for (let i = 0; i < n; i++) {
            const y = rnd() * h, x = rnd() * w - 20, len = 20 + rnd() * 110
            ctx.globalAlpha = 0.15 + rnd() * 0.4
            ctx.lineWidth = 0.6 + rnd() * 1.4
            ctx.beginPath()
            ctx.moveTo(x, y)
            ctx.bezierCurveTo(x + len / 3, y + (rnd() - 0.5) * 3, x + len * 2 / 3, y + (rnd() - 0.5) * 3, x + len, y + (rnd() - 0.5) * 2)
            ctx.stroke()
        }
        // helle Kanten
        ctx.strokeStyle = "#e0b27a"
        for (let i = 0; i < n / 6; i++) {
            const y = rnd() * h * 0.5, x = rnd() * w
            ctx.globalAlpha = 0.08 + rnd() * 0.15
            ctx.lineWidth = 1
            ctx.beginPath(); ctx.moveTo(x, y); ctx.lineTo(x + 15 + rnd() * 50, y); ctx.stroke()
        }
        // Astlöcher
        if (knots) {
            const k = Math.max(1, Math.round(w / 220))
            for (let i = 0; i < k; i++) {
                const kx = (i + 0.2 + rnd() * 0.6) * w / k, ky = h * (0.3 + rnd() * 0.45), r = Math.min(h * 0.12, 6)
                ctx.globalAlpha = 0.8
                ctx.fillStyle = String(grainColor)
                ctx.beginPath(); ctx.ellipse(kx - r * 1.4, ky - r * 0.8, r * 2.8, r * 1.6); ctx.fill()
                ctx.globalAlpha = 0.35
                ctx.strokeStyle = String(grainColor)
                ctx.lineWidth = 1.2
                ctx.beginPath(); ctx.ellipse(kx - r * 2.4, ky - r * 1.4, r * 4.8, r * 2.8); ctx.stroke()
            }
        }
        // Rundung: oben Licht, unten Schatten
        ctx.globalAlpha = 1
        const r = ctx.createLinearGradient(0, 0, 0, h)
        r.addColorStop(0, "rgba(255,230,190,0.18)")
        r.addColorStop(0.25, "rgba(255,230,190,0.0)")
        r.addColorStop(0.75, "rgba(0,0,0,0.0)")
        r.addColorStop(1, "rgba(0,0,0,0.35)")
        ctx.fillStyle = r
        ctx.fillRect(0, 0, w, h)
        ctx.restore()
    }
}
