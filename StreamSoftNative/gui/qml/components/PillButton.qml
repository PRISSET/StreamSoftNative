import QtQuick
import QtQuick.Effects
import StreamSoftGui

Rectangle {
    id: root
    signal clicked()
    property string text: ""
    property bool danger: false

    implicitWidth: label.implicitWidth + 36
    implicitHeight: 40
    radius: Theme.pill
    opacity: enabled ? 1.0 : 0.5
    scale: press.pressed ? 0.96 : 1.0
    color: danger
        ? (hover.hovered ? Theme.badBgHover : Theme.badBg)
        : (hover.hovered ? "#ffffff" : "#eeeef1")
    border.width: danger ? 1 : 0
    border.color: Theme.badBorder

    layer.enabled: !danger
    layer.effect: MultiEffect {
        shadowEnabled: true
        shadowColor: "#70000000"
        shadowBlur: 0.5
        shadowVerticalOffset: press.pressed ? 1 : (hover.hovered ? 3 : 2)
        shadowHorizontalOffset: 0
        Behavior on shadowVerticalOffset { NumberAnimation { duration: Theme.motionFast; easing.type: Theme.motionEasing } }
    }

    Behavior on color { ColorAnimation { duration: Theme.motionFast } }
    Behavior on scale { NumberAnimation { duration: Theme.motionFast; easing.type: Theme.motionEasing } }

    HoverHandler { id: hover; enabled: root.enabled }
    TapHandler { id: press; enabled: root.enabled; onTapped: root.clicked() }

    Rectangle {
        visible: !root.danger && !Theme.flatMode
        anchors.fill: parent
        radius: parent.radius
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#4dffffff" }
            GradientStop { position: 0.55; color: "#00ffffff" }
        }
    }

    Text {
        id: label
        anchors.centerIn: parent
        text: root.text
        color: root.danger ? "#ffc2c8" : "#0c0c0d"
        font.pixelSize: Theme.fontMd
        font.weight: Font.DemiBold
    }
}
