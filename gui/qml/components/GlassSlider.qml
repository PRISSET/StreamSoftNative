import QtQuick
import QtQuick.Controls
import StreamSoftGui

Slider {
    id: root
    hoverEnabled: true

    background: Item {
        x: root.leftPadding
        y: root.topPadding + root.availableHeight / 2 - height / 2
        width: root.availableWidth
        height: 8

        GlassSurface {
            anchors.fill: parent
            radiusPx: 4
            pad: 6
            refractPx: 5
            specularStrength: 0.4
            flatShadow: false
        }

        Rectangle {
            width: root.visualPosition * parent.width
            height: parent.height
            radius: 4
            color: Theme.accent
        }
    }

    handle: Item {
        readonly property int dotSize: 17
        readonly property int boxSize: 30

        x: root.leftPadding + root.visualPosition * (root.availableWidth - dotSize) - (boxSize - dotSize) / 2
        y: root.topPadding + root.availableHeight / 2 - boxSize / 2
        width: boxSize
        height: boxSize

        Rectangle {
            anchors.centerIn: parent
            width: root.pressed ? parent.boxSize : (root.hovered ? parent.boxSize * 0.85 : parent.dotSize)
            height: width
            radius: width / 2
            color: "transparent"
            border.width: 1
            border.color: Theme.glassFillActive
            opacity: root.hovered || root.pressed ? 1.0 : 0.0
            Behavior on width { NumberAnimation { duration: Theme.motionFast; easing.type: Theme.motionEasing } }
            Behavior on opacity { NumberAnimation { duration: Theme.motionFast } }
        }

        Rectangle {
            anchors.centerIn: parent
            width: root.pressed ? parent.dotSize + 2 : parent.dotSize
            height: width
            radius: width / 2
            color: "#ffffff"
            Behavior on width { NumberAnimation { duration: Theme.motionFast; easing.type: Theme.motionEasing } }
        }
    }
}
