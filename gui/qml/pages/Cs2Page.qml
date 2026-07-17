import QtQuick
import QtQuick.Layouts
import StreamSoftGui

ColumnLayout {
    id: root
    spacing: 18

    property var snapshot: ({ active: false })
    property string installStatus: ""
    property bool installOk: false

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
        subtitle: "Live-счёт и K/D во время матча показываются прямо в карточке игрового баннера на оверлее — через официальную Game State Integration самой игры. Ставки зрителей на баллы — в разделе «Баллы»."
    }

    CollapsibleCard {
        Layout.fillWidth: true
        settingsKey: "cs2_install"
        title: "Установка в CS2"
        subtitle: "Разовая настройка: кладём небольшой .cfg-файл в папку CS2 — дальше игра сама присылает начало/конец матча и счёт, ничего запускать не нужно."

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

    Item { Layout.fillHeight: true }
}
