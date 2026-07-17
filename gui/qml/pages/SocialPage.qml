import QtQuick
import QtQuick.Layouts
import StreamSoftGui

ColumnLayout {
    id: root
    spacing: 18

    property bool loading: false

    function load() {
        loading = true
        api.get("/api/connections", function (ok, data) {
            if (!ok) { loading = false; return }
            telegramChannelId.text = data.social_telegram_channel_id || ""
            telegramAnnounceToggle.checked = !!data.social_telegram_enabled
            loading = false
        })
    }

    Component.onCompleted: load()

    function save() {
        if (loading) return
        api.post("/api/connections", {
            social_telegram_channel_id: telegramChannelId.text,
            social_telegram_enabled: telegramAnnounceToggle.checked
        }, function (ok) {
            statusText.text = ok ? "Сохранено." : "Не удалось сохранить."
            statusTimer.restart()
        })
    }

    SectionHeader {
        Layout.fillWidth: true
        title: "Соцсети"
        subtitle: "Автопостинг о начале стрима. Сейчас доступен только Telegram-канал — остальные площадки добавим позже."
    }

    CollapsibleCard {
        Layout.fillWidth: true
        settingsKey: "social_telegram"
        title: "Telegram-канал"
        subtitle: "Публикация идёт через бота со страницы «Подключения» — токен здесь вводить не нужно."

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            LinkChip { text: "Узнать ID канала →"; url: "https://t.me/userinfobot" }
            Item { Layout.fillWidth: true }
            PillButton {
                text: guide.visible ? "Скрыть инструкцию" : "Как настроить?"
                implicitHeight: 30
                onClicked: guide.visible = !guide.visible
            }
        }

        ColumnLayout {
            id: guide
            Layout.fillWidth: true
            visible: false
            spacing: 6

            Text { text: "1. На странице «Подключения» должен быть настроен Telegram-бот (токен от @BotFather) — этот же бот публикует посты в канал."; color: Theme.textDim; font.pixelSize: Theme.fontSm; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Text { text: "2. Добавь бота администратором в свой канал: Настройки канала → Администраторы → Добавить администратора, с правом публикации сообщений."; color: Theme.textDim; font.pixelSize: Theme.fontSm; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Text { text: "3. Если канал публичный — впиши его юзернейм с собакой, например <b>@my_channel</b>. Если приватный — нужен числовой ID: перешли любой пост из канала боту @userinfobot (кнопка выше), он покажет ID вида -100..."; color: Theme.textDim; font.pixelSize: Theme.fontSm; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        }

        Text { text: "ID или юзернейм канала"; color: Theme.textDim; font.pixelSize: Theme.fontMd; font.bold: true }
        GlassTextField {
            id: telegramChannelId
            Layout.fillWidth: true
            placeholderText: "@my_channel или -1001234567890"
            onEditingFinished: root.save()
        }

        GlassToggle { id: telegramAnnounceToggle; text: "Публиковать пост в канал при старте стрима"; onToggled: root.save() }
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
