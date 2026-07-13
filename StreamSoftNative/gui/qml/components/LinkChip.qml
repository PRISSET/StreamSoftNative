import QtQuick
import StreamSoftGui

Item {
    id: root
    property string text: ""
    property string url: ""
    scale: press.pressed ? 0.96 : 1.0
    implicitWidth: label.implicitWidth + 24
    implicitHeight: 30

    Behavior on scale { NumberAnimation { duration: Theme.motionFast; easing.type: Theme.motionEasing } }

    GlassSurface {
        anchors.fill: parent
        radiusPx: height / 2
        pad: 8
        refractPx: 7
        specularStrength: 0.45
        flatShadow: false
        tintColor: hover.hovered ? Theme.glassFillHover : Theme.glassFill
        rimColor: hover.hovered ? Theme.fieldBorderHover : Theme.glassBorder
        Behavior on tintColor { ColorAnimation { duration: Theme.motionFast } }
        Behavior on rimColor { ColorAnimation { duration: Theme.motionFast } }
    }

    HoverHandler { id: hover }
    TapHandler {
        id: press
        enabled: root.url.length > 0
        onTapped: Qt.openUrlExternally(root.url)
        cursorShape: Qt.PointingHandCursor
    }

    Text {
        id: label
        anchors.centerIn: parent
        text: root.text
        color: "#b7b7bd"
        font.pixelSize: Theme.fontSm
    }
}
