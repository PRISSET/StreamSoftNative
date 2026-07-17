import QtQuick
import QtQuick.Layouts
import StreamSoftGui

ColumnLayout {
    id: root
    spacing: 18

    property bool loading: false
    property var multistreamStatus: ({ plugin_installed: false, obs_running: false, profile_found: false, synced: false })
    property string installStatus: ""
    property bool installOk: false
    property bool syncOk: false
    property var multistreamServices: [{ name: "Другое (свой RTMP-сервер)", server: "", common: true }]
    property var streamTemplates: []
    property bool youtubeAuthPending: false
    property string youtubeAuthUrl: ""
    property string youtubeAuthCode: ""
    property string youtubeAuthStatusText: ""
    property bool youtubeAuthOk: false
    property string youtubeAuthLastResultKey: ""

    function startYoutubeAuth(forceReauth) {
        youtubeAuthStatusText = "Открываю окно авторизации…"
        var path = forceReauth ? "/api/youtube/reauth" : "/api/youtube/start-auth"
        api.post(path, {}, function (ok, data) {
            if (!ok) { youtubeAuthOk = false; youtubeAuthStatusText = "Не получилось начать авторизацию." }
        }, false)
    }

    function pollYoutubeAuthStatus() {
        api.get("/api/youtube/auth-status", function (ok, data) {
            if (!ok || !data) return
            root.youtubeAuthPending = !!data.pending
            root.youtubeAuthUrl = data.verification_url || ""
            root.youtubeAuthCode = data.user_code || ""
            if (data.pending) return
            var resultKey = (data.last_result || "") + "|" + (data.last_error || "")
            if ((data.last_result || "") === "" || resultKey === root.youtubeAuthLastResultKey) return
            root.youtubeAuthLastResultKey = resultKey
            root.youtubeAuthOk = data.last_result === "success"
            root.youtubeAuthStatusText = root.youtubeAuthOk
                ? "YouTube подключён — название трансляции теперь можно менять."
                : "Ошибка авторизации YouTube: " + (data.last_error || "")
        })
    }

    Timer {
        interval: 3000
        running: true
        repeat: true
        triggeredOnStart: true
        onTriggered: root.pollYoutubeAuthStatus()
    }

    function loadStreamTemplates() {
        api.get("/api/stream-templates", function (ok, data) {
            if (ok && data.templates) root.streamTemplates = data.templates
        })
    }

    function applyStreamTemplate() {
        if (root.streamTemplates.length === 0) return
        var tmpl = root.streamTemplates[templatePicker.currentIndex]
        if (!tmpl) return
        templateApplyStatus.text = "Применяю…"
        templateApplyStatusTimer.restart()
        api.post("/api/stream-templates/" + encodeURIComponent(tmpl.id) + "/apply", {}, function (ok, data) {
            templateApplyStatus.text = ok
                ? (data && data.applied_twitch ? "Применено — Twitch обновится через пару секунд." : "Сохранено (Twitch не подключён).")
                : "Не удалось применить."
            templateApplyStatusTimer.restart()
            if (ok) streamTitle.text = tmpl.title
        }, false)
    }

    function saveStreamTemplate() {
        var name = newTemplateName.text.trim()
        if (!name) return
        api.post("/api/stream-templates", {
            name: name,
            title: newTemplateTitle.text.trim(),
            twitch_game: newTemplateGame.text.trim()
        }, function (ok, data) {
            if (ok && data && data.templates) {
                root.streamTemplates = data.templates
                newTemplateName.text = ""
                newTemplateTitle.text = ""
                newTemplateGame.text = ""
            }
        }, false)
    }

    function deleteStreamTemplate(id) {
        api.del("/api/stream-templates/" + encodeURIComponent(id), function () { root.loadStreamTemplates() })
    }

    function loadMultistreamServices() {
        api.get("/api/multistream/services", function (ok, data) {
            if (!ok || !data.services) return
            var list = data.services.slice()
            list.sort(function (a, b) {
                if (a.common !== b.common) return a.common ? -1 : 1
                return a.name.localeCompare(b.name)
            })
            list.unshift({ name: "Другое (свой RTMP-сервер)", server: "", common: true })
            root.multistreamServices = list
            root.syncPlatformCombo()
        })
    }

    // Selects the combo entry matching the currently saved server URL (so
    // reopening the page shows "Twitch" instead of "Другое" for someone who
    // already picked it before) — falls back to "Другое" for a custom/
    // unlisted server. Runs after either async load finishes, since which
    // one lands first isn't guaranteed.
    function syncPlatformCombo() {
        for (var i = 0; i < root.multistreamServices.length; i++) {
            if (root.multistreamServices[i].server !== "" && root.multistreamServices[i].server === multistreamServer.text) {
                multistreamPlatform.currentIndex = i
                return
            }
        }
        multistreamPlatform.currentIndex = 0
    }

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
            youtubeChannelId.text = data.youtube_channel_id || ""
            youtubeOAuthClientId.text = data.youtube_oauth_client_id || ""
            youtubeOAuthClientSecret.text = data.youtube_oauth_client_secret || ""
            youtubeToggle.checked = !!data.youtube_enabled

            streamTitle.text = data.stream_title || ""

            telegramToken.text = data.telegram_bot_token || ""
            telegramChatId.text = data.telegram_chat_id || ""
            telegramToggle.checked = !!data.telegram_enabled
            telegramControlToggle.checked = !!data.telegram_control_enabled

            multistreamLabel.text = data.multistream_label || ""
            multistreamServer.text = data.multistream_server || ""
            multistreamKey.text = data.multistream_key || ""
            multistreamToggle.checked = !!data.multistream_enabled
            root.syncPlatformCombo()

            ttsToggle.checked = !!data.tts_enabled
            loading = false
        })
    }

    function refreshMultistreamStatus() {
        api.get("/api/multistream/status", function (ok, data) {
            if (ok) root.multistreamStatus = data
        })
    }

    Component.onCompleted: { load(); refreshMultistreamStatus(); loadMultistreamServices(); loadStreamTemplates() }

    Timer {
        interval: 5000
        running: true
        repeat: true
        onTriggered: root.refreshMultistreamStatus()
    }

    function save() {
        if (loading) return
        api.post("/api/connections", {
            twitch_client_id: twitchClientId.text,
            twitch_channel: twitchChannel.text,
            twitch_chat_enabled: twitchChatToggle.checked,
            twitch_eventsub_enabled: twitchEventsToggle.checked,
            youtube_api_key: youtubeApiKey.text,
            youtube_video_id: youtubeVideoId.text,
            youtube_channel_id: youtubeChannelId.text,
            youtube_oauth_client_id: youtubeOAuthClientId.text,
            youtube_oauth_client_secret: youtubeOAuthClientSecret.text,
            youtube_enabled: youtubeToggle.checked,
            stream_title: streamTitle.text,
            telegram_bot_token: telegramToken.text,
            telegram_chat_id: telegramChatId.text,
            telegram_enabled: telegramToggle.checked,
            telegram_control_enabled: telegramControlToggle.checked,
            multistream_label: multistreamLabel.text,
            multistream_server: multistreamServer.text,
            multistream_key: multistreamKey.text,
            multistream_enabled: multistreamToggle.checked,
            tts_enabled: ttsToggle.checked
        }, function (ok) {
            statusText.text = ok
                ? "Сохранено. Twitch/YouTube/Telegram подключатся при следующем запуске."
                : "Не удалось сохранить."
            statusTimer.restart()
        }, false)
    }

    function installMultistreamPlugin() {
        installStatus = "Устанавливаю…"
        api.post("/api/multistream/install-plugin", {}, function (ok, data) {
            installOk = ok && data && data.ok
            installStatus = installOk ? "Плагин установлен. Запусти OBS, чтобы он подхватился." : ((data && data.error) ? data.error : "Не удалось установить")
            root.refreshMultistreamStatus()
        }, false)
    }

    function fetchMultistreamKey() {
        var chosen = root.multistreamServices[multistreamPlatform.currentIndex]
        if (!chosen || !chosen.name) return
        api.post("/api/multistream/fetch-key", { service: chosen.name }, function (ok, data) {
            if (ok && data && data.found) {
                multistreamKey.text = data.key
                root.save()
                keyFetchStatus.text = "Ключ скопирован из настроек OBS."
            } else if (chosen.key_link) {
                Qt.openUrlExternally(chosen.key_link)
                keyFetchStatus.text = "В OBS такой ключ не настроен — открыл страницу, где его можно взять."
            } else {
                keyFetchStatus.text = "Ключ не найден ни в OBS, ни в каталоге площадок — посмотри в настройках трансляции на самой площадке."
            }
            keyFetchStatusTimer.restart()
        }, false)
    }

    function syncMultistream() {
        root.save()
        syncStatusText.text = "Применяю…"
        syncStatusTimer.restart()
        api.post("/api/multistream/sync", {}, function (ok, data) {
            syncOk = ok && data && data.ok
            syncStatusText.text = syncOk ? "Готово — открой OBS и жми «Начать трансляцию» как обычно." : ((data && data.error) ? data.error : "Не удалось применить")
            syncStatusTimer.restart()
            root.refreshMultistreamStatus()
        }, false)
    }

    SectionHeader {
        Layout.fillWidth: true
        title: "Подключения"
        subtitle: "Ключи и переключатели для каждого источника. Изменения по Twitch/YouTube/Telegram применяются после перезапуска программы — TTS переключается сразу."
    }

    CollapsibleCard {
        Layout.fillWidth: true
        settingsKey: "streamTitle"
        title: "Название трансляции"
        subtitle: "Шаблон — один клик и сразу меняются название и (если указана) категория на Twitch, плюс название на YouTube (если подключена OAuth-авторизация ниже на странице). Без OAuth YouTube придётся менять вручную в YouTube Studio."

        Text { text: "Быстро применить шаблон"; color: Theme.textDim; font.pixelSize: Theme.fontMd; font.bold: true; visible: root.streamTemplates.length > 0 }
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            visible: root.streamTemplates.length > 0
            GlassComboBox {
                id: templatePicker
                Layout.fillWidth: true
                model: root.streamTemplates
                textRole: "name"
            }
            PillButton {
                text: "Применить"
                onClicked: root.applyStreamTemplate()
            }
        }
        Text {
            id: templateApplyStatus
            color: Theme.good
            font.pixelSize: Theme.fontSm
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            opacity: 0
            Behavior on opacity { NumberAnimation { duration: 200 } }
        }
        Timer {
            id: templateApplyStatusTimer
            interval: 5000
            onTriggered: templateApplyStatus.opacity = 0
            onRunningChanged: if (running) templateApplyStatus.opacity = 1
        }

        Text { text: "Текущее название (ручной ввод)"; color: Theme.textDim; font.pixelSize: Theme.fontMd; font.bold: true }
        GlassTextField {
            id: streamTitle
            Layout.fillWidth: true
            placeholderText: "например, Играем в CS2 с чатом!"
            onEditingFinished: root.save()
        }

        SectionHeader {
            Layout.fillWidth: true
            Layout.topMargin: 8
            title: "Шаблоны"
            subtitle: "Категория Twitch — опционально, вводи ровно как называется на Twitch (например «Counter-Strike», не «CS2»)."
        }

        Repeater {
            model: root.streamTemplates
            delegate: RowLayout {
                required property var modelData
                Layout.fillWidth: true
                spacing: 8
                Text {
                    text: modelData.name + " — " + modelData.title + (modelData.twitch_game ? (" · " + modelData.twitch_game) : "")
                    color: Theme.textDim
                    font.pixelSize: Theme.fontSm
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                PillButton {
                    danger: true
                    text: "✕"
                    implicitWidth: 32
                    implicitHeight: 28
                    onClicked: root.deleteStreamTemplate(modelData.id)
                }
            }
        }

        Text { text: "Название шаблона"; color: Theme.textDim; font.pixelSize: Theme.fontMd; font.bold: true }
        GlassTextField {
            id: newTemplateName
            Layout.fillWidth: true
            placeholderText: "например, Играем в CS2"
        }
        Text { text: "Текст названия трансляции"; color: Theme.textDim; font.pixelSize: Theme.fontMd; font.bold: true }
        GlassTextField {
            id: newTemplateTitle
            Layout.fillWidth: true
            placeholderText: "например, Играем в CS2 с чатом!"
        }
        Text { text: "Категория на Twitch (необязательно)"; color: Theme.textDim; font.pixelSize: Theme.fontMd; font.bold: true }
        GlassTextField {
            id: newTemplateGame
            Layout.fillWidth: true
            placeholderText: "например, Counter-Strike"
        }
        RowLayout {
            Layout.fillWidth: true
            PillButton {
                text: "Сохранить шаблон"
                onClicked: root.saveStreamTemplate()
            }
            Item { Layout.fillWidth: true }
        }
    }

    CollapsibleCard {
        Layout.fillWidth: true
        settingsKey: "twitch"
        title: "Twitch"
        subtitle: "Client ID берётся из своего приложения на dev.twitch.tv — Twitch не даёт общий ключ на всех."

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

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 4
            spacing: 10
            PillButton {
                text: "Переавторизовать Twitch"
                onClicked: {
                    reauthStatus.text = "Открываю окно авторизации…"
                    reauthStatus.opacity = 1
                    api.post("/api/twitch/reauth", { client_id: twitchClientId.text }, function (ok) {
                        if (!ok) { reauthStatus.text = "Не получилось начать авторизацию."; reauthStatusTimer.restart() }
                    }, false)
                }
            }
            Text {
                text: "Если чат/алерты не подключаются сами — сбросит сохранённый вход и попросит войти в Twitch заново."
                color: Theme.textFaint
                font.pixelSize: Theme.fontSm
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }
        Text {
            id: reauthStatus
            color: Theme.textDim
            font.pixelSize: Theme.fontSm
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            opacity: 0
            Behavior on opacity { NumberAnimation { duration: 200 } }
        }
        Timer { id: reauthStatusTimer; interval: 4000; onTriggered: reauthStatus.opacity = 0 }
    }

    CollapsibleCard {
        Layout.fillWidth: true
        settingsKey: "youtube"
        title: "YouTube"
        subtitle: "API-ключ создаётся в Google Cloud Console (включи YouTube Data API v3)."

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            LinkChip { text: "Получить API-ключ →"; url: "https://console.cloud.google.com/apis/credentials" }
            LinkChip { text: "Узнать ID канала →"; url: "https://www.youtube.com/account_advanced" }
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
            Text { text: "4. <b>ID канала</b> — открой страницу «Узнать ID канала» (кнопка выше), там строка вида UCxxxxxxxxxxxxxxxxxxxxxxxx — вставь её в поле ниже. Заполняется один раз, дальше активная трансляция находится сама, менять перед каждым стримом не нужно."; color: Theme.textDim; font.pixelSize: Theme.fontSm; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        }

        Text { text: "API-ключ"; color: Theme.textDim; font.pixelSize: Theme.fontMd; font.bold: true }
        GlassTextField {
            id: youtubeApiKey
            Layout.fillWidth: true
            placeholderText: "AIza..."
            onEditingFinished: root.save()
        }

        Text { text: "ID канала"; color: Theme.textDim; font.pixelSize: Theme.fontMd; font.bold: true }
        GlassTextField {
            id: youtubeChannelId
            Layout.fillWidth: true
            placeholderText: "UCxxxxxxxxxxxxxxxxxxxxxxxx"
            onEditingFinished: root.save()
        }
        Text {
            text: "Заполни один раз — активная трансляция канала находится автоматически, ID видео вручную менять больше не нужно."
            color: Theme.textFaint
            font.pixelSize: Theme.fontSm
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        Text { text: "ID видеотрансляции (необязательно)"; color: Theme.textDim; font.pixelSize: Theme.fontMd; font.bold: true }
        GlassTextField {
            id: youtubeVideoId
            Layout.fillWidth: true
            placeholderText: "оставь пустым — определится само по ID канала выше"
            onEditingFinished: root.save()
        }
        Text {
            text: "Заполняй только если авто-определение не подходит (например, нужна конкретная трансляция, а не текущая активная) — тогда используется именно это значение, а не поиск по каналу."
            color: Theme.textFaint
            font.pixelSize: Theme.fontSm
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        GlassToggle { id: youtubeToggle; text: "Читать чат YouTube"; onToggled: root.save() }

        SectionHeader {
            Layout.fillWidth: true
            Layout.topMargin: 8
            title: "Смена названия трансляции"
            subtitle: "Отдельная авторизация — API-ключ выше только читает чат, менять название/шаблоны на YouTube может лишь настоящий OAuth-вход. Одноразовая настройка через Google Cloud Console."
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            LinkChip { text: "Открыть Google Cloud Console →"; url: "https://console.cloud.google.com/apis/credentials" }
            Item { Layout.fillWidth: true }
            PillButton {
                text: youtubeOAuthGuide.visible ? "Скрыть инструкцию" : "Как настроить?"
                implicitHeight: 30
                onClicked: youtubeOAuthGuide.visible = !youtubeOAuthGuide.visible
            }
        }

        ColumnLayout {
            id: youtubeOAuthGuide
            Layout.fillWidth: true
            visible: false
            spacing: 6

            Text { text: "1. В том же проекте, где брал API-ключ — <b>APIs & Services → OAuth consent screen</b>. Тип — External, добавь себя в тестовые пользователи (Test users)."; color: Theme.textDim; font.pixelSize: Theme.fontSm; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Text { text: "2. <b>Credentials → Create Credentials → OAuth client ID</b>. Тип приложения — <b>TVs and Limited Input devices</b>."; color: Theme.textDim; font.pixelSize: Theme.fontSm; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Text { text: "3. Скопируй <b>Client ID</b> и <b>Client Secret</b> в поля ниже."; color: Theme.textDim; font.pixelSize: Theme.fontSm; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Text { text: "4. Пока проект в статусе Testing — вход придётся повторять раз в неделю (Google так ограничивает неопубликованные приложения). Если это мешает — в OAuth consent screen есть кнопка Publish App."; color: Theme.textDim; font.pixelSize: Theme.fontSm; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        }

        Text { text: "OAuth Client ID"; color: Theme.textDim; font.pixelSize: Theme.fontMd; font.bold: true }
        GlassTextField {
            id: youtubeOAuthClientId
            Layout.fillWidth: true
            placeholderText: "....apps.googleusercontent.com"
            onEditingFinished: root.save()
        }
        Text { text: "OAuth Client Secret"; color: Theme.textDim; font.pixelSize: Theme.fontMd; font.bold: true }
        GlassTextField {
            id: youtubeOAuthClientSecret
            Layout.fillWidth: true
            echoMode: TextInput.Password
            onEditingFinished: root.save()
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            PillButton {
                text: "Подключить YouTube"
                enabled: youtubeOAuthClientId.text.length > 0 && youtubeOAuthClientSecret.text.length > 0
                onClicked: root.startYoutubeAuth()
            }
            PillButton {
                text: "Войти заново"
                enabled: youtubeOAuthClientId.text.length > 0 && youtubeOAuthClientSecret.text.length > 0
                onClicked: root.startYoutubeAuth(true)
            }
            Item { Layout.fillWidth: true }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            visible: root.youtubeAuthPending
            Text {
                text: "Код: " + root.youtubeAuthCode
                color: "#ffd27a"
                font.pixelSize: Theme.fontLg
                font.weight: Font.Bold
            }
            PillButton {
                text: "Открыть страницу входа"
                implicitHeight: 30
                onClicked: Qt.openUrlExternally(root.youtubeAuthUrl)
            }
        }
        Text {
            visible: root.youtubeAuthStatusText.length > 0
            text: root.youtubeAuthStatusText
            color: root.youtubeAuthOk ? Theme.good : Theme.bad
            font.pixelSize: Theme.fontSm
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    }

    CollapsibleCard {
        Layout.fillWidth: true
        settingsKey: "telegram"
        title: "Telegram"
        subtitle: "Токен бота — у @BotFather, chat_id — у @userinfobot (или своего чата с ботом)."

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

    CollapsibleCard {
        Layout.fillWidth: true
        settingsKey: "multistream"
        title: "Мультистрим (2 платформы)"
        subtitle: "Основная площадка — как обычно, в настройках OBS (Settings → Stream). Здесь настраивается ровно одна дополнительная площадка через бесплатный плагин obs-multi-rtmp — выбери площадку из списка (адрес сервера подставится сам, как в самом OBS) и вставь только ключ трансляции из её настроек. Жмёшь «Начать трансляцию» в OBS один раз — идёт сразу на обе."

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            visible: !root.multistreamStatus.plugin_installed
            PillButton {
                text: "Установить плагин в OBS"
                onClicked: root.installMultistreamPlugin()
            }
            Item { Layout.fillWidth: true }
        }
        Text {
            visible: root.multistreamStatus.plugin_installed
            text: "Плагин obs-multi-rtmp установлен."
            color: Theme.good
            font.pixelSize: Theme.fontSm
        }
        Text {
            visible: installStatus.length > 0
            text: installStatus
            color: installOk ? Theme.good : Theme.bad
            font.pixelSize: Theme.fontSm
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        Text { text: "Название площадки"; color: Theme.textDim; font.pixelSize: Theme.fontMd; font.bold: true }
        GlassTextField {
            id: multistreamLabel
            Layout.fillWidth: true
            placeholderText: "например, ВКонтакте"
            onEditingFinished: root.save()
        }

        Text { text: "Платформа"; color: Theme.textDim; font.pixelSize: Theme.fontMd; font.bold: true }
        GlassComboBox {
            id: multistreamPlatform
            Layout.fillWidth: true
            model: root.multistreamServices
            textRole: "name"
            onActivated: {
                var chosen = root.multistreamServices[currentIndex]
                if (chosen && chosen.server) multistreamServer.text = chosen.server
                root.save()
            }
        }

        Text { text: "RTMP-сервер"; color: Theme.textDim; font.pixelSize: Theme.fontMd; font.bold: true }
        GlassTextField {
            id: multistreamServer
            Layout.fillWidth: true
            placeholderText: "rtmp://..."
            onEditingFinished: root.save()
        }

        Text { text: "Ключ трансляции"; color: Theme.textDim; font.pixelSize: Theme.fontMd; font.bold: true }
        GlassTextField {
            id: multistreamKey
            Layout.fillWidth: true
            echoMode: TextInput.Password
            placeholderText: "вставь сюда или получи кнопкой ниже"
            onEditingFinished: root.save()
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            PillButton {
                text: "Получить ключ трансляции"
                enabled: multistreamPlatform.currentIndex !== 0
                onClicked: root.fetchMultistreamKey()
            }
            Item { Layout.fillWidth: true }
        }
        Text {
            id: keyFetchStatus
            color: Theme.textDim
            font.pixelSize: Theme.fontSm
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            opacity: 0
            Behavior on opacity { NumberAnimation { duration: 200 } }
        }
        Timer {
            id: keyFetchStatusTimer
            interval: 5000
            onTriggered: keyFetchStatus.opacity = 0
            onRunningChanged: if (running) keyFetchStatus.opacity = 1
        }

        GlassToggle { id: multistreamToggle; text: "Включить вторую площадку"; onToggled: root.save() }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 4
            spacing: 10
            PillButton {
                text: "Применить в OBS"
                enabled: root.multistreamStatus.plugin_installed
                onClicked: root.syncMultistream()
            }
            Item { Layout.fillWidth: true }
        }
        // One persistent line reflecting the actual current state — OBS
        // being open takes priority since it's the blocking condition, so
        // it never shows alongside "applied" at the same time (that read as
        // a straight contradiction: both could be independently true, since
        // "already synced from an earlier run" and "OBS is open right now"
        // aren't mutually exclusive facts, just confusing shown together).
        Text {
            visible: true
            text: root.multistreamStatus.obs_running
                ? "OBS сейчас открыт — закрой его, чтобы применить настройки (иначе OBS перезапишет файл своими же при выходе)."
                : (root.multistreamStatus.synced
                    ? "Применено и синхронизировано с OBS (профиль «" + root.multistreamStatus.profile_name + "»)."
                    : "Ещё не применено — жми «Применить в OBS».")
            color: root.multistreamStatus.obs_running ? Theme.bad : (root.multistreamStatus.synced ? Theme.good : Theme.textFaint)
            font.pixelSize: Theme.fontSm
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        Text {
            id: syncStatusText
            color: syncOk ? Theme.good : Theme.bad
            font.pixelSize: Theme.fontSm
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            opacity: 0
            Behavior on opacity { NumberAnimation { duration: 200 } }
        }
        Timer {
            id: syncStatusTimer
            interval: 5000
            onTriggered: syncStatusText.opacity = 0
            onRunningChanged: if (running) syncStatusText.opacity = 1
        }
    }

    CollapsibleCard {
        Layout.fillWidth: true
        settingsKey: "tts"
        title: "Озвучка"
        subtitle: "Голоса и скорость настраиваются на странице «Озвучка» — здесь только общий выключатель."
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
