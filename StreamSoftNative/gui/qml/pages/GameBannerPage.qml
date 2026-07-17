import QtQuick
import QtQuick.Layouts
import StreamSoftGui

// Faceit/CS2/Dota 2 used to be three separate top-level nav pages even
// though they're all settings for the exact same shared OBS banner (see
// core/web/faceit.html, obs_client.hpp's "StreamSoft Faceit" source) — that
// split read as three unrelated features instead of one, so they're grouped
// here behind sub-tabs instead. The underlying pages/logic are untouched.
ColumnLayout {
    id: root
    spacing: 18

    property alias faceitPage: faceitInner
    property alias cs2Page: cs2Inner
    property alias dotaPage: dotaInner

    property int subIndex: 0
    readonly property var subTabs: [
        { text: "Faceit", index: 0 },
        { text: "CS2", index: 1 },
        { text: "Dota 2", index: 2 }
    ]

    SectionHeader {
        Layout.fillWidth: true
        title: "Игровой баннер"
        subtitle: "Один баннер на оверлее — сам переключается между Faceit/CS2 и Dota 2 в зависимости от того, какая игра сейчас запущена. Настройки каждой игры — во вкладках ниже."
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 6

        Repeater {
            model: root.subTabs
            delegate: Rectangle {
                required property var modelData
                readonly property bool active: root.subIndex === modelData.index
                implicitWidth: subLabel.implicitWidth + 28
                implicitHeight: 34
                radius: Theme.pill
                color: active ? Theme.glassFillActive : "transparent"
                border.width: 1
                border.color: active ? Theme.glassBorderBright : Theme.hairline
                Behavior on color { ColorAnimation { duration: Theme.motionFast } }

                Text {
                    id: subLabel
                    anchors.centerIn: parent
                    text: modelData.text
                    color: parent.active ? Theme.text : Theme.textDim
                    font.pixelSize: Theme.fontSm
                    font.weight: Font.DemiBold
                }

                TapHandler { onTapped: root.subIndex = modelData.index; cursorShape: Qt.PointingHandCursor }
            }
        }
        Item { Layout.fillWidth: true }
    }

    StackLayout {
        id: gameStack
        Layout.fillWidth: true
        currentIndex: root.subIndex

        FaceitPage { id: faceitInner; width: gameStack.width }
        Cs2Page { id: cs2Inner; width: gameStack.width }
        DotaPage { id: dotaInner; width: gameStack.width }
    }
}
