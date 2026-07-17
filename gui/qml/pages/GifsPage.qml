import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Dialogs
import StreamSoftGui

ColumnLayout {
    id: root
    spacing: 18

    property var gifs: []

    function refresh() {
        api.get("/api/gifs", function (ok, data) {
            if (ok) root.gifs = data.gifs || []
        })
    }

    function addGif() {
        var name = nameField.text.trim().toLowerCase()
        if (!name) return
        api.post("/api/gifs", { name: name, price: priceSpin.value }, function () {
            nameField.text = ""
            priceSpin.value = 50
            root.refresh()
        })
    }

    Component.onCompleted: refresh()

    FileDialog {
        id: fileDialog
        property string gifName: ""
        property string ext: ""
        nameFilters: ext === "gif" ? ["GIF files (*.gif)"] : ["MP3 files (*.mp3)"]
        onAccepted: {
            api.uploadFile("/api/gifs/" + encodeURIComponent(gifName) + "/" + ext, selectedFile,
                           ext === "gif" ? "image/gif" : "audio/mpeg",
                           function () { root.refresh() })
        }
    }

    SectionHeader {
        Layout.fillWidth: true
        title: "Гифки за баллы"
        subtitle: "Зритель тратит баллы командой \"!gif <имя>\" — на оверлее проигрывается гифка и/или звук поверх алертов. Список — \"!gifs\"."
    }

    CollapsibleCard {
        Layout.fillWidth: true
        settingsKey: "gifs_add"
        title: "Добавить гифку"

        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            GlassTextField {
                id: nameField
                Layout.preferredWidth: 160
                placeholderText: "имя (например rickroll)"
            }
            SpinBox {
                id: priceSpin
                from: 0
                to: 100000
                value: 50
                stepSize: 10
                editable: true
                implicitHeight: 40
                implicitWidth: 110
                background: Rectangle {
                    radius: Theme.radiusMd
                    color: Theme.fieldBg
                    border.width: 1
                    border.color: Theme.fieldBorder
                }
                contentItem: TextInput {
                    text: priceSpin.textFromValue(priceSpin.value, priceSpin.locale)
                    color: Theme.text
                    font.pixelSize: Theme.fontLg
                    horizontalAlignment: Qt.AlignHCenter
                    verticalAlignment: Qt.AlignVCenter
                    readOnly: !priceSpin.editable
                    selectByMouse: true
                }
                up.indicator: Item {}
                down.indicator: Item {}
            }
            PillButton {
                text: "Добавить"
                onClicked: root.addGif()
            }
        }
        Text {
            text: "Сначала добавь имя и цену, потом прикрепи гифку и/или звук в списке ниже — можно и то, и то, можно только одно."
            color: Theme.textFaint
            font.pixelSize: Theme.fontSm
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    }

    CollapsibleCard {
        Layout.fillWidth: true
        settingsKey: "gifs_list"
        title: "Список"

        Text {
            visible: root.gifs.length === 0
            text: "Гифок пока нет"
            color: Theme.textFaint
            font.pixelSize: Theme.fontMd
        }

        Repeater {
            model: root.gifs
            delegate: ColumnLayout {
                required property var modelData
                Layout.fillWidth: true
                spacing: 6

                RowLayout {
                    Layout.fillWidth: true
                    Text { text: modelData.name; color: Theme.text; font.pixelSize: Theme.fontMd; font.bold: true; Layout.preferredWidth: 140; elide: Text.ElideRight }
                    Text { text: modelData.price + " баллов"; color: Theme.textDim; font.pixelSize: Theme.fontSm; Layout.fillWidth: true }
                    PillButton {
                        danger: true
                        text: "✕"
                        implicitHeight: 30
                        implicitWidth: 36
                        onClicked: api.del("/api/gifs/" + encodeURIComponent(modelData.name), function () { root.refresh() })
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 4
                    spacing: 24
                    MediaSlot {
                        ext: "gif"
                        exists: !!modelData.hasGif
                        onRequestUpload: { fileDialog.gifName = modelData.name; fileDialog.ext = "gif"; fileDialog.open() }
                        onRequestDelete: api.del("/api/gifs/" + encodeURIComponent(modelData.name) + "/gif", function () { root.refresh() })
                    }
                    MediaSlot {
                        ext: "mp3"
                        exists: !!modelData.hasMp3
                        onRequestUpload: { fileDialog.gifName = modelData.name; fileDialog.ext = "mp3"; fileDialog.open() }
                        onRequestDelete: api.del("/api/gifs/" + encodeURIComponent(modelData.name) + "/mp3", function () { root.refresh() })
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.hairline; Layout.topMargin: 4 }
            }
        }
    }

    Item { Layout.fillHeight: true }
}
