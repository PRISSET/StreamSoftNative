import QtQuick
import QtQuick.Effects

// A single 4-point sparkle, drawn as an 8-vertex kite polygon (matches the
// shape of the old static background.jpg exactly — tips on the axes, a
// tight concave waist between them) instead of a raster image, so it can
// glow and drift. Self-contained: instantiate several with different size/
// position/duration and each one animates independently.
Item {
    id: root

    property real size: 24
    property real driftRange: 14
    property int driftDuration: 8000
    property int driftDelay: 0
    property real minOpacity: 0.5
    property real maxOpacity: 1.0
    property color tint: "#ffffff"

    width: size
    height: size

    Canvas {
        id: canvas
        anchors.fill: parent
        renderTarget: Canvas.FramebufferObject
        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            var cx = width / 2
            var cy = height / 2
            var outerR = width / 2
            var innerR = outerR * 0.16
            ctx.beginPath()
            for (var i = 0; i < 8; i++) {
                var ang = i * Math.PI / 4 - Math.PI / 2
                var rad = (i % 2 === 0) ? outerR : innerR
                var x = cx + Math.cos(ang) * rad
                var y = cy + Math.sin(ang) * rad
                if (i === 0) ctx.moveTo(x, y)
                else ctx.lineTo(x, y)
            }
            ctx.closePath()
            ctx.fillStyle = root.tint
            ctx.fill()
        }
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        Component.onCompleted: requestPaint()
    }

    // Wide, soft halo behind the crisp shape — a single MultiEffect shadow
    // pass reads as a subtle drop-shadow at low blur, but pushed toward its
    // max it reads as neon bloom instead. autoPaddingEnabled (default on)
    // lets the blur bleed outside the item's own bounds without needing to
    // oversize the effect item or stretch the source shape.
    MultiEffect {
        anchors.fill: canvas
        source: canvas
        shadowEnabled: true
        shadowColor: root.tint
        shadowBlur: 1.0
        shadowOpacity: 1.0
        shadowHorizontalOffset: 0
        shadowVerticalOffset: 0
        blurMax: Math.max(32, root.size * 1.4)
    }

    opacity: root.minOpacity

    // Drives a plain offset property (fixed numeric targets) rather than
    // animating `y` directly with a `to:` bound back to `y` itself — that
    // would re-evaluate against the animation's own live output every
    // frame and thrash instead of settling into a clean loop.
    property real driftOffset: 0
    transform: Translate { y: root.driftOffset }

    SequentialAnimation on driftOffset {
        loops: Animation.Infinite
        PauseAnimation { duration: root.driftDelay }
        NumberAnimation { to: -root.driftRange; duration: root.driftDuration; easing.type: Easing.InOutSine }
        NumberAnimation { to: root.driftRange; duration: root.driftDuration * 2; easing.type: Easing.InOutSine }
        NumberAnimation { to: 0; duration: root.driftDuration; easing.type: Easing.InOutSine }
    }

    SequentialAnimation on opacity {
        loops: Animation.Infinite
        PauseAnimation { duration: root.driftDelay }
        NumberAnimation { to: root.maxOpacity; duration: root.driftDuration * 1.4; easing.type: Easing.InOutSine }
        NumberAnimation { to: root.minOpacity; duration: root.driftDuration * 1.4; easing.type: Easing.InOutSine }
    }
}
