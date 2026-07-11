import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import StreamSoftGui

Switch {
    id: root
    spacing: 10
    hoverEnabled: true

    indicator: Item {
        x: root.leftPadding
        y: root.topPadding + (root.availableHeight - height) / 2
        width: 46
        height: 26

        GlassSurface {
            id: track
            anchors.fill: parent
            radiusPx: height / 2
            pad: 8
            refractPx: 7
            specularStrength: 0.6
            tintColor: root.checked
                ? (root.hovered ? "#596ee7a8" : "#406ee7a8")
                : (root.hovered ? Theme.glassFillHover : Theme.glassFill)
            Behavior on tintColor { ColorAnimation { duration: Theme.motionFast } }
        }

        Rectangle {
            visible: root.checked
            anchors.fill: track
            radius: track.radiusPx
            border.width: 1
            border.color: "#5a6ee7a8"
            color: "transparent"
        }

        Rectangle {
            visible: root.checked
            anchors.fill: track
            radius: track.radiusPx
            gradient: Gradient {
                GradientStop { position: 0.0; color: "#4dffffff" }
                GradientStop { position: 0.55; color: "#00ffffff" }
            }
        }

        Rectangle {
            id: knob
            x: root.checked ? parent.width - width - 3 : 3
            y: 3
            width: 20
            height: 20
            radius: 10
            color: "#ffffff"
            Behavior on x { NumberAnimation { duration: Theme.motionMed; easing.type: Theme.motionEasing } }

            layer.enabled: true
            layer.effect: MultiEffect {
                shadowEnabled: true
                shadowColor: "#80000000"
                shadowBlur: 0.5
                shadowVerticalOffset: 1
            }
        }
    }

    contentItem: Text {
        text: root.text
        color: Theme.text
        font.pixelSize: Theme.fontLg
        leftPadding: root.indicator.width + root.spacing
        verticalAlignment: Text.AlignVCenter
    }
}
