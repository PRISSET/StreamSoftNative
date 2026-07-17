import QtQuick
import QtQuick.Layouts
import StreamSoftGui

// Everything that earns or spends the same viewer point balance used to be
// split across two unrelated-looking nav entries — "Музыка" (earn rate +
// song requests) and a "Ставки зрителей" card buried inside the CS2 sub-tab
// of the game banner page. Both spend/earn the same points.json balance
// (see points.hpp), so they're one page now.
ColumnLayout {
    id: root
    spacing: 18

    property bool loading: false
    property var queueStatus: ({ current: null, queue: [] })
    property var leaderboard: []

    function applySettings(settings) {
        loading = true
        pointsSlider.value = settings.points_per_message !== undefined ? settings.points_per_message : 1

        songEnabledToggle.checked = !!settings.song_requests_enabled
        costSlider.value = settings.song_request_cost !== undefined ? settings.song_request_cost : 50
        volumeSlider.value = settings.song_request_volume !== undefined ? settings.song_request_volume : 80

        betsToggle.checked = !!settings.bets_enabled
        minSlider.value = settings.bet_min !== undefined ? settings.bet_min : 10
        maxSlider.value = settings.bet_max !== undefined ? settings.bet_max : 500
        multiplierSlider.value = settings.bet_payout_multiplier !== undefined ? settings.bet_payout_multiplier * 10 : 20
        lockRoundSlider.value = settings.bet_lock_round !== undefined ? settings.bet_lock_round : 3
        loading = false
    }

    function save() {
        if (loading) return
        api.post("/api/settings", {
            points_per_message: Math.round(pointsSlider.value),
            song_requests_enabled: songEnabledToggle.checked,
            song_request_cost: Math.round(costSlider.value),
            song_request_volume: Math.round(volumeSlider.value),
            bets_enabled: betsToggle.checked,
            bet_min: Math.round(minSlider.value),
            bet_max: Math.round(maxSlider.value),
            bet_payout_multiplier: multiplierSlider.value / 10,
            bet_lock_round: Math.round(lockRoundSlider.value)
        }, function () {})
    }

    function refreshQueue() {
        api.get("/api/songqueue/status", function (ok, data) {
            if (ok) root.queueStatus = data
        })
    }

    function refreshLeaderboard() {
        api.get("/api/points", function (ok, data) {
            if (ok) root.leaderboard = data.leaderboard || []
        })
    }

    function skip() {
        api.post("/api/songqueue/skip", {}, function () { root.refreshQueue() })
    }

    function clearQueue() {
        api.post("/api/songqueue/clear", {}, function () { root.refreshQueue() })
    }

    Component.onCompleted: {
        refreshQueue()
        refreshLeaderboard()
    }

    Timer {
        interval: 3000
        running: true
        repeat: true
        onTriggered: { root.refreshQueue(); root.refreshLeaderboard() }
    }

    SectionHeader {
        Layout.fillWidth: true
        title: "Баллы"
        subtitle: "Зрители копят баллы за сообщения в чате и тратят их на музыку (\"!song <ссылка>\") или на ставки во время матчей CS2 (\"!bet win/lose <баллы>\")."
    }

    GlassCard {
        Layout.fillWidth: true

        SectionHeader {
            Layout.fillWidth: true
            title: "Начисление"
        }

        RowLayout {
            Layout.fillWidth: true
            Text { text: "Баллов за 1 сообщение в чате"; color: Theme.textDim; font.pixelSize: Theme.fontMd; font.bold: true; Layout.fillWidth: true }
            Text { text: Math.round(pointsSlider.value) + (Math.round(pointsSlider.value) === 1 ? " балл" : " баллов"); color: Theme.text; font.pixelSize: Theme.fontMd; font.bold: true }
        }
        GlassSlider {
            id: pointsSlider
            Layout.fillWidth: true
            from: 0; to: 20; stepSize: 1
            onMoved: root.save()
        }
    }

    GlassCard {
        Layout.fillWidth: true

        SectionHeader {
            Layout.fillWidth: true
            title: "Музыка"
            subtitle: "Реквесты треков за баллы."
        }

        GlassToggle {
            id: songEnabledToggle
            text: "Включить реквесты музыки за баллы"
            onToggled: root.save()
        }

        RowLayout {
            Layout.fillWidth: true
            Text { text: "Стоимость реквеста"; color: Theme.textDim; font.pixelSize: Theme.fontMd; font.bold: true; Layout.fillWidth: true }
            Text { text: Math.round(costSlider.value) + " баллов"; color: Theme.text; font.pixelSize: Theme.fontMd; font.bold: true }
        }
        GlassSlider {
            id: costSlider
            Layout.fillWidth: true
            from: 0; to: 500; stepSize: 10
            onMoved: root.save()
        }

        RowLayout {
            Layout.fillWidth: true
            Text { text: "Громкость плеера"; color: Theme.textDim; font.pixelSize: Theme.fontMd; font.bold: true; Layout.fillWidth: true }
            Text { text: Math.round(volumeSlider.value) + "%"; color: Theme.text; font.pixelSize: Theme.fontMd; font.bold: true }
        }
        GlassSlider {
            id: volumeSlider
            Layout.fillWidth: true
            from: 0; to: 100; stepSize: 5
            onMoved: root.save()
        }
    }

    GlassCard {
        Layout.fillWidth: true

        SectionHeader {
            Layout.fillWidth: true
            title: "Сейчас играет"
            subtitle: root.queueStatus.current
                ? (root.queueStatus.current.platform === "youtube" ? "YouTube" : "SoundCloud") + " — заказал " + root.queueStatus.current.requester
                : "Ничего не играет"
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            PillButton {
                text: "Пропустить"
                enabled: !!root.queueStatus.current
                onClicked: root.skip()
            }
            PillButton {
                danger: true
                text: "Очистить очередь"
                onClicked: root.clearQueue()
            }
            Item { Layout.fillWidth: true }
            Text {
                text: "В очереди: " + (root.queueStatus.queue ? root.queueStatus.queue.length : 0)
                color: Theme.textFaint
                font.pixelSize: Theme.fontMd
            }
        }

        Repeater {
            model: root.queueStatus.queue || []
            delegate: RowLayout {
                required property var modelData
                required property int index
                Layout.fillWidth: true
                Text {
                    text: (index + 1) + ". " + (modelData.platform === "youtube" ? "YouTube" : "SoundCloud") + " — " + modelData.requester
                    color: Theme.textDim
                    font.pixelSize: Theme.fontMd
                    Layout.fillWidth: true
                }
            }
        }
    }

    GlassCard {
        Layout.fillWidth: true

        SectionHeader {
            Layout.fillWidth: true
            title: "Ставки зрителей (CS2)"
            subtitle: "С варм-апа и до начала выбранного раунда — зрители пишут !bet win <баллы> или !bet lose <баллы>. После матча бот сверяется с Faceit: если это был подтверждённый матч — раздаёт баллы угадавшим, если нет (обычная игра, отменённая катка) — возвращает ставки всем. Live-статус матча и установка GSI-конфига — в «Игровой баннер» → CS2."
        }

        GlassToggle {
            id: betsToggle
            text: "Включить ставки"
            onToggled: root.save()
        }

        RowLayout {
            Layout.fillWidth: true
            Text { text: "Ставки принимаются до раунда"; color: Theme.textDim; font.pixelSize: Theme.fontMd; font.bold: true; Layout.fillWidth: true }
            Text { text: Math.round(lockRoundSlider.value) + ""; color: Theme.text; font.pixelSize: Theme.fontMd; font.bold: true }
        }
        GlassSlider {
            id: lockRoundSlider
            Layout.fillWidth: true
            from: 1; to: 8; stepSize: 1
            onMoved: root.save()
        }

        RowLayout {
            Layout.fillWidth: true
            Text { text: "Минимальная ставка"; color: Theme.textDim; font.pixelSize: Theme.fontMd; font.bold: true; Layout.fillWidth: true }
            Text { text: Math.round(minSlider.value) + " баллов"; color: Theme.text; font.pixelSize: Theme.fontMd; font.bold: true }
        }
        GlassSlider {
            id: minSlider
            Layout.fillWidth: true
            from: 1; to: 500; stepSize: 1
            onMoved: root.save()
        }

        RowLayout {
            Layout.fillWidth: true
            Text { text: "Максимальная ставка"; color: Theme.textDim; font.pixelSize: Theme.fontMd; font.bold: true; Layout.fillWidth: true }
            Text { text: Math.round(maxSlider.value) + " баллов"; color: Theme.text; font.pixelSize: Theme.fontMd; font.bold: true }
        }
        GlassSlider {
            id: maxSlider
            Layout.fillWidth: true
            from: 10; to: 5000; stepSize: 10
            onMoved: root.save()
        }

        RowLayout {
            Layout.fillWidth: true
            Text { text: "Выплата за верную ставку"; color: Theme.textDim; font.pixelSize: Theme.fontMd; font.bold: true; Layout.fillWidth: true }
            Text { text: "x" + (multiplierSlider.value / 10).toFixed(1); color: Theme.text; font.pixelSize: Theme.fontMd; font.bold: true }
        }
        GlassSlider {
            id: multiplierSlider
            Layout.fillWidth: true
            from: 10; to: 30; stepSize: 1
            onMoved: root.save()
        }
    }

    GlassCard {
        Layout.fillWidth: true

        SectionHeader {
            Layout.fillWidth: true
            title: "Топ по баллам"
        }

        Text {
            visible: root.leaderboard.length === 0
            text: "Пока никто ничего не заработал"
            color: Theme.textFaint
            font.pixelSize: Theme.fontMd
        }

        Repeater {
            model: root.leaderboard
            delegate: RowLayout {
                required property var modelData
                required property int index
                Layout.fillWidth: true
                Text { text: (index + 1) + ". " + modelData.username; color: Theme.text; font.pixelSize: Theme.fontMd; Layout.fillWidth: true }
                Text { text: modelData.points + " баллов"; color: Theme.textDim; font.pixelSize: Theme.fontMd }
            }
        }
    }

    Item { Layout.fillHeight: true }
}
