import QtQuick
import QtQuick.Layouts
import StreamSoftGui

ColumnLayout {
    id: root
    spacing: 18

    property bool loading: false
    property var snapshot: ({ valid: false })

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
            statusText.text = ok ? "Сохранено, обновление придёт в течение пары минут." : "Не удалось сохранить."
            statusTimer.restart()
        })
    }

    function refreshSnapshot() {
        api.get("/api/dota/snapshot", function (ok, data) {
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
        title: "Dota 2"
        subtitle: "Тот же баннер на оверлее, что и Faceit — переключается на Dota сам, как только запущена dota2.exe вместо cs2.exe. Данные — из бесплатного OpenDota API."
    }

    GlassCard {
        Layout.fillWidth: true

        Text { text: "Dota Account ID (Friend ID)"; color: Theme.textDim; font.pixelSize: Theme.fontMd; font.bold: true }
        GlassTextField {
            id: accountIdField
            Layout.fillWidth: true
            placeholderText: "105248644"
            onEditingFinished: root.save()
        }
        Text {
            text: "Число, а не Steam-ссылка и не ник — тот же ID, что виден в списке друзей в Dota 2 или в адресе профиля на opendota.com/players/<ID>."
            color: Theme.textFaint
            font.pixelSize: Theme.fontSm
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        GlassToggle { id: enabledToggle; text: "Показывать виджет на оверлее"; onToggled: root.save() }
    }

    GlassCard {
        Layout.fillWidth: true
        visible: root.snapshot.valid

        SectionHeader {
            Layout.fillWidth: true
            title: root.snapshot.personaname || ""
            subtitle: root.snapshot.rank_label ? ("Ранг: " + root.snapshot.rank_label) : "Ранг скрыт настройками приватности профиля"
        }

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
