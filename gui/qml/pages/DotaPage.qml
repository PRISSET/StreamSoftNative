import QtQuick
import QtQuick.Layouts
import StreamSoftGui

ColumnLayout {
    id: root
    spacing: 18

    property bool loading: false
    property var snapshot: ({ valid: false })
    property var liveSnapshot: ({ active: false })
    property string installStatus: ""
    property bool installOk: false

    function load() {
        loading = true
        api.get("/api/connections", function (ok, data) {
            if (!ok) { loading = false; return }
            accountIdField.text = data.dota_account_id || ""
            enabledToggle.checked = !!data.dota_enabled
            loading = false
        })
    }

    function save() {
        if (loading) return
        api.post("/api/connections", {
            dota_account_id: accountIdField.text,
            dota_enabled: enabledToggle.checked
        }, function (ok) {
            statusText.text = ok ? "Сохранено." : "Не удалось сохранить."
            statusTimer.restart()
        })
    }

    function installCfg() {
        installStatus = "Устанавливаю…"
        api.post("/api/dota/install-cfg", {}, function (ok, data) {
            installOk = ok && data && data.ok
            installStatus = installOk ? ("Готово: " + data.path) : ((data && data.error) ? data.error : "Не удалось установить")
        })
    }

    function refreshSnapshot() {
        api.get("/api/dota/snapshot", function (ok, data) {
            if (ok) root.snapshot = data
        })
        api.get("/api/dota/live", function (ok, data) {
            if (ok) root.liveSnapshot = data
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
        title: "Dota 2"
        subtitle: "Тот же баннер на оверлее, что и Faceit — переключается на Dota сам, как только запущена dota2.exe вместо cs2.exe."
    }

    CollapsibleCard {
        Layout.fillWidth: true
        settingsKey: "dota_accountId"
        title: "Account ID и виджет"

        Text { text: "Dota Account ID (Friend ID)"; color: Theme.textDim; font.pixelSize: Theme.fontMd; font.bold: true }
        GlassTextField {
            id: accountIdField
            Layout.fillWidth: true
            placeholderText: "105248644"
            onEditingFinished: root.save()
        }
        Text {
            text: "Опционально — число, а не Steam-ссылка и не ник (список друзей в Dota 2 или адрес профиля на opendota.com/players/<ID>). Нужно только для истории прошлых матчей; live-виджет ниже работает и без него."
            color: Theme.textFaint
            font.pixelSize: Theme.fontSm
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        GlassToggle { id: enabledToggle; text: "Показывать виджет на оверлее"; onToggled: root.save() }
    }

    CollapsibleCard {
        Layout.fillWidth: true
        settingsKey: "dota_liveMatch"
        title: "Live-матч (Game State Integration)"
        subtitle: "Разовая настройка: кладём .cfg-файл в папку Dota 2 — дальше игра сама присылает герой/KDA/счёт прямо во время матча, в обход настроек приватности профиля."

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            PillButton {
                text: "Установить конфиг в Dota 2"
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
            text: root.liveSnapshot.active
                ? ("Матч сейчас идёт: " + (root.liveSnapshot.heroName || "?") + " · " + root.liveSnapshot.kills + "/" + root.liveSnapshot.deaths + "/" + root.liveSnapshot.assists + " · " + (root.liveSnapshot.radiantScore || 0) + ":" + (root.liveSnapshot.direScore || 0))
                : "Сейчас матч не идёт (или Dota 2 не запущена / конфиг ещё не установлен)"
            color: root.liveSnapshot.active ? Theme.good : Theme.textFaint
            font.pixelSize: Theme.fontSm
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    }

    CollapsibleCard {
        Layout.fillWidth: true
        visible: root.snapshot.valid
        settingsKey: "dota_livePreview"
        title: root.snapshot.personaname || ""
        subtitle: root.snapshot.rank_label ? ("Ранг: " + root.snapshot.rank_label) : "Ранг скрыт настройками приватности профиля"

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
