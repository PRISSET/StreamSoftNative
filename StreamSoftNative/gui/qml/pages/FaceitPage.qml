import QtQuick
import QtQuick.Layouts
import StreamSoftGui

ColumnLayout {
    id: root
    spacing: 18

    property bool loading: false
    property var snapshot: ({ valid: false })

    property bool hasSocialTelegram: false

    readonly property var bannerThemes: [
        { key: "milk", label: "Milk", bg: "#ffffff", border: "#c9ced6", text: "#1a2a3e" },
        { key: "faceitstyle", label: "Faceit", bg: "#1f1f22", border: "#ff0000", text: "#ffffff" },
        { key: "whitecyber", label: "White Cyber", bg: "#f4f6fa", border: "#0b1428", text: "#0a0a0a" },
        { key: "darkside", label: "DarkSide", bg: "#0a0812", border: "#b14bff", text: "#f5f0ff", accent2: "#22d3ee" }
    ]
    property string bannerTheme: "milk"

    function setBannerTheme(key) {
        if (root.bannerTheme === key) return
        root.bannerTheme = key
        api.post("/api/settings", { banner_theme: key }, function (ok) {
            statusText.text = ok ? "Цвет виджета применён." : "Не удалось сохранить цвет."
            statusTimer.restart()
        })
    }

    function load() {
        loading = true
        api.get("/api/connections", function (ok, data) {
            if (!ok) { loading = false; return }
            nicknameField.text = data.faceit_nickname || ""
            apiKeyField.text = data.faceit_api_key || ""
            ownKeyToggle.checked = !!(data.faceit_api_key && data.faceit_api_key.length > 0)
            enabledToggle.checked = !!data.faceit_enabled
            statsToggle.checked = !!data.faceit_stats_telegram_enabled
            hasSocialTelegram = !!(data.telegram_bot_token && data.telegram_bot_token.length > 0 &&
                                    data.social_telegram_channel_id && data.social_telegram_channel_id.length > 0)
            loading = false
        })
        api.get("/api/settings", function (ok, data) {
            if (ok && data.banner_theme) root.bannerTheme = data.banner_theme
        })
    }

    function save() {
        if (loading) return
        api.post("/api/connections", {
            faceit_nickname: nicknameField.text,
            faceit_api_key: ownKeyToggle.checked ? apiKeyField.text : "",
            faceit_enabled: enabledToggle.checked,
            faceit_stats_telegram_enabled: statsToggle.checked
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

    CollapsibleCard {
        Layout.fillWidth: true
        settingsKey: "faceit_nickname"
        title: "Ник и виджет"

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

    CollapsibleCard {
        Layout.fillWidth: true
        settingsKey: "faceit_bannerTheme"
        title: "Вид виджета"
        subtitle: "Карточка ELO/матчей на оверлее в OBS — применяется сразу, без перезапуска источника."

        Text { text: "Цвет"; color: Theme.textDim; font.pixelSize: Theme.fontMd; font.bold: true }
        RowLayout {
            spacing: 10
            Repeater {
                model: root.bannerThemes
                delegate: Rectangle {
                    required property var modelData
                    width: 92
                    height: 56
                    radius: 12
                    color: modelData.bg
                    gradient: modelData.accent2 ? swatchGradient : null
                    border.width: root.bannerTheme === modelData.key ? 3 : 2
                    border.color: root.bannerTheme === modelData.key ? Theme.accent : modelData.border

                    Gradient {
                        id: swatchGradient
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.0; color: modelData.bg }
                        GradientStop { position: 1.0; color: modelData.accent2 || modelData.bg }
                    }

                    Behavior on border.width { NumberAnimation { duration: 150 } }

                    Text {
                        anchors.centerIn: parent
                        text: modelData.label
                        color: modelData.text
                        font.bold: true
                        font.pixelSize: Theme.fontSm
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.setBannerTheme(modelData.key)
                    }
                }
            }
            Item { Layout.fillWidth: true }
        }
        Text {
            text: "Подгони источник «StreamSoft Faceit» в OBS под низкий и широкий прямоугольник (например 580×170) — карточка сама по себе компактная, лишнее место остаётся прозрачным."
            color: Theme.textFaint
            font.pixelSize: Theme.fontSm
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    }

    CollapsibleCard {
        Layout.fillWidth: true
        settingsKey: "faceit_apiKey"
        title: "Свой API-ключ"

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

    CollapsibleCard {
        Layout.fillWidth: true
        settingsKey: "faceit_telegramStats"
        title: "Статистика в Telegram"
        subtitle: "После каждого стрима — сколько ELO поднял/слил за стрим, за день, за месяц, за год. Раз в месяц — вся история матчей за прошедший месяц."

        GlassToggle {
            id: statsToggle
            text: "Публиковать статистику Faceit"
            onToggled: root.save()
        }

        Text {
            visible: !root.hasSocialTelegram
            text: "Нужен настроенный Telegram-бот и канал — заполни их на странице «Соцсети» (тот же канал, куда публикуется старт стрима)."
            color: Theme.bad
            font.pixelSize: Theme.fontSm
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    }

    CollapsibleCard {
        Layout.fillWidth: true
        visible: root.snapshot.valid
        settingsKey: "faceit_livePreview"
        title: root.snapshot.nickname || ""
        subtitle: "ELO " + (root.snapshot.elo || "—") + " · уровень " + (root.snapshot.skill_level || "?")

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
