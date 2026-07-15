import QtQuick
import QtQuick.Layouts
import StreamSoftGui

ColumnLayout {
    id: root
    spacing: 18

    property bool loading: false
    property string installStatus: ""
    property bool installOk: false
    property var snapshot: ({ active: false })

    function applySettings(settings) {
        loading = true
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
            bets_enabled: betsToggle.checked,
            bet_min: Math.round(minSlider.value),
            bet_max: Math.round(maxSlider.value),
            bet_payout_multiplier: multiplierSlider.value / 10,
            bet_lock_round: Math.round(lockRoundSlider.value)
        }, function () {})
    }

    function installCfg() {
        installStatus = "Устанавливаю…"
        api.post("/api/cs2/install-cfg", {}, function (ok, data) {
            installOk = ok && data && data.ok
            installStatus = installOk ? ("Готово: " + data.path) : ((data && data.error) ? data.error : "Не удалось установить")
        })
    }

    function refreshSnapshot() {
        api.get("/api/cs2/snapshot", function (ok, data) {
            if (ok) root.snapshot = data
        })
    }

    Component.onCompleted: refreshSnapshot()

    Timer {
        interval: 4000
        running: true
        repeat: true
        onTriggered: root.refreshSnapshot()
    }

    SectionHeader {
        Layout.fillWidth: true
        title: "CS2"
        subtitle: "Ставки зрителей на твой текущий матч — через официальную Game State Integration самой игры. Live-счёт и K/D во время матча показываются прямо в карточке Faceit на оверлее."
    }

    GlassCard {
        Layout.fillWidth: true

        SectionHeader {
            Layout.fillWidth: true
            title: "Установка в CS2"
            subtitle: "Разовая настройка: кладём небольшой .cfg-файл в папку CS2 — дальше игра сама присылает начало/конец матча и счёт, ничего запускать не нужно."
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            PillButton {
                text: "Установить конфиг в CS2"
                onClicked: root.installCfg()
            }
            Item { Layout.fillWidth: true }
        }

        Text {
            visible: installStatus.length > 0
            text: installStatus
            color: installOk ? Theme.good : Theme.bad
            font.pixelSize: Theme.fontSm
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        Text {
            text: root.snapshot.active
                ? ("Матч сейчас идёт: " + (root.snapshot.map || "—") + " · " + (root.snapshot.ctScore || 0) + ":" + (root.snapshot.tScore || 0))
                : "Сейчас матч не идёт (или CS2 не запущен / конфиг ещё не установлен)"
            color: root.snapshot.active ? Theme.good : Theme.textFaint
            font.pixelSize: Theme.fontSm
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    }

    GlassCard {
        Layout.fillWidth: true

        SectionHeader {
            Layout.fillWidth: true
            title: "Ставки зрителей"
            subtitle: "С варм-апа и до начала выбранного раунда — зрители пишут !bet win <баллы> или !bet lose <баллы>. После матча бот сверяется с Faceit: если это был подтверждённый матч Faceit — раздаёт баллы угадавшим, если нет (обычная игра, отменённая катка) — возвращает ставки всем."
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

    Item { Layout.fillHeight: true }
}
