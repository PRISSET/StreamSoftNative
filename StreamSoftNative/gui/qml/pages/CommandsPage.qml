import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import StreamSoftGui

ColumnLayout {
    id: root
    spacing: 18

    property var commands: []

    function refresh() {
        api.get("/api/commands", function (ok, data) {
            if (ok) root.commands = data.commands || []
        })
    }

    function addCommand() {
        var trigger = triggerField.text.trim()
        var response = responseField.text.trim()
        if (!trigger || !response) return
        api.post("/api/commands", {
            trigger: trigger,
            response: response,
            cooldown: cooldownSpin.value
        }, function () {
            triggerField.text = ""
            responseField.text = ""
            cooldownSpin.value = 15
            root.refresh()
        })
    }

    Component.onCompleted: refresh()

    SectionHeader {
        Layout.fillWidth: true
        title: "Команды чата"
        subtitle: "Автоответы по триггеру (!команда) с кулдауном."
    }

    CollapsibleCard {
        Layout.fillWidth: true
        settingsKey: "commands_add"
        title: "Добавить команду"

        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            GlassTextField {
                id: triggerField
                Layout.preferredWidth: 140
                placeholderText: "!discord"
            }
            GlassTextField {
                id: responseField
                Layout.fillWidth: true
                placeholderText: "Текст ответа"
            }
            SpinBox {
                id: cooldownSpin
                from: 0
                to: 3600
                value: 15
                editable: true
                implicitHeight: 40
                implicitWidth: 100
                background: Rectangle {
                    radius: Theme.radiusMd
                    color: Theme.fieldBg
                    border.width: 1
                    border.color: Theme.fieldBorder
                }
                contentItem: TextInput {
                    text: cooldownSpin.textFromValue(cooldownSpin.value, cooldownSpin.locale)
                    color: Theme.text
                    font.pixelSize: Theme.fontLg
                    horizontalAlignment: Qt.AlignHCenter
                    verticalAlignment: Qt.AlignVCenter
                    readOnly: !cooldownSpin.editable
                    selectByMouse: true
                }
                up.indicator: Item {}
                down.indicator: Item {}
            }
            PillButton {
                text: "Добавить"
                onClicked: root.addCommand()
            }
        }
    }

    CollapsibleCard {
        Layout.fillWidth: true
        settingsKey: "commands_list"
        title: "Список"

        RowLayout {
            Layout.fillWidth: true
            Text { text: "ТРИГГЕР"; color: Theme.textFaint; font.pixelSize: Theme.fontSm; font.bold: true; Layout.preferredWidth: 120 }
            Text { text: "ОТВЕТ"; color: Theme.textFaint; font.pixelSize: Theme.fontSm; font.bold: true; Layout.fillWidth: true }
            Text { text: "КУЛДАУН"; color: Theme.textFaint; font.pixelSize: Theme.fontSm; font.bold: true; Layout.preferredWidth: 80 }
            Item { Layout.preferredWidth: 36 }
        }

        Repeater {
            model: root.commands
            delegate: RowLayout {
                required property var modelData
                Layout.fillWidth: true

                Text { text: modelData.trigger; color: Theme.text; font.pixelSize: Theme.fontMd; Layout.preferredWidth: 120; elide: Text.ElideRight }
                Text { text: modelData.response; color: Theme.textDim; font.pixelSize: Theme.fontMd; Layout.fillWidth: true; elide: Text.ElideRight }
                Text { text: modelData.cooldown + "с"; color: Theme.textDim; font.pixelSize: Theme.fontMd; Layout.preferredWidth: 80 }
                PillButton {
                    danger: true
                    text: "✕"
                    implicitHeight: 30
                    implicitWidth: 36
                    onClicked: api.del("/api/commands/" + modelData.trigger, function () { root.refresh() })
                }
            }
        }
    }
}
