import QtQuick
import QtQuick.Layouts
import StreamSoftGui

Item {
    id: root
    property string text: ""
    property string iconName: ""
    property bool active: false
    property bool collapsed: false
    property bool hovered: false
    signal clicked()
    signal hoverEntered()

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
        flatShadow: false
        tintColor: root.active ? Theme.glassFillActive : (root.hovered ? Theme.glassFillHover : Qt.rgba(Theme.glassFillHover.r, Theme.glassFillHover.g, Theme.glassFillHover.b, 0))
        rimColor: root.active ? Theme.glassBorderBright : Qt.rgba(Theme.glassBorderBright.r, Theme.glassBorderBright.g, Theme.glassBorderBright.b, 0)
        Behavior on tintColor { ColorAnimation { duration: Theme.motionFast } }
        Behavior on rimColor { ColorAnimation { duration: Theme.motionFast } }
    }

    HoverHandler { id: hover; onHoveredChanged: if (hovered) root.hoverEntered() }
    TapHandler { id: press; onTapped: root.clicked() }

    Text {
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.leftMargin: 14
        text: root.text
        color: root.active ? "#ffffff" : "#b7b7bd"
        font.pixelSize: Theme.fontMd
        font.weight: Font.Medium
        opacity: root.collapsed ? 0 : 1
        visible: opacity > 0.01

        Behavior on opacity { NumberAnimation { duration: Theme.motionFast } }
        Behavior on color { ColorAnimation { duration: Theme.motionFast } }
    }

    NavIcon {
        anchors.centerIn: parent
        icon: root.iconName
        strokeColor: root.active ? "#ffffff" : "#b7b7bd"
        opacity: root.collapsed ? 1 : 0
        visible: opacity > 0.01

        Behavior on opacity { NumberAnimation { duration: Theme.motionFast } }
    }
}
