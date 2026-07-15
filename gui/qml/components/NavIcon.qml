import QtQuick
import StreamSoftGui

Canvas {
    id: root
    property string icon: "overlay"
    property color strokeColor: Theme.text
    width: 18
    height: 18

    Behavior on strokeColor { ColorAnimation { duration: Theme.motionFast } }

    onStrokeColorChanged: requestPaint()
    onIconChanged: requestPaint()
    Component.onCompleted: requestPaint()

    onPaint: {
        var ctx = getContext("2d")
        ctx.reset()
        ctx.strokeStyle = root.strokeColor
        ctx.fillStyle = root.strokeColor
        ctx.lineWidth = 1.6
        ctx.lineCap = "round"
        ctx.lineJoin = "round"

        var w = width, h = height

        if (icon === "overlay") {
            ctx.beginPath()
            ctx.rect(2.5, 2.5, w - 5, h - 7)
            ctx.stroke()
            ctx.beginPath()
            ctx.moveTo(w / 2, h - 4.5)
            ctx.lineTo(w / 2, h - 2)
            ctx.moveTo(w / 2 - 3, h - 2)
            ctx.lineTo(w / 2 + 3, h - 2)
            ctx.stroke()
        } else if (icon === "connections") {
            ctx.beginPath()
            ctx.moveTo(6.5, h - 6.5)
            ctx.lineTo(h - 6.5, 6.5)
            ctx.stroke()
            ctx.beginPath()
            ctx.arc(5.5, h - 5.5, 3, 0, Math.PI * 2)
            ctx.stroke()
            ctx.beginPath()
            ctx.arc(w - 5.5, 5.5, 3, 0, Math.PI * 2)
            ctx.stroke()
        } else if (icon === "voice") {
            ctx.beginPath()
            ctx.rect(w / 2 - 2.5, 2.5, 5, 8)
            ctx.stroke()
            ctx.beginPath()
            ctx.arc(w / 2, 9, 5, Math.PI * 0.15, Math.PI * 0.85, false)
            ctx.stroke()
            ctx.beginPath()
            ctx.moveTo(w / 2, 14)
            ctx.lineTo(w / 2, h - 2)
            ctx.moveTo(w / 2 - 3, h - 2)
            ctx.lineTo(w / 2 + 3, h - 2)
            ctx.stroke()
        } else if (icon === "alerts") {
            ctx.beginPath()
            ctx.arc(w / 2, h / 2 - 1, 5, Math.PI, Math.PI * 2, false)
            ctx.lineTo(w / 2 + 5, h / 2 + 4)
            ctx.lineTo(w / 2 - 5, h / 2 + 4)
            ctx.closePath()
            ctx.stroke()
            ctx.beginPath()
            ctx.arc(w / 2, h - 3, 1.4, 0, Math.PI * 2)
            ctx.fill()
        } else if (icon === "commands") {
            ctx.beginPath()
            ctx.rect(2.5, 3, w - 5, h - 7)
            ctx.stroke()
            ctx.beginPath()
            ctx.moveTo(6, h - 4)
            ctx.lineTo(6, h - 1)
            ctx.lineTo(9, h - 4)
            ctx.stroke()
        } else if (icon === "poll") {
            ctx.beginPath()
            ctx.moveTo(5, h - 3)
            ctx.lineTo(5, h - 8)
            ctx.moveTo(w / 2, h - 3)
            ctx.lineTo(w / 2, 4)
            ctx.moveTo(w - 5, h - 3)
            ctx.lineTo(w - 5, h - 11)
            ctx.stroke()
        } else if (icon === "music") {
            ctx.beginPath()
            ctx.arc(6, h - 4.5, 2.2, 0, Math.PI * 2)
            ctx.stroke()
            ctx.beginPath()
            ctx.arc(w - 6.5, h - 6.5, 2.2, 0, Math.PI * 2)
            ctx.stroke()
            ctx.beginPath()
            ctx.moveTo(8.2, h - 4.5)
            ctx.lineTo(8.2, 3)
            ctx.lineTo(w - 4.3, 4.5)
            ctx.lineTo(w - 4.3, h - 6.5)
            ctx.stroke()
        } else if (icon === "social") {
            ctx.beginPath()
            ctx.moveTo(2.5, h / 2)
            ctx.lineTo(h - 3, 3)
            ctx.lineTo(w / 2 + 1, h - 3)
            ctx.lineTo(w / 2 - 1, h / 2 + 1)
            ctx.closePath()
            ctx.stroke()
            ctx.beginPath()
            ctx.moveTo(w / 2 - 1, h / 2 + 1)
            ctx.lineTo(2.5, h / 2)
            ctx.stroke()
        } else if (icon === "muted") {
            ctx.beginPath()
            ctx.moveTo(3, h / 2 - 3)
            ctx.lineTo(7, h / 2 - 3)
            ctx.lineTo(11, h / 2 - 7)
            ctx.lineTo(11, h / 2 + 7)
            ctx.lineTo(7, h / 2 + 3)
            ctx.lineTo(3, h / 2 + 3)
            ctx.closePath()
            ctx.stroke()
            ctx.beginPath()
            ctx.moveTo(12.5, h / 2 - 3.5)
            ctx.lineTo(16, h / 2 + 3.5)
            ctx.moveTo(16, h / 2 - 3.5)
            ctx.lineTo(12.5, h / 2 + 3.5)
            ctx.stroke()
        } else if (icon === "settings") {
            var rows = [5.5, 9, 12.5]
            var dots = [12, 6, 10.5]
            for (var i = 0; i < rows.length; i++) {
                ctx.beginPath()
                ctx.moveTo(2.5, rows[i])
                ctx.lineTo(w - 2.5, rows[i])
                ctx.stroke()
                ctx.beginPath()
                ctx.arc(dots[i], rows[i], 1.8, 0, Math.PI * 2)
                ctx.fill()
            }
        } else if (icon === "gifs") {
            ctx.beginPath()
            ctx.rect(2.5, 3.5, w - 5, h - 7)
            ctx.stroke()
            ctx.beginPath()
            ctx.moveTo(w / 2 - 2, h / 2 - 3.2)
            ctx.lineTo(w / 2 - 2, h / 2 + 3.2)
            ctx.lineTo(w / 2 + 3.2, h / 2)
            ctx.closePath()
            ctx.fill()
        } else if (icon === "faceit") {
            ctx.beginPath()
            ctx.moveTo(w / 2, 2.5)
            ctx.lineTo(w - 2.5, h / 2)
            ctx.lineTo(w / 2, h - 2.5)
            ctx.lineTo(2.5, h / 2)
            ctx.closePath()
            ctx.stroke()
            ctx.beginPath()
            ctx.arc(w / 2, h / 2, 1.8, 0, Math.PI * 2)
            ctx.fill()
        } else if (icon === "cs2") {
            ctx.beginPath()
            ctx.arc(w / 2, h / 2, 6, 0, Math.PI * 2)
            ctx.stroke()
            ctx.beginPath()
            ctx.moveTo(w / 2, 1.5)
            ctx.lineTo(w / 2, 5)
            ctx.moveTo(w / 2, h - 5)
            ctx.lineTo(w / 2, h - 1.5)
            ctx.moveTo(1.5, h / 2)
            ctx.lineTo(5, h / 2)
            ctx.moveTo(w - 5, h / 2)
            ctx.lineTo(w - 1.5, h / 2)
            ctx.stroke()
            ctx.beginPath()
            ctx.arc(w / 2, h / 2, 1.4, 0, Math.PI * 2)
            ctx.fill()
        } else if (icon === "updates") {
            ctx.beginPath()
            ctx.arc(w / 2, h / 2, 6, Math.PI * 0.15, Math.PI * 1.75, false)
            ctx.stroke()
            ctx.beginPath()
            ctx.moveTo(w / 2 + 6 * Math.cos(Math.PI * 0.15), h / 2 + 6 * Math.sin(Math.PI * 0.15))
            ctx.lineTo(w / 2 + 9, h / 2 - 1)
            ctx.moveTo(w / 2 + 6 * Math.cos(Math.PI * 0.15), h / 2 + 6 * Math.sin(Math.PI * 0.15))
            ctx.lineTo(w / 2 + 4.5, h / 2 + 3)
            ctx.stroke()
        }
    }
}
