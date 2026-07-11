import QtQuick
import QtQuick.Layouts
import StreamSoftGui

ColumnLayout {
    id: root
    spacing: 18

    property bool loading: false
    property var queueStatus: ({ current: null, queue: [] })
    property var leaderboard: []

    function applySettings(settings) {
        loading = true
        enabledToggle.checked = !!settings.song_requests_enabled
        costSlider.value = settings.song_request_cost !== undefined ? settings.song_request_cost : 50
        volumeSlider.value = settings.song_request_volume !== undefined ? settings.song_request_volume : 80
        loading = false
    }

    function save() {
        if (loading) return
        api.post("/api/settings", {
            song_requests_enabled: enabledToggle.checked,
            song_request_cost: Math.round(costSlider.value),
            song_request_volume: Math.round(volumeSlider.value)
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
        title: "Музыка"
        subtitle: "Зрители копят баллы за сообщения в чате и тратят их на \"!song <ссылка YouTube/SoundCloud>\"."
    }

    GlassCard {
        Layout.fillWidth: true

        GlassToggle {
            id: enabledToggle
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
