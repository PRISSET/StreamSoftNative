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
        delegate: CollapsibleCard {
            required property var modelData
            Layout.fillWidth: true
            settingsKey: "updates_release_" + modelData.version
            // Every release gets its own persisted collapse state (keyed by
            // version) rather than a shared one — old changelog entries
            // someone's already read stay collapsed while a freshly
            // installed version's notes stay open.
            title: modelData.version + (modelData.version === root.currentVersion ? "  (установлено)" : "")
            subtitle: root.formatDate(modelData.published_at)

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

    Item { Layout.fillHeight: true }
}
