import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Window
import QtQuick.Effects
import StreamSoftGui

ColumnLayout {
    id: root
    spacing: 18

    readonly property var win: root.Window.window

    function refreshLog() {
        api.get("/api/log?lines=300", function (ok, data) {
            logText.text = ok && data.text ? data.text : "Лог пока пуст или недоступен."
        })
    }

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

        Text { text: "Оформление"; color: Theme.textDim; font.pixelSize: Theme.fontMd; font.bold: true }

        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            Item {
                Layout.fillWidth: true
                implicitHeight: 44

                GlassSurface {
                    anchors.fill: parent
                    radiusPx: Theme.radiusMd
                    pad: 8
                    refractPx: 7
                    tintColor: !root.win.flatTheme ? Theme.accentAlpha(0.22) : (glassHover.hovered ? Theme.glassFillHover : Theme.glassFill)
                    rimColor: !root.win.flatTheme ? Theme.accent : Theme.glassBorder
                    Behavior on tintColor { ColorAnimation { duration: Theme.motionFast } }
                    Behavior on rimColor { ColorAnimation { duration: Theme.motionFast } }
                }
                HoverHandler { id: glassHover }
                TapHandler {
                    cursorShape: Qt.PointingHandCursor
                    onTapped: { root.win.flatTheme = false; root.win.notifySaved() }
                }
                Text {
                    anchors.centerIn: parent
                    text: "Glass"
                    color: !root.win.flatTheme ? "#ffffff" : Theme.textDim
                    font.pixelSize: Theme.fontMd
                    font.weight: Font.DemiBold
                }
            }

            Item {
                Layout.fillWidth: true
                implicitHeight: 44

                GlassSurface {
                    anchors.fill: parent
                    radiusPx: Theme.radiusMd
                    pad: 8
                    refractPx: 7
                    tintColor: root.win.flatTheme ? Theme.accentAlpha(0.22) : (flatHover.hovered ? Theme.glassFillHover : Theme.glassFill)
                    rimColor: root.win.flatTheme ? Theme.accent : Theme.glassBorder
                    Behavior on tintColor { ColorAnimation { duration: Theme.motionFast } }
                    Behavior on rimColor { ColorAnimation { duration: Theme.motionFast } }
                }
                HoverHandler { id: flatHover }
                TapHandler {
                    cursorShape: Qt.PointingHandCursor
                    onTapped: { root.win.flatTheme = true; root.win.notifySaved() }
                }
                Text {
                    anchors.centerIn: parent
                    text: "Обычная"
                    color: root.win.flatTheme ? "#ffffff" : Theme.textDim
                    font.pixelSize: Theme.fontMd
                    font.weight: Font.DemiBold
                }
            }
        }

        Text {
            text: "«Обычная» — без эффекта стекла и блюра, фон зафиксирован одним цветом и его нельзя сменить. Подходит, если стекло не нравится или тормозит на слабом железе."
            color: Theme.textFaint
            font.pixelSize: Theme.fontSm
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    }

    GlassCard {
        Layout.fillWidth: true
        visible: root.win.flatTheme

        Text { text: "Фон приложения"; color: Theme.textDim; font.pixelSize: Theme.fontMd; font.bold: true }
        Text {
            text: "В «Обычной» теме фон зафиксирован — выбор картинки и блюр недоступны. Переключись на «Glass» выше, чтобы вернуть настройку фона."
            color: Theme.textFaint
            font.pixelSize: Theme.fontSm
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    }

    GlassCard {
        Layout.fillWidth: true
        visible: !root.win.flatTheme

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
        visible: !root.win.flatTheme

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
        visible: !root.win.flatTheme

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

    GlassCard {
        Layout.fillWidth: true

        SectionHeader {
            Layout.fillWidth: true
            title: "Логи"
            subtitle: "Последние строки streamsoft.log — полезно для диагностики, если что-то не подключается (Twitch, TTS и т.д.)."
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            PillButton {
                text: "Обновить"
                onClicked: root.refreshLog()
            }
            PillButton {
                text: "Скопировать"
                onClicked: { logText.selectAll(); logText.copy(); logText.deselect() }
            }
            Item { Layout.fillWidth: true }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 260
            radius: Theme.radiusMd
            color: Theme.fieldBg
            border.width: 1
            border.color: Theme.fieldBorder

            ScrollView {
                anchors.fill: parent
                anchors.margins: 10
                clip: true

                TextArea {
                    id: logText
                    readOnly: true
                    wrapMode: TextArea.NoWrap
                    color: Theme.textDim
                    font.family: "Consolas"
                    font.pixelSize: 11
                    selectByMouse: true
                    background: null
                }
            }
        }

        Component.onCompleted: root.refreshLog()
    }

    Item { Layout.fillHeight: true }
}
