// Moospolster aus vielen kleinen Tupfen, in der Mitte am höchsten
import QtQuick

Canvas {
    property int seed: 5
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()

    onPaint: {
        const ctx = getContext("2d")
        ctx.reset()
        const w = width, h = height
        if (w <= 0 || h <= 0) return
        let s = (seed * 104729 + 7) % 2147483647
        const rnd = () => { s = (s * 16807) % 2147483647; return s / 2147483647 }
        const greens = ["#3f5f22", "#4c6e2a", "#5f8a35", "#7fa64b", "#8fb35a", "#a3c76a"]
        const n = Math.round(w * h / 6)
        for (let i = 0; i < n; i++) {
            const t = rnd()
            const top = h * (1 - Math.sin(Math.PI * t)) * 0.8
            const y = top + rnd() * (h - top)
            const up = 1 - (y - top) / (h - top + 0.01)       // oben heller
            const c = Math.min(greens.length - 1, Math.floor(up * 4 + rnd() * 2))
            ctx.fillStyle = greens[c]
            ctx.globalAlpha = 0.9
            const r = 1 + rnd() * 2.2
            ctx.beginPath(); ctx.ellipse(t * w - r, y - r, 2 * r, 2 * r); ctx.fill()
        }
    }
}
