import QtQuick
import QtQuick.Layouts
import QtCore
import StreamSoftGui

// Same glass container as GlassCard, but with a clickable title row that
// collapses the rest of the content — added once Connections grew to ~7
// long cards (Twitch/YouTube/Telegram/Multistream/etc all on one page).
// Expanded/collapsed state persists per section (keyed by settingsKey)
// across restarts via QtCore's Settings, same mechanism Main.qml already
// uses for background/theme prefs.
Item {
    id: root
    default property alias content: contentColumn.data
    property string title: ""
    property string subtitle: ""
    required property string settingsKey
    property bool expanded: true

    Settings {
        category: "collapsible_" + root.settingsKey
        property alias expanded: root.expanded
    }

    implicitHeight: outerColumn.implicitHeight + 44

    GlassSurface {
        anchors.fill: parent
    }

    ColumnLayout {
        id: outerColumn
        anchors.fill: parent
        anchors.margins: 22
        spacing: 14

        RowLayout {
            id: headerRow
            Layout.fillWidth: true
            spacing: 10

            HoverHandler { id: headerHover; cursorShape: Qt.PointingHandCursor }
            TapHandler { onTapped: root.expanded = !root.expanded }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text {
                    text: root.title
                    color: Theme.text
                    font.pixelSize: Theme.fontLg
                    font.weight: Font.Bold
                }
                Text {
                    visible: root.subtitle.length > 0
                    text: root.subtitle
                    color: Theme.textFaint
                    font.pixelSize: Theme.fontSm
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }

            Canvas {
                id: chevron
                width: 14
                height: 14
                rotation: root.expanded ? 180 : 0
                Behavior on rotation { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }

                property color strokeColor: headerHover.hovered ? Theme.text : Theme.textDim
                onStrokeColorChanged: requestPaint()
                Component.onCompleted: requestPaint()

                onPaint: {
                    var ctx = getContext("2d")
                    ctx.reset()
                    ctx.strokeStyle = strokeColor
                    ctx.lineWidth = 1.8
                    ctx.lineCap = "round"
                    ctx.lineJoin = "round"
                    ctx.beginPath()
                    ctx.moveTo(2, 4)
                    ctx.lineTo(width / 2, height - 4)
                    ctx.lineTo(width - 2, 4)
                    ctx.stroke()
                }
            }
        }

        Item {
            id: contentWrapper
            Layout.fillWidth: true
            Layout.preferredHeight: root.expanded ? contentColumn.implicitHeight : 0
            clip: true
            Behavior on Layout.preferredHeight { NumberAnimation { duration: 220; easing.type: Easing.InOutCubic } }

            ColumnLayout {
                id: contentColumn
                width: contentWrapper.width
                spacing: 14
                opacity: root.expanded ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: 180 } }
            }
        }
    }
}
