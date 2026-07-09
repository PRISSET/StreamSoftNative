import QtQuick
import QtQuick.Layouts
import QtQuick.Effects
import StreamSoftGui

ColumnLayout {
    property alias title: titleText.text
    property alias subtitle: subText.text
    Layout.bottomMargin: 4
    spacing: 4

    // Headers sit directly on the raw app backdrop (not inside a tinted
    // GlassCard) — a soft drop shadow keeps them readable against busy or
    // bright wallpapers instead of only the dark ones.
    ColumnLayout {
        Layout.fillWidth: true
        spacing: 4
        layer.enabled: true
        layer.effect: MultiEffect {
            shadowEnabled: true
            shadowColor: "#d9000000"
            shadowBlur: 0.7
            shadowVerticalOffset: 1
            shadowHorizontalOffset: 0
        }

        Text {
            id: titleText
            color: Theme.text
            font.pixelSize: Theme.fontTitle
            font.weight: Font.Bold
        }
        Text {
            id: subText
            color: Theme.textDim
            font.pixelSize: Theme.fontMd
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            visible: text.length > 0
        }
    }
}
