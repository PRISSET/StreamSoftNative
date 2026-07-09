import QtQuick
import QtQuick.Layouts
import StreamSoftGui

ColumnLayout {
    id: root
    spacing: 18

    property bool loading: false

    function applySettings(settings) {
        loading = true
        themeCombo.currentIndex = themeCombo.indexOfValue(settings.theme || "minimal")
        chatScaleSlider.value = Math.round((settings.chat_scale !== undefined ? settings.chat_scale : 1) * 100)
        alertScaleSlider.value = Math.round((settings.alert_scale !== undefined ? settings.alert_scale : 1) * 100)
        loading = false
    }

    function save() {
        if (loading) return
        api.post("/api/settings", {
            theme: themeCombo.currentValue,
            chat_scale: chatScaleSlider.value / 100.0,
            alert_scale: alertScaleSlider.value / 100.0
        }, function () {})
    }

    Connections {
        target: overlayEvents
        function onEventMessage(kind, user, detail) {
            alertStage.showAlert(kind, user)
        }
        function onChatMessage(platform, author, text) {
            alertStage.showChatMessage(platform, author, text)
        }
    }

    SectionHeader {
        Layout.fillWidth: true
        title: "Оверлей и превью"
        subtitle: "Оформление и масштаб чата/алертов для OBS Browser Source."
    }

    GlassCard {
        Layout.fillWidth: true

        Text { text: "Шаблон"; color: Theme.textDim; font.pixelSize: Theme.fontMd; font.bold: true }
        GlassComboBox {
            id: themeCombo
            Layout.fillWidth: true
            textRole: "text"
            valueRole: "value"
            model: [
                { text: "Minimal — прозрачный, без рамок", value: "minimal" },
                { text: "Card — светлые карточки", value: "card" },
                { text: "Neon — тёмный, неоновая обводка", value: "neon" }
            ]
            onActivated: root.save()
        }

        RowLayout {
            Layout.fillWidth: true
            Text {
                text: "Масштаб текста чата"
                color: Theme.textDim
                font.pixelSize: Theme.fontMd
                font.bold: true
                Layout.fillWidth: true
            }
            Text {
                text: Math.round(chatScaleSlider.value) + "%"
                color: Theme.text
                font.pixelSize: Theme.fontMd
                font.bold: true
            }
        }
        GlassSlider {
            id: chatScaleSlider
            Layout.fillWidth: true
            from: 50
            to: 200
            stepSize: 5
            onMoved: root.save()
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 8
            Text {
                text: "Масштаб алертов"
                color: Theme.textDim
                font.pixelSize: Theme.fontMd
                font.bold: true
                Layout.fillWidth: true
            }
            Text {
                text: Math.round(alertScaleSlider.value) + "%"
                color: Theme.text
                font.pixelSize: Theme.fontMd
                font.bold: true
            }
        }
        GlassSlider {
            id: alertScaleSlider
            Layout.fillWidth: true
            from: 50
            to: 200
            stepSize: 5
            onMoved: root.save()
        }

        Text {
            text: "Настраиваются отдельно — если в OBS гифка алерта или ник еле видны, подними именно масштаб алертов, не трогая текст чата."
            color: Theme.textFaint
            font.pixelSize: Theme.fontSm
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    }

    GlassCard {
        id: previewCard
        Layout.fillWidth: true
        SectionHeader {
            Layout.fillWidth: true
            title: "Живое превью"
            subtitle: "То же самое, что летит в OBS — прямо здесь, без браузера."
        }

        // Alert/chat stage — both alerts and chat test messages show up
        // here and auto-hide, rather than chat piling up in a separate
        // always-growing list underneath. Alert mirrors overlay/app.js's
        // addEventAlert() (gif + nickname); chat mirrors addChatBubble()
        // (author + text), just as a single transient bubble instead of a
        // scrolling feed — this is a quick preview, not the real /chat.
        Rectangle {
            id: alertStage
            Layout.fillWidth: true
            Layout.preferredHeight: 170
            radius: Theme.radiusMd
            color: "#2a000000"
            border.width: 1
            border.color: Theme.glassBorder
            clip: true

            property bool active: false
            property string mode: "" // "alert" | "chat"

            function showAlert(kind, user) {
                mode = "alert"
                alertGif.source = "http://127.0.0.1:8099/media/" + kind + ".gif"
                alertNick.text = user
                active = true
                hideTimer.restart()
            }

            function showChatMessage(platform, author, text) {
                mode = "chat"
                chatAuthor.text = (platform === "youtube" ? "🔴" : "💜") + " " + author + ":"
                chatText.text = text
                active = true
                hideTimer.restart()
            }

            Timer {
                id: hideTimer
                interval: alertStage.mode === "chat" ? 4000 : 5200
                onTriggered: alertStage.active = false
            }

            Text {
                anchors.centerIn: parent
                text: "Здесь появится алерт или тестовое сообщение чата — кнопки ниже"
                color: Theme.textFaint
                font.pixelSize: Theme.fontSm
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                width: parent.width - 40
                visible: !alertStage.active
            }

            ColumnLayout {
                anchors.centerIn: parent
                spacing: 8
                visible: alertStage.active && alertStage.mode === "alert"

                AnimatedImage {
                    id: alertGif
                    Layout.alignment: Qt.AlignHCenter
                    Layout.maximumWidth: 220
                    Layout.maximumHeight: 110
                    fillMode: Image.PreserveAspectFit
                    cache: false
                    playing: true
                    onStatusChanged: visible = (status === AnimatedImage.Ready)
                }
                Text {
                    id: alertNick
                    Layout.alignment: Qt.AlignHCenter
                    color: "#ffffff"
                    font.bold: true
                    font.pixelSize: Theme.fontLg
                }
            }

            ColumnLayout {
                anchors.centerIn: parent
                spacing: 6
                visible: alertStage.active && alertStage.mode === "chat"
                width: parent.width - 48

                Text {
                    id: chatAuthor
                    Layout.alignment: Qt.AlignHCenter
                    color: "#ffffff"
                    font.bold: true
                    font.pixelSize: Theme.fontLg
                }
                Text {
                    id: chatText
                    Layout.alignment: Qt.AlignHCenter
                    Layout.fillWidth: true
                    color: "#ffffff"
                    font.pixelSize: Theme.fontLg
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            LinkChip { text: "Открыть /chat в браузере"; url: "http://127.0.0.1:8099/chat" }
            LinkChip { text: "Открыть /events в браузере"; url: "http://127.0.0.1:8099/events" }
            Item { Layout.fillWidth: true }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            GlassComboBox {
                id: testKindCombo
                Layout.preferredWidth: 220
                textRole: "text"
                valueRole: "value"
                model: [
                    { text: "Фоллоу", value: "follow" },
                    { text: "Подписка", value: "subscribe" },
                    { text: "Подарочная подписка", value: "gift_sub" },
                    { text: "Рейд", value: "raid" },
                    { text: "Донат битсами", value: "cheer" }
                ]
            }
            PillButton {
                text: "Показать тестовый алерт"
                onClicked: api.post("/api/test-event", { kind: testKindCombo.currentValue }, function () {}, false)
            }
            PillButton {
                text: "Тестовое сообщение чата"
                onClicked: api.post("/api/test-chat", {}, function () {}, false)
            }
            Item { Layout.fillWidth: true }
        }
    }

    GlassCard {
        id: obsCard
        Layout.fillWidth: true

        property bool obsConnecting: false
        property bool obsConnected: false
        property string obsStatusText: ""
        property bool obsStatusIsError: false

        Component.onCompleted: {
            api.get("/api/connections", function (ok, data) {
                if (ok && data && data.obs_connected) obsCard.obsConnected = true
            })
        }

        SectionHeader {
            Layout.fillWidth: true
            title: "Подключение к OBS"
            subtitle: "Само добавит источники StreamSoft Chat / StreamSoft Alerts в текущую сцену. OBS должен быть закрыт на момент нажатия."
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            PillButton {
                text: obsCard.obsConnecting ? "Подключаюсь..."
                    : (obsCard.obsConnected ? "Источники уже добавлены — переподключить" : "Подключить к OBS")
                enabled: !obsCard.obsConnecting
                onClicked: {
                    obsCard.obsConnecting = true
                    obsCard.obsStatusText = ""
                    api.post("/api/obs/connect", {}, function (ok, data) {
                        // has its own inline status text below, no toast
                        obsCard.obsConnecting = false
                        if (ok && data && data.ok) {
                            obsCard.obsConnected = true
                            obsCard.obsStatusText = "Готово — источники добавлены в текущую сцену."
                            obsCard.obsStatusIsError = false
                        } else {
                            obsCard.obsStatusText = (data && data.error) ? data.error
                                : "Не удалось подключиться — попробуй закрыть OBS и повторить."
                            obsCard.obsStatusIsError = true
                        }
                    }, false)
                }
            }
            Item { Layout.fillWidth: true }
        }

        Text {
            visible: obsCard.obsStatusText.length === 0 && obsCard.obsConnected
            text: "Источники уже были добавлены в сцену раньше — нажимать снова нужно только если их удалили в OBS."
            color: Theme.textFaint
            font.pixelSize: Theme.fontSm
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        Text {
            visible: obsCard.obsStatusText.length > 0
            text: obsCard.obsStatusText
            color: obsCard.obsStatusIsError ? Theme.bad : Theme.good
            font.pixelSize: Theme.fontSm
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    }

    Item { Layout.fillHeight: true }
}
