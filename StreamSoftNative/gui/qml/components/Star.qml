import QtQuick
import QtQuick.Effects

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
