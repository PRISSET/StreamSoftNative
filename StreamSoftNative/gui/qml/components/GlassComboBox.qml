import QtQuick
import QtQuick.Controls
import StreamSoftGui

// Canvas beats "two rotated Rectangles" for a chevron — that approach needs
// exact rotation/transformOrigin math to land both bars on the same point,
// and a small error there is exactly what produced the lopsided "mutant"
// arrow. A Canvas path is just three coordinates, unambiguous.

// Draws its own chevron instead of relying on the native style's arrow
// primitive — the Qt Widgets version silently had no visible arrow at all
// and the cause was never pinned down, so this sidesteps the whole question.
ComboBox {
    id: root
    font.pixelSize: Theme.fontLg
    implicitHeight: 40

    background: GlassSurface {
        radiusPx: Theme.radiusMd
        pad: 10
        refractPx: 9
        specularStrength: root.activeFocus || root.popup.visible ? 0.55 : 0.4
        tintColor: root.activeFocus || root.popup.visible ? Theme.glassFillHover : Theme.glassFill
        rimColor: root.activeFocus || root.popup.visible ? Theme.glassBorderBright : Theme.glassBorder
        Behavior on tintColor { ColorAnimation { duration: Theme.motionFast } }
        Behavior on rimColor { ColorAnimation { duration: Theme.motionFast } }
    }

    contentItem: Text {
        text: root.displayText
        color: Theme.text
        font: root.font
        leftPadding: 12
        rightPadding: 28
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    // Hand-drawn chevron instead of a Unicode glyph — the "⌄" character
    // isn't covered by the fallback font here and rendered as a plain "v".
    indicator: Canvas {
        id: chevron
        width: 12
        height: 8
        x: root.width - width - 15
        y: root.topPadding + (root.availableHeight - height) / 2
        rotation: root.popup.visible ? 180 : 0
        Behavior on rotation { NumberAnimation { duration: 120 } }

        property color strokeColor: Theme.textDim
        onStrokeColorChanged: requestPaint()
        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            ctx.strokeStyle = strokeColor
            ctx.lineWidth = 1.6
            ctx.lineCap = "round"
            ctx.lineJoin = "round"
            ctx.beginPath()
            ctx.moveTo(1, 2)
            ctx.lineTo(width / 2, height - 2)
            ctx.lineTo(width - 1, 2)
            ctx.stroke()
        }
    }

    popup: Popup {
        y: root.height + 4
        width: root.width
        implicitHeight: Math.min(contentItem.implicitHeight + 8, 220)
        padding: 4

        background: GlassSurface {
            radiusPx: Theme.radiusMd
            pad: 10
            refractPx: 9
            specularStrength: 0.4
            tintColor: Qt.rgba(0.078, 0.078, 0.086, 0.85)
        }

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: root.popup.visible ? root.delegateModel : null
            currentIndex: root.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator {}
        }
    }

    delegate: ItemDelegate {
        id: itemDelegate
        required property var modelData
        required property int index
        width: root.width
        height: 36
        highlighted: root.highlightedIndex === index

        contentItem: Text {
            // `model` here is a list of {text, value} objects (see textRole
            // usage in every GlassComboBox instance) — modelData is the
            // *whole object*, not a string, so it must be indexed by
            // textRole. Assigning the raw object to `text` silently
            // stringifies to "" instead of throwing, which is why this
            // rendered as a blank row before.
            text: root.textRole ? itemDelegate.modelData[root.textRole] : itemDelegate.modelData
            color: Theme.text
            font: root.font
            leftPadding: 8
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            radius: Theme.radiusMd - 4
            color: itemDelegate.highlighted ? Theme.glassFillHover : "#00ffffff"
            Behavior on color { ColorAnimation { duration: Theme.motionFast } }
        }
    }
}
