import QtQuick
import QtQuick.Controls
import StreamSoftGui

Slider {
    id: root
    hoverEnabled: true

    // The handle is drawn 30px tall (see background below) but QQC2 has no
    // way to know that on its own — background/handle here don't expose an
    // implicitHeight, so the control's own implicitHeight collapses down to
    // roughly just the 8px track. Inside a CollapsibleCard's ColumnLayout,
    // that under-reported height meant the LAST slider in a card had its
    // handle's bottom edge poke past contentWrapper's clip boundary (which
    // is sized from the column's, in turn from each row's, implicit
    // height) — looked exactly like the knob was cut, but only for
    // whichever slider happened to be last in its card. Declaring the real
    // height here is what actually reserves enough row space.
    implicitHeight: 30

    background: Item {
        x: root.leftPadding
        y: root.topPadding + root.availableHeight / 2 - height / 2
        width: root.availableWidth
        height: 8

        readonly property int handleDotSize: 17
        readonly property int handleBoxSize: 30

        Rectangle {
            anchors.fill: parent
            radius: 4
            color: Theme.glassFill
            border.width: 1
            border.color: Theme.glassBorder
        }

        // Lines up with the handle's own center formula below — the fill
        // is meant to end exactly at the dot's center, same as any normal
        // slider — so the dot has to be drawn on top of it.
        Rectangle {
            width: root.visualPosition * (parent.width - parent.handleDotSize) + parent.handleDotSize / 2
            height: parent.height
            radius: 4
            color: Theme.accent
        }

        // The dot/ring live here now, declared after the fill above,
        // instead of in the separate `handle:` delegate slot — QQC2's
        // Basic style doesn't reliably paint `handle` above `background`
        // even with explicit z values set on each (they aren't guaranteed
        // siblings of a common parent, so z comparisons between them are
        // meaningless). That showed up as the fill/track visibly cutting
        // through the middle of the white dot. Plain same-parent sibling
        // order (declared last = painted last = on top) can't have that
        // problem — there's nothing left to race or desync.
        Item {
            id: handleVisual
            readonly property int dotSize: parent.handleDotSize
            readonly property int boxSize: parent.handleBoxSize
            x: root.visualPosition * (parent.width - dotSize) - (boxSize - dotSize) / 2
            y: parent.height / 2 - boxSize / 2
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

    handle: Item {
        // Geometry-only placeholder — Slider wants a `handle` item for its
        // own internal bookkeeping, but nothing here is visible; the real
        // dot/ring were moved into `background` above (see comment there).
        readonly property int dotSize: 17
        readonly property int boxSize: 30

        x: root.leftPadding + root.visualPosition * (root.availableWidth - dotSize) - (boxSize - dotSize) / 2
        y: root.topPadding + root.availableHeight / 2 - boxSize / 2
        width: boxSize
        height: boxSize
    }
}
