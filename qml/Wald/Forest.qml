// Hintergrund: Wald in der Dämmerung, mehrere Reihen Tannen, hinten heller (Nebel), Lichtstrahlen
import QtQuick

Canvas {
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()

    onPaint: {
        const ctx = getContext("2d")
        ctx.reset()
        const w = width, h = height
        if (w <= 0 || h <= 0) return
        let s = 4242
        const rnd = () => { s = (s * 16807) % 2147483647; return s / 2147483647 }

        const sky = ctx.createLinearGradient(0, 0, 0, h)
        sky.addColorStop(0, "#2c3d28")
        sky.addColorStop(0.55, "#4d5f3a")
        sky.addColorStop(1, "#1b2418")
        ctx.fillStyle = sky
        ctx.fillRect(0, 0, w, h)

        for (let i = 0; i < 5; i++) {
            const x = w * (0.15 + rnd() * 0.7)
            ctx.globalAlpha = 0.05 + rnd() * 0.05
            ctx.fillStyle = "#f3e6c6"
            ctx.beginPath()
            ctx.moveTo(x, 0); ctx.lineTo(x + 40 + rnd() * 60, 0)
            ctx.lineTo(x + 220 + rnd() * 120, h); ctx.lineTo(x + 120, h)
            ctx.closePath(); ctx.fill()
        }

        const layers = [["#3c4c32", 0.55, 0.9], ["#2d3a26", 0.68, 1.0], ["#1f291b", 0.8, 1.15], ["#141b12", 0.95, 1.35]]
        for (let l = 0; l < layers.length; l++) {
            const col = layers[l][0], base = layers[l][1], scale = layers[l][2]
            ctx.globalAlpha = 1
            ctx.fillStyle = col
            let x = -30 + rnd() * 30
            while (x < w + 40) {
                const tw = (30 + rnd() * 30) * scale, th = (h * 0.35 + rnd() * h * 0.3) * scale
                const by = h * base
                for (let k = 0; k < 3; k++) {  // Tanne aus drei Etagen
                    const ty = by - th + k * th * 0.25, bw = tw * (0.6 + k * 0.35)
                    ctx.beginPath()
                    ctx.moveTo(x, ty)
                    ctx.lineTo(x + bw, ty + th * 0.45)
                    ctx.lineTo(x - bw, ty + th * 0.45)
                    ctx.closePath(); ctx.fill()
                }
                ctx.fillRect(x - tw * 0.1, by - th * 0.2, tw * 0.2, th * 0.25 + h)
                x += tw * (1.1 + rnd() * 0.9)
            }
            ctx.fillRect(0, h * base, w, h)
        }
        const v = ctx.createRadialGradient(w / 2, h / 2, Math.min(w, h) * 0.3, w / 2, h / 2, Math.max(w, h) * 0.75)
        v.addColorStop(0, "rgba(0,0,0,0)")
        v.addColorStop(1, "rgba(0,0,0,0.55)")
        ctx.globalAlpha = 1
        ctx.fillStyle = v
        ctx.fillRect(0, 0, w, h)
    }
}
