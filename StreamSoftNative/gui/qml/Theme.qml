pragma Singleton
import QtQuick

QtObject {
    id: theme

    property bool flatMode: false

    readonly property color bg: flatMode ? "#1b1b1d" : "#08080a"

    readonly property color glassFill: flatMode ? "#242428" : "#10ffffff"
    readonly property color glassFillHover: flatMode ? "#2c2c31" : "#1cffffff"
    readonly property color glassFillActive: flatMode ? "#333338" : "#2affffff"
    readonly property color glassBorder: flatMode ? "#333338" : "#38ffffff"
    readonly property color glassBorderBright: flatMode ? "#4c4c54" : "#80ffffff"
    readonly property color glassHighlight: flatMode ? "#00000000" : "#70ffffff"
    readonly property color glassGlow: flatMode ? "#4c4c54" : "#ffffff"

    readonly property color panel: glassFill
    readonly property color borderTop: glassBorderBright
    readonly property color borderSide: glassBorder
    readonly property color hairline: glassBorder

    readonly property color text: "#f5f5f6"
    readonly property color textDim: "#9a9aa1"
    readonly property color textFaint: "#7d7d84"
    readonly property color placeholder: "#6b6b72"

    readonly property color fieldBg: flatMode ? "#232326" : "#12ffffff"
    readonly property color fieldBorder: glassBorder
    readonly property color fieldBorderHover: flatMode ? accent : "#55ffffff"

    readonly property color good: "#6ee7a8"
    readonly property color bad: "#ff7882"
    readonly property color badBg: "#24ff7882"
    readonly property color badBgHover: "#33ff7882"
    readonly property color badBorder: "#57ff7882"

    readonly property color accent: flatMode ? "#ff6c37" : "#6ee7a8"
    function accentAlpha(a) { return Qt.rgba(accent.r, accent.g, accent.b, a) }

    readonly property int radiusLg: 28
    readonly property int radiusMd: 16
    readonly property int radiusSm: 10
    readonly property int pill: 999

    readonly property int motionFast: 120
    readonly property int motionMed: 220
    readonly property int motionEasing: Easing.OutCubic

    readonly property int fontSm: 12
    readonly property int fontMd: 13
    readonly property int fontLg: 14
    readonly property int fontTitle: 19
}
