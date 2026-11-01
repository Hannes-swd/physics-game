// Schnittfläche eines Stamms: Jahresringe, Kern, Riss
import QtQuick

Canvas {
    property int seed: 3
    property real shade: 0
    onShadeChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()

    onPaint: {
        const ctx = getContext("2d")
        ctx.reset()
        const w = width, h = height
        if (w <= 0 || h <= 0) return
        let s = (seed * 7919 + 13) % 2147483647
        const rnd = () => { s = (s * 16807) % 2147483647; return s / 2147483647 }
        const cx = w / 2, cy = h / 2
        const light = shade > 0 ? 1.12 : shade < 0 ? 0.8 : 1

        ctx.fillStyle = "#3b2412"   // Rinde rundherum
        ctx.beginPath(); ctx.ellipse(0, 0, w, h); ctx.fill()
        const g = ctx.createRadialGradient(cx * 0.9, cy * 0.9, 0, cx, cy, Math.max(w, h) / 2)
        g.addColorStop(0, String(Qt.lighter("#e2bd84", light)))
        g.addColorStop(0.7, String(Qt.lighter("#c9975c", light)))
        g.addColorStop(1, String(Qt.lighter("#9c6b3a", light)))
        ctx.fillStyle = g
        const bx = w * 0.1, by = h * 0.08
        ctx.beginPath(); ctx.ellipse(bx, by, w - 2 * bx, h - 2 * by); ctx.fill()

        ctx.strokeStyle = "#8a5a2e"
        const rings = 5 + Math.round(Math.min(w, h) / 12)
        for (let i = 1; i < rings; i++) {
            const k = i / rings
            const rw = (w - 2 * bx) * k, rh = (h - 2 * by) * k
            ctx.globalAlpha = 0.3 + 0.45 * k
            ctx.lineWidth = 0.8 + rnd() * 0.6
            ctx.beginPath()
            ctx.ellipse(cx - rw / 2 + (rnd() - 0.5) * 1.2, cy - rh / 2 + (rnd() - 0.5) * 1.2, rw, rh)
            ctx.stroke()
        }
        ctx.globalAlpha = 0.7
        ctx.fillStyle = "#6b4422"
        ctx.beginPath(); ctx.ellipse(cx - 1.5, cy - 1.5, 3, 3); ctx.fill()
        ctx.strokeStyle = "#5a3517"
        ctx.lineWidth = 1
        ctx.beginPath(); ctx.moveTo(cx, cy); ctx.lineTo(cx + (w / 2 - bx) * 0.8, cy - h * 0.12); ctx.stroke()
    }
}
