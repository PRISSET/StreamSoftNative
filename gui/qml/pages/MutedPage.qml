import QtQuick
import QtQuick.Layouts
import StreamSoftGui

ColumnLayout {
    id: root
    spacing: 18

    property var muted: []

    function applySettings(settings) {
        root.muted = settings.muted || []
    }

    function addMute() {
        var name = muteField.text.trim()
        if (!name) return
        api.post("/api/settings", { mute: name }, function (ok, data) {
            muteField.text = ""
            if (ok) root.muted = data.muted || []
        })
    }

    function unmute(name) {
        api.post("/api/settings", { unmute: name }, function (ok, data) {
            if (ok) root.muted = data.muted || []
        })
    }

    SectionHeader {
        Layout.fillWidth: true
        title: "Замьюченные"
        subtitle: "Их сообщения не читаются вслух и не показываются в оверлее."
    }

    GlassCard {
        Layout.fillWidth: true
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            GlassTextField {
                id: muteField
                Layout.fillWidth: true
                placeholderText: "ник"
                onAccepted: root.addMute()
            }
            PillButton {
                text: "Замьютить"
                onClicked: root.addMute()
            }
        }
    }

    GlassCard {
        Layout.fillWidth: true

        Text {
            visible: root.muted.length === 0
            text: "Никого не замьючено"
            color: Theme.textFaint
            font.pixelSize: Theme.fontMd
        }

        Repeater {
            model: root.muted
            delegate: RowLayout {
                required property string modelData
                Layout.fillWidth: true
                Text { text: modelData; color: Theme.text; font.pixelSize: Theme.fontMd; Layout.fillWidth: true }
                PillButton {
                    danger: true
                    text: "Убрать мьют"
                    implicitHeight: 30
                    onClicked: root.unmute(modelData)
                }
            }
        }
    }

    Item { Layout.fillHeight: true }
}
