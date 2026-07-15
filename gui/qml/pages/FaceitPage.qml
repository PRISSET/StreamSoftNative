import QtQuick
import QtQuick.Layouts
import StreamSoftGui

ColumnLayout {
    id: root
    spacing: 18

    property bool loading: false
    property var snapshot: ({ valid: false })

    function load() {
        loading = true
        api.get("/api/connections", function (ok, data) {
            if (!ok) { loading = false; return }
            nicknameField.text = data.faceit_nickname || ""
            apiKeyField.text = data.faceit_api_key || ""
            ownKeyToggle.checked = !!(data.faceit_api_key && data.faceit_api_key.length > 0)
            enabledToggle.checked = !!data.faceit_enabled
            loading = false
        })
    }

    function save() {
        if (loading) return
        api.post("/api/connections", {
            faceit_nickname: nicknameField.text,
            faceit_api_key: ownKeyToggle.checked ? apiKeyField.text : "",
            faceit_enabled: enabledToggle.checked
        }, function (ok) {
            statusText.text = ok ? "Сохранено, обновление придёт в течение пары минут." : "Не удалось сохранить."
            statusTimer.restart()
        })
    }

    function refreshSnapshot() {
        api.get("/api/faceit/snapshot", function (ok, data) {
            if (ok) root.snapshot = data
        })
    }

    Component.onCompleted: { load(); refreshSnapshot() }

    Timer {
        interval: 15000
        running: true
        repeat: true
        onTriggered: root.refreshSnapshot()
    }

    SectionHeader {
        Layout.fillWidth: true
        title: "Faceit"
        subtitle: "Виджет на оверлее: ник, актуальное ELO и последние 5 матчей (CS2). Обновляется само каждые ~1.5 минуты."
    }

    GlassCard {
        Layout.fillWidth: true

        Text { text: "Никнейм на Faceit"; color: Theme.textDim; font.pixelSize: Theme.fontMd; font.bold: true }
        GlassTextField {
            id: nicknameField
            Layout.fillWidth: true
            placeholderText: "s1mple"
            onEditingFinished: root.save()
        }
        Text {
            text: "Больше ничего вводить не нужно — статистика подтягивается сразу, приложение пользуется общим ключом StreamSoft."
            color: Theme.textFaint
            font.pixelSize: Theme.fontSm
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        GlassToggle { id: enabledToggle; text: "Показывать виджет на оверлее"; onToggled: root.save() }
    }

    GlassCard {
        Layout.fillWidth: true

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            GlassToggle {
                id: ownKeyToggle
                text: "Использовать свой API-ключ"
                onToggled: root.save()
            }
            Item { Layout.fillWidth: true }
            LinkChip { text: "Получить ключ →"; url: "https://developers.faceit.com/apps" }
        }
        Text {
            text: "Нужно только если общий ключ StreamSoft упрётся в лимит запросов (маловероятно) — тогда зарегистрируй Server-side приложение на developers.faceit.com и вставь ключ сюда."
            color: Theme.textFaint
            font.pixelSize: Theme.fontSm
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        GlassTextField {
            id: apiKeyField
            Layout.fillWidth: true
            visible: ownKeyToggle.checked
            placeholderText: "00000000-0000-0000-0000-000000000000"
            echoMode: TextInput.Password
            onEditingFinished: root.save()
        }
    }

    GlassCard {
        Layout.fillWidth: true
        visible: root.snapshot.valid

        SectionHeader {
            Layout.fillWidth: true
            title: root.snapshot.nickname || ""
            subtitle: "ELO " + (root.snapshot.elo || "—") + " · уровень " + (root.snapshot.skill_level || "?")
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Repeater {
                model: root.snapshot.matches || []
                delegate: Rectangle {
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: 44
                    radius: Theme.radiusSm
                    color: modelData.win ? "#336ee7a8" : "#33ff7882"
                    border.width: 1
                    border.color: modelData.win ? Theme.good : Theme.bad
                    ColumnLayout {
                        anchors.centerIn: parent
                        spacing: 1
                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            text: modelData.win ? "W" : "L"
                            color: modelData.win ? Theme.good : Theme.bad
                            font.bold: true
                            font.pixelSize: Theme.fontSm
                        }
                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            visible: modelData.kills >= 0 && modelData.deaths >= 0
                            text: modelData.kills + "/" + modelData.deaths
                            color: Theme.textFaint
                            font.pixelSize: 10
                        }
                    }
                }
            }
        }
    }

    Text {
        visible: !root.snapshot.valid && !!root.snapshot.error
        text: root.snapshot.error || ""
        color: Theme.bad
        font.pixelSize: Theme.fontSm
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
    }

    Text {
        id: statusText
        color: Theme.good
        font.pixelSize: Theme.fontSm
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
        opacity: 0
        Behavior on opacity { NumberAnimation { duration: 200 } }
    }
    Timer {
        id: statusTimer
        interval: 4000
        onTriggered: statusText.opacity = 0
        onRunningChanged: if (running) statusText.opacity = 1
    }

    Item { Layout.fillHeight: true }
}
