import QtQuick
import QtQuick.Layouts
import StreamSoftGui

Item {
    id: root
    property string text: ""
    property bool active: false
    signal clicked()

    Layout.fillWidth: true
    implicitHeight: 40
    scale: press.pressed ? 0.97 : 1.0
    Behavior on scale { NumberAnimation { duration: Theme.motionFast; easing.type: Theme.motionEasing } }

    GlassSurface {
        anchors.fill: parent
        radiusPx: height / 2
        pad: 10
        refractPx: 8
        specularStrength: 0.5
        tintColor: root.active ? Theme.glassFillActive : (hover.hovered ? Theme.glassFillHover : "#00ffffff")
        rimColor: root.active ? Theme.glassBorderBright : "#00ffffff"
        Behavior on tintColor { ColorAnimation { duration: Theme.motionFast } }
        Behavior on rimColor { ColorAnimation { duration: Theme.motionFast } }
    }

    HoverHandler { id: hover }
    TapHandler { id: press; onTapped: root.clicked() }

    Text {
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.leftMargin: 14
        text: root.text
        color: root.active ? "#ffffff" : "#b7b7bd"
        font.pixelSize: Theme.fontMd
        font.weight: Font.Medium

        Behavior on color { ColorAnimation { duration: Theme.motionFast } }
    }
}
