import QtQuick
import QtQuick.Layouts
import QtQuick.Dialogs
import QtQuick.Window
import QtQuick.Effects
import StreamSoftGui

ColumnLayout {
    id: root
    spacing: 18

    readonly property var win: root.Window.window

    FileDialog {
        id: bgFileDialog
        title: "Выбери изображение фона"
        nameFilters: ["Изображения (*.png *.jpg *.jpeg *.webp *.bmp)"]
        onAccepted: {
            var name = nameField.text.trim()
            if (!name) name = selectedFile.toString().split("/").pop()
            win.addCustomTheme(name, selectedFile)
            nameField.text = ""
        }
    }

    SectionHeader {
        Layout.fillWidth: true
        title: "Настройки"
        subtitle: "Внешний вид приложения — фон и тема окна."
    }

    GlassCard {
        Layout.fillWidth: true

        Text { text: "Фон приложения"; color: Theme.textDim; font.pixelSize: Theme.fontMd; font.bold: true }

        GlassComboBox {
            id: bgCombo
            Layout.fillWidth: true
            textRole: "text"
            valueRole: "value"
            model: [
                { text: "Звёзды", value: "stars" },
                { text: "Голограмма", value: "holo" },
                { text: "Шахматы", value: "chess" }
            ]
            Component.onCompleted: currentIndex = indexOfValue(root.win.backgroundStyle)
            onActivated: {
                root.win.backgroundStyle = currentValue
                root.win.notifySaved()
            }
        }

        Text {
            text: "Применяется сразу, сохраняется между запусками."
            color: Theme.textFaint
            font.pixelSize: Theme.fontSm
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    }

    GlassCard {
        Layout.fillWidth: true

        Text { text: "Блюр фона"; color: Theme.textDim; font.pixelSize: Theme.fontMd; font.bold: true }

        GlassToggle {
            id: blurToggle
            text: "Размывать фон под стеклом"
            checked: root.win.blurBackgroundEnabled
            onToggled: {
                root.win.blurBackgroundEnabled = checked
                root.win.notifySaved()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            enabled: blurToggle.checked
            opacity: enabled ? 1.0 : 0.4
            Behavior on opacity { NumberAnimation { duration: Theme.motionFast } }

            GlassSlider {
                id: blurSlider
                Layout.fillWidth: true
                from: 0
                to: 100
                stepSize: 1
                value: root.win.blurAmount
                onMoved: {
                    root.win.blurAmount = value
                    root.win.notifySaved()
                }
            }
            Text {
                text: Math.round(blurSlider.value) + "%"
                color: Theme.textDim
                font.pixelSize: Theme.fontMd
                Layout.preferredWidth: 40
            }
        }
    }

    GlassCard {
        Layout.fillWidth: true

        Text { text: "Мои темы"; color: Theme.textDim; font.pixelSize: Theme.fontMd; font.bold: true }
        Text {
            text: "Своя картинка на фон — можно добавить сколько угодно и переключаться."
            color: Theme.textFaint
            font.pixelSize: Theme.fontSm
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 6
            visible: root.win.customThemes.length > 0

            Repeater {
                model: root.win.customThemes
                delegate: RowLayout {
                    required property var modelData
                    required property int index
                    Layout.fillWidth: true
                    spacing: 10

                    property bool isActive: root.win.backgroundStyle === "custom" && root.win.activeCustomIndex === index

                    Rectangle {
                        width: 8; height: 8; radius: 4
                        color: isActive ? Theme.good : "#55555c"
                    }
                    Text {
                        text: modelData.name
                        color: isActive ? Theme.text : Theme.textDim
                        font.pixelSize: Theme.fontMd
                        font.bold: isActive
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                        layer.enabled: true
                        layer.effect: MultiEffect {
                            shadowEnabled: true
                            shadowColor: "#d9000000"
                            shadowBlur: 0.7
                            shadowVerticalOffset: 1
                        }
                    }
                    PillButton {
                        text: "Применить"
                        implicitHeight: 30
                        enabled: !isActive
                        onClicked: {
                            root.win.activeCustomIndex = index
                            root.win.backgroundStyle = "custom"
                            root.win.notifySaved()
                        }
                    }
                    PillButton {
                        danger: true
                        text: "✕"
                        implicitHeight: 30
                        implicitWidth: 36
                        onClicked: root.win.removeCustomTheme(index)
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 4
            spacing: 10

            GlassTextField {
                id: nameField
                Layout.preferredWidth: 200
                placeholderText: "Название темы"
            }
            PillButton {
                text: "Выбрать изображение"
                onClicked: bgFileDialog.open()
            }
        }
    }

    Item { Layout.fillHeight: true }
}
