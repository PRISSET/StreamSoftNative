import QtQuick
import QtQuick.Window
import QtQuick.Effects
import StreamSoftGui

Item {
    id: surface
    property real pad: 18
    property real radiusPx: Theme.radiusLg
    property real refractPx: 18.0
    property color tintColor: Theme.glassFill
    property color rimColor: Theme.glassBorderBright
    property color shadowColor: Qt.rgba(0, 0, 0, 1)
    property real specularStrength: 0.5
    property bool flatShadow: true
    property real blurRadius: Window.window && Window.window.blurRadius !== undefined ? Window.window.blurRadius : 3.0

    Rectangle {
        visible: Theme.flatMode
        anchors.fill: parent
        radius: surface.radiusPx
        color: surface.tintColor
        border.width: 1
        border.color: surface.rimColor

        layer.enabled: Theme.flatMode && surface.flatShadow
        layer.effect: MultiEffect {
            shadowEnabled: true
            shadowColor: "#99000000"
            shadowBlur: 0.6
            shadowVerticalOffset: 3
            shadowHorizontalOffset: 0
        }
    }

    ShaderEffectSource {
        id: backdropSource
        property Item src: Window.window ? Window.window.backdropItem : null
        sourceItem: src
        x: -surface.pad
        y: -surface.pad
        width: surface.width + surface.pad * 2
        height: surface.height + surface.pad * 2
        visible: false
        live: !Theme.flatMode
        sourceRect: {
            if (!src) return Qt.rect(0, 0, 0, 0)
            var scrollDep = Window.window.backdropScrollY
            var tickDep = Window.window.layoutTick
            var p = surface.mapToItem(src, -surface.pad, -surface.pad)
            var x = Math.max(0, p.x)
            var y = Math.max(0, p.y)
            var w = Math.max(1, Math.min(width - (x - p.x), src.width - x))
            var h = Math.max(1, Math.min(height - (y - p.y), src.height - y))
            return Qt.rect(x, y, w, h)
        }
    }

    ShaderEffect {
        visible: !Theme.flatMode
        x: -surface.pad
        y: -surface.pad
        width: surface.width + surface.pad * 2
        height: surface.height + surface.pad * 2
        property variant source: backdropSource
        property vector2d itemSize: Qt.vector2d(width, height)
        property vector2d cardSize: Qt.vector2d(surface.width, surface.height)
        property real pad: surface.pad
        property real radiusPx: surface.radiusPx
        property real refractPx: surface.refractPx
        property color tintColor: surface.tintColor
        property color rimColor: surface.rimColor
        property color shadowColor: surface.shadowColor
        property real specularStrength: surface.specularStrength
        property real blurRadius: surface.blurRadius
        vertexShader: "qrc:/qt/qml/StreamSoftGui/shaders/glass.vert.qsb"
        fragmentShader: "qrc:/qt/qml/StreamSoftGui/shaders/glass.frag.qsb"
    }
}
