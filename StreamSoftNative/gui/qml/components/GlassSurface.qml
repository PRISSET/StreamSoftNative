import QtQuick
import QtQuick.Window
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
    property real blurRadius: Window.window && Window.window.blurRadius !== undefined ? Window.window.blurRadius : 3.0

    ShaderEffectSource {
        id: backdropSource
        property Item src: Window.window ? Window.window.backdropItem : null
        sourceItem: src
        x: -surface.pad
        y: -surface.pad
        width: surface.width + surface.pad * 2
        height: surface.height + surface.pad * 2
        visible: false
        live: true
        sourceRect: {
            if (!src) return Qt.rect(0, 0, 0, 0)
            var scrollDep = Window.window.backdropScrollY
            var tickDep = Window.window.layoutTick
            var p = surface.mapToItem(src, -surface.pad, -surface.pad)
            return Qt.rect(p.x, p.y, Math.max(1, width), Math.max(1, height))
        }
    }

    ShaderEffect {
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
