import QtQuick
import QtQuick.Layouts
import QtQuick.Dialogs
import StreamSoftGui

ColumnLayout {
    id: root
    spacing: 18

    readonly property var kinds: [
        { key: "follow", label: "Фоллоу" },
        { key: "subscribe", label: "Подписка" },
        { key: "gift_sub", label: "Подарочная подписка" },
        { key: "raid", label: "Рейд" },
        { key: "cheer", label: "Донат битсами" },
        { key: "youtube_sub", label: "YouTube: новый участник" },
        { key: "youtube_sub_milestone", label: "YouTube: юбилей участия" },
        { key: "youtube_gift_sub", label: "YouTube: подарочное участие" },
        { key: "youtube_superchat", label: "YouTube: Super Chat" },
        { key: "youtube_supersticker", label: "YouTube: Super Sticker" }
    ]

    property var status: ({})

    function refresh() {
        api.get("/api/media/status", function (ok, data) {
            if (ok) root.status = data
        })
    }

    Component.onCompleted: refresh()

    FileDialog {
        id: fileDialog
        property string kind: ""
        property string ext: ""
        nameFilters: ext === "gif" ? ["GIF files (*.gif)"] : ["MP3 files (*.mp3)"]
        onAccepted: {
            api.uploadFile("/api/media/" + kind + "/" + ext, selectedFile,
                           ext === "gif" ? "image/gif" : "audio/mpeg",
                           function () { root.refresh() })
        }
    }

    SectionHeader {
        Layout.fillWidth: true
        title: "Алерты и медиа"
        subtitle: "Гифка и звук на каждое событие — follow.gif/mp3 и т.д."
    }

    CollapsibleCard {
        Layout.fillWidth: true
        settingsKey: "alerts_media"
        title: "Настройка алертов"

        Repeater {
            model: root.kinds
            delegate: RowLayout {
                required property var modelData
                Layout.fillWidth: true
                spacing: 12

                Text {
                    text: modelData.label
                    color: Theme.text
                    font.pixelSize: Theme.fontMd
                    font.bold: true
                    wrapMode: Text.WordWrap
                    Layout.preferredWidth: 210
                    Layout.maximumWidth: 210
                }

                MediaSlot {
                    ext: "gif"
                    exists: !!(root.status[modelData.key] && root.status[modelData.key].gif)
                    onRequestUpload: { fileDialog.kind = modelData.key; fileDialog.ext = "gif"; fileDialog.open() }
                    onRequestDelete: api.del("/api/media/" + modelData.key + "/gif", function () { root.refresh() })
                }
                MediaSlot {
                    ext: "mp3"
                    exists: !!(root.status[modelData.key] && root.status[modelData.key].mp3)
                    onRequestUpload: { fileDialog.kind = modelData.key; fileDialog.ext = "mp3"; fileDialog.open() }
                    onRequestDelete: api.del("/api/media/" + modelData.key + "/mp3", function () { root.refresh() })
                }

                Item { Layout.fillWidth: true }
            }
        }
    }

    Item { Layout.fillHeight: true }
}
