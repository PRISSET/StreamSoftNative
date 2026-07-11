import QtQuick
import QtQuick.Layouts
import StreamSoftGui

ColumnLayout {
    id: root
    spacing: 18

    property string currentVersion: ""
    property var releases: []
    property bool loading: true

    function refresh() {
        root.loading = true
        api.get("/api/updates", function (ok, data) {
            root.loading = false
            if (!ok) return
            root.currentVersion = data.current_version || ""
            root.releases = data.releases || []
        })
    }

    function formatDate(iso) {
        if (!iso) return ""
        var d = new Date(iso)
        if (isNaN(d.getTime())) return ""
        return Qt.formatDate(d, "d MMMM yyyy")
    }

    Component.onCompleted: refresh()

    SectionHeader {
        Layout.fillWidth: true
        title: "Обновления"
        subtitle: root.currentVersion ? ("Установленная версия: " + root.currentVersion) : "Проверка версии…"
    }

    GlassCard {
        Layout.fillWidth: true
        visible: root.loading
        Text { text: "Загрузка списка обновлений…"; color: Theme.textFaint; font.pixelSize: Theme.fontMd }
    }

    GlassCard {
        Layout.fillWidth: true
        visible: !root.loading && root.releases.length === 0
        Text {
            text: "Не удалось получить список обновлений — нет соединения с GitHub"
            color: Theme.textFaint
            font.pixelSize: Theme.fontMd
            wrapMode: Text.WordWrap
        }
    }

    Repeater {
        model: root.releases
        delegate: GlassCard {
            required property var modelData
            Layout.fillWidth: true

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Text {
                        text: modelData.version
                        color: Theme.text
                        font.pixelSize: Theme.fontLg
                        font.bold: true
                    }
                    Text {
                        visible: modelData.version === root.currentVersion
                        text: "(установлено)"
                        color: Theme.good
                        font.pixelSize: Theme.fontSm
                    }
                    Item { Layout.fillWidth: true }
                    Text {
                        text: root.formatDate(modelData.published_at)
                        color: Theme.textFaint
                        font.pixelSize: Theme.fontSm
                    }
                }

                Text {
                    Layout.fillWidth: true
                    visible: !!modelData.notes
                    text: modelData.notes || ""
                    color: Theme.textDim
                    font.pixelSize: Theme.fontMd
                    wrapMode: Text.WordWrap
                }
            }
        }
    }

    Item { Layout.fillHeight: true }
}
