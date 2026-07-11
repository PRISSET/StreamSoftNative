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
            twitchClientId.text = data.twitch_client_id || ""
            twitchChannel.text = data.twitch_channel || ""
            twitchChatToggle.checked = !!data.twitch_chat_enabled
            twitchEventsToggle.checked = !!data.twitch_eventsub_enabled

            youtubeApiKey.text = data.youtube_api_key || ""
            youtubeVideoId.text = data.youtube_video_id || ""
            youtubeToggle.checked = !!data.youtube_enabled

            telegramToken.text = data.telegram_bot_token || ""
            telegramChatId.text = data.telegram_chat_id || ""
            telegramToggle.checked = !!data.telegram_enabled
            telegramControlToggle.checked = !!data.telegram_control_enabled

            ttsToggle.checked = !!data.tts_enabled
            loading = false
        })
    }

    Component.onCompleted: load()

    function save() {
        if (loading) return
        api.post("/api/connections", {
            twitch_client_id: twitchClientId.text,
            twitch_channel: twitchChannel.text,
            twitch_chat_enabled: twitchChatToggle.checked,
            twitch_eventsub_enabled: twitchEventsToggle.checked,
            youtube_api_key: youtubeApiKey.text,
            youtube_video_id: youtubeVideoId.text,
            youtube_enabled: youtubeToggle.checked,
            telegram_bot_token: telegramToken.text,
            telegram_chat_id: telegramChatId.text,
            telegram_enabled: telegramToggle.checked,
            telegram_control_enabled: telegramControlToggle.checked,
            tts_enabled: ttsToggle.checked
        }, function (ok) {
            statusText.text = ok
                ? "Сохранено. Twitch/YouTube/Telegram подключатся при следующем запуске."
                : "Не удалось сохранить."
            statusTimer.restart()
        })
    }

    SectionHeader {
        Layout.fillWidth: true
        title: "Подключения"
        subtitle: "Ключи и переключатели для каждого источника. Изменения по Twitch/YouTube/Telegram применяются после перезапуска программы — TTS переключается сразу."
    }

    GlassCard {
        Layout.fillWidth: true
        SectionHeader {
            Layout.fillWidth: true
            title: "Twitch"
            subtitle: "Client ID берётся из своего приложения на dev.twitch.tv — Twitch не даёт общий ключ на всех."
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            LinkChip { text: "Получить Client ID →"; url: "https://dev.twitch.tv/console/apps/create" }
            Item { Layout.fillWidth: true }
            PillButton {
                text: twitchGuide.visible ? "Скрыть инструкцию" : "Как настроить?"
                implicitHeight: 30
                onClicked: twitchGuide.visible = !twitchGuide.visible
            }
        }

        ColumnLayout {
            id: twitchGuide
            Layout.fillWidth: true
            visible: false
            spacing: 6

            Text { text: "1. Открой страницу создания приложения (кнопка выше) и войди под своим Twitch-аккаунтом."; color: Theme.textDim; font.pixelSize: Theme.fontSm; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Text { text: "2. <b>Name</b> — любое имя, например «StreamSoft»."; color: Theme.textDim; font.pixelSize: Theme.fontSm; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Text { text: "3. <b>OAuth Redirect URLs</b> — впиши ровно <b>http://localhost</b> и нажми Add. Twitch требует это поле у любого приложения, но сам StreamSoft им не пользуется — авторизация идёт по одноразовому коду, а не по редиректу."; color: Theme.textDim; font.pixelSize: Theme.fontSm; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Text { text: "4. <b>Category</b> — «Application Integration». <b>Client Type</b> — «Public»."; color: Theme.textDim; font.pixelSize: Theme.fontSm; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Text { text: "5. Нажми Create, открой созданное приложение и скопируй <b>Client ID</b> — вставь в поле ниже."; color: Theme.textDim; font.pixelSize: Theme.fontSm; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Text { text: "6. <b>Канал</b> — просто ник из адреса twitch.tv/ТВОЙ_НИК, без @ и без остальной части ссылки."; color: Theme.textDim; font.pixelSize: Theme.fontSm; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        }

        Text { text: "Client ID"; color: Theme.textDim; font.pixelSize: Theme.fontMd; font.bold: true }
        GlassTextField {
            id: twitchClientId
            Layout.fillWidth: true
            placeholderText: "например, gp762nuuoqcoxypju8c569th9wz7q5"
            onEditingFinished: {
                root.save()
                if (text.length > 0) {
                    api.post("/api/twitch/start-auth", { client_id: text }, function () {}, false)
                }
            }
        }

        Text { text: "Канал (без @)"; color: Theme.textDim; font.pixelSize: Theme.fontMd; font.bold: true }
        GlassTextField {
            id: twitchChannel
            Layout.fillWidth: true
            placeholderText: "мой_канал"
            onEditingFinished: root.save()
        }

        GlassToggle { id: twitchChatToggle; text: "Читать чат Twitch"; onToggled: root.save() }
        GlassToggle { id: twitchEventsToggle; text: "Алерты Twitch (фоллоу/подписки/рейды/донаты)"; onToggled: root.save() }
    }

    GlassCard {
        Layout.fillWidth: true
        SectionHeader {
            Layout.fillWidth: true
            title: "YouTube"
            subtitle: "API-ключ создаётся в Google Cloud Console (включи YouTube Data API v3)."
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            LinkChip { text: "Получить API-ключ →"; url: "https://console.cloud.google.com/apis/credentials" }
            Item { Layout.fillWidth: true }
            PillButton {
                text: youtubeGuide.visible ? "Скрыть инструкцию" : "Как настроить?"
                implicitHeight: 30
                onClicked: youtubeGuide.visible = !youtubeGuide.visible
            }
        }

        ColumnLayout {
            id: youtubeGuide
            Layout.fillWidth: true
            visible: false
            spacing: 6

            Text { text: "1. Открой Google Cloud Console (кнопка выше) и создай проект (или выбери существующий) вверху страницы."; color: Theme.textDim; font.pixelSize: Theme.fontSm; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Text { text: "2. Слева — <b>APIs & Services → Library</b>, найди «YouTube Data API v3» и нажми <b>Enable</b>. Без этого шага ключ не будет работать."; color: Theme.textDim; font.pixelSize: Theme.fontSm; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Text { text: "3. <b>APIs & Services → Credentials → Create Credentials → API key</b> — скопируй появившийся ключ, вставь в поле ниже."; color: Theme.textDim; font.pixelSize: Theme.fontSm; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Text { text: "4. <b>ID видеотрансляции</b> — это не вся ссылка, а часть после <b>v=</b>. Для youtube.com/watch?v=dQw4w9WgXcQ это dQw4w9WgXcQ. У каждой новой трансляции — новый ID, надо менять перед каждым стримом."; color: Theme.textDim; font.pixelSize: Theme.fontSm; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        }

        Text { text: "API-ключ"; color: Theme.textDim; font.pixelSize: Theme.fontMd; font.bold: true }
        GlassTextField {
            id: youtubeApiKey
            Layout.fillWidth: true
            placeholderText: "AIza..."
            onEditingFinished: root.save()
        }

        Text { text: "ID видеотрансляции"; color: Theme.textDim; font.pixelSize: Theme.fontMd; font.bold: true }
        GlassTextField {
            id: youtubeVideoId
            Layout.fillWidth: true
            placeholderText: "из ссылки youtube.com/watch?v=..."
            onEditingFinished: root.save()
        }

        GlassToggle { id: youtubeToggle; text: "Читать чат YouTube"; onToggled: root.save() }
    }

    GlassCard {
        Layout.fillWidth: true
        SectionHeader {
            Layout.fillWidth: true
            title: "Telegram"
            subtitle: "Токен бота — у @BotFather, chat_id — у @userinfobot (или своего чата с ботом)."
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            LinkChip { text: "Создать бота →"; url: "https://t.me/BotFather" }
            LinkChip { text: "Узнать chat_id →"; url: "https://t.me/userinfobot" }
            Item { Layout.fillWidth: true }
            PillButton {
                text: telegramGuide.visible ? "Скрыть инструкцию" : "Как настроить?"
                implicitHeight: 30
                onClicked: telegramGuide.visible = !telegramGuide.visible
            }
        }

        ColumnLayout {
            id: telegramGuide
            Layout.fillWidth: true
            visible: false
            spacing: 6

            Text { text: "1. Напиши @BotFather (кнопка выше) команду /newbot, придумай отображаемое имя и username бота (username должен заканчиваться на «bot», например my_stream_bot)."; color: Theme.textDim; font.pixelSize: Theme.fontSm; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Text { text: "2. BotFather пришлёт токен вида 123456789:ABC-DEF... — вставь его в поле «Токен бота» ниже."; color: Theme.textDim; font.pixelSize: Theme.fontSm; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Text { text: "3. Напиши своему новому боту любое сообщение первым — иначе Telegram не разрешит боту писать тебе (стандартное ограничение Telegram, не связано со StreamSoft)."; color: Theme.textDim; font.pixelSize: Theme.fontSm; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Text { text: "4. Открой @userinfobot (кнопка выше) и напиши ему что угодно — он покажет твой Chat ID числом. Вставь его в поле «Chat ID» ниже."; color: Theme.textDim; font.pixelSize: Theme.fontSm; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        }

        Text { text: "Токен бота"; color: Theme.textDim; font.pixelSize: Theme.fontMd; font.bold: true }
        GlassTextField {
            id: telegramToken
            Layout.fillWidth: true
            placeholderText: "123456:ABC-DEF..."
            onEditingFinished: root.save()
        }

        Text { text: "Chat ID"; color: Theme.textDim; font.pixelSize: Theme.fontMd; font.bold: true }
        GlassTextField {
            id: telegramChatId
            Layout.fillWidth: true
            placeholderText: "например, 123456789"
            onEditingFinished: root.save()
        }

        GlassToggle { id: telegramToggle; text: "Пересылать чат и алерты в Telegram"; onToggled: root.save() }
        GlassToggle { id: telegramControlToggle; text: "Команды из Telegram (/mute, /skip, /volume...)"; onToggled: root.save() }
    }

    GlassCard {
        Layout.fillWidth: true
        SectionHeader {
            Layout.fillWidth: true
            title: "Озвучка"
            subtitle: "Голоса и скорость настраиваются на странице «Озвучка» — здесь только общий выключатель."
        }
        GlassToggle { id: ttsToggle; text: "Озвучка (TTS) включена"; onToggled: root.save() }
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
