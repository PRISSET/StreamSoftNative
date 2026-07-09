pragma Singleton
import QtQuick

QtObject {
    readonly property color bg: "#08080a"

    // Liquid Glass material language — one consistent set of tokens every
    // glass surface (cards, buttons, fields, sliders, toggles, nav) pulls
    // from, so a bright frosted panel reads as glass against the near-black
    // window instead of blending into it. `panel`/`borderTop`/`borderSide`/
    // `hairline` kept as aliases of the new tokens — several components
    // (NavButton, LinkChip) still refer to them by the old names.
    // Deliberately faint fills: the frost on real glass is barely-there —
    // the visible "material" is the blurred backdrop showing through, not
    // the fill itself. Cranking these up just paints grey slabs.
    readonly property color glassFill: "#10ffffff"
    readonly property color glassFillHover: "#1cffffff"
    readonly property color glassFillActive: "#2affffff"
    readonly property color glassBorder: "#38ffffff"
    readonly property color glassBorderBright: "#80ffffff"
    readonly property color glassHighlight: "#70ffffff"
    readonly property color glassGlow: "#ffffff"

    readonly property color panel: glassFill
    readonly property color borderTop: glassBorderBright
    readonly property color borderSide: glassBorder
    readonly property color hairline: glassBorder

    readonly property color text: "#f5f5f6"
    readonly property color textDim: "#9a9aa1"
    readonly property color textFaint: "#7d7d84"
    readonly property color placeholder: "#6b6b72"

    readonly property color fieldBg: "#12ffffff"
    readonly property color fieldBorder: glassBorder
    readonly property color fieldBorderHover: "#55ffffff"

    readonly property color good: "#6ee7a8"
    readonly property color bad: "#ff7882"
    readonly property color badBg: "#24ff7882"
    readonly property color badBgHover: "#33ff7882"
    readonly property color badBorder: "#57ff7882"

    readonly property int radiusLg: 28
    readonly property int radiusMd: 16
    readonly property int radiusSm: 10
    readonly property int pill: 999

    // Standard hover/press/focus transition timings — every interactive
    // glass element animates through these instead of snapping.
    readonly property int motionFast: 120
    readonly property int motionMed: 220
    readonly property int motionEasing: Easing.OutCubic

    readonly property int fontSm: 12
    readonly property int fontMd: 13
    readonly property int fontLg: 14
    readonly property int fontTitle: 19
}
