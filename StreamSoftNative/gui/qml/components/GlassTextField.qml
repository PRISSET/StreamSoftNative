import QtQuick
import QtQuick.Controls
import StreamSoftGui

TextField {
    id: root
    color: Theme.text
    placeholderTextColor: Theme.placeholder
    font.pixelSize: Theme.fontLg
    selectByMouse: true
    leftPadding: 12
    rightPadding: 12
    topPadding: 10
    bottomPadding: 10
    implicitHeight: 40

    background: GlassSurface {
        radiusPx: Theme.radiusMd
        pad: 10
        refractPx: 9
        specularStrength: root.activeFocus ? 0.55 : 0.4
        tintColor: root.activeFocus ? Theme.glassFillHover : Theme.glassFill
        rimColor: root.activeFocus ? (Theme.flatMode ? Theme.accent : Theme.glassBorderBright) : (hoverHandler.hovered ? Theme.fieldBorderHover : Theme.glassBorder)
        Behavior on tintColor { ColorAnimation { duration: Theme.motionFast } }
        Behavior on rimColor { ColorAnimation { duration: Theme.motionFast } }
    }

    HoverHandler { id: hoverHandler }
}
