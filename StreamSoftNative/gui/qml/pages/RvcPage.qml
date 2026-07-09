import QtQuick
import QtQuick.Layouts
import StreamSoftGui

ColumnLayout {
    id: root
    spacing: 18

    property bool loading: false
    property bool rvcAvailable: false

    function applySettings(settings) {
        loading = true
        enabledToggle.checked = !!settings.rvc_enabled
        scopeCombo.currentIndex = scopeCombo.indexOfValue(settings.rvc_scope || "alerts")
        f0Combo.currentIndex = f0Combo.indexOfValue(settings.rvc_f0method || "rmvpe")
        pitchSlider.value = settings.rvc_pitch !== undefined ? settings.rvc_pitch : 12
        indexRateSlider.value = (settings.rvc_index_rate !== undefined ? settings.rvc_index_rate : 0.3) * 100
        protectSlider.value = (settings.rvc_protect !== undefined ? settings.rvc_protect : 0.5) * 100
        loading = false
    }

    function save() {
        if (loading) return
        api.post("/api/settings", {
            rvc_enabled: enabledToggle.checked,
            rvc_scope: scopeCombo.currentValue,
            rvc_f0method: f0Combo.currentValue,
            rvc_pitch: Math.round(pitchSlider.value),
            rvc_index_rate: indexRateSlider.value / 100.0,
            rvc_protect: protectSlider.value / 100.0
        }, function () {})
    }

    function refreshHealth() {
        api.get("/api/rvc/health", function (ok, data) {
            root.rvcAvailable = ok && !!data.available
        })
    }

    Component.onCompleted: refreshHealth()

    Timer {
        interval: 5000
        running: true
        repeat: true
        onTriggered: root.refreshHealth()
    }

    SectionHeader {
        Layout.fillWidth: true
        title: "Клон голоса (RVC)"
        subtitle: "Меняет TTS-голос через локальный RVC-сервис."
    }

    ModuleInstallCard {
        Layout.fillWidth: true
        moduleName: "rvc"
        title: "Установка RVC-модуля"
    }

    GlassCard {
        Layout.fillWidth: true
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Rectangle {
                width: 9; height: 9; radius: 4.5
                color: root.rvcAvailable ? Theme.good : "#55555c"
            }
            Text {
                text: root.rvcAvailable ? "RVC-сервис доступен" : "RVC-сервис недоступен (запусти rvc_service/start.bat)"
                color: Theme.textFaint
                font.pixelSize: Theme.fontMd
            }
        }
    }

    GlassCard {
        Layout.fillWidth: true

        GlassToggle {
            id: enabledToggle
            text: "Включить смену голоса"
            onToggled: root.save()
        }

        Text { text: "Где применять"; color: Theme.textDim; font.pixelSize: Theme.fontMd; font.bold: true }
        GlassComboBox {
            id: scopeCombo
            Layout.fillWidth: true
            textRole: "text"
            valueRole: "value"
            model: [
                { text: "Только алерты", value: "alerts" },
                { text: "Весь чат", value: "all" }
            ]
            onActivated: root.save()
        }

        Text { text: "Метод F0"; color: Theme.textDim; font.pixelSize: Theme.fontMd; font.bold: true }
        GlassComboBox {
            id: f0Combo
            Layout.fillWidth: true
            textRole: "text"
            valueRole: "value"
            model: [
                { text: "rmvpe", value: "rmvpe" },
                { text: "crepe", value: "crepe" },
                { text: "harvest", value: "harvest" },
                { text: "pm", value: "pm" }
            ]
            onActivated: root.save()
        }

        RowLayout {
            Layout.fillWidth: true
            Text { text: "Питч"; color: Theme.textDim; font.pixelSize: Theme.fontMd; font.bold: true; Layout.fillWidth: true }
            Text {
                text: pitchSlider.value > 0 ? "+" + Math.round(pitchSlider.value) : Math.round(pitchSlider.value)
                color: Theme.text; font.pixelSize: Theme.fontMd; font.bold: true
            }
        }
        GlassSlider {
            id: pitchSlider
            Layout.fillWidth: true
            from: -24; to: 24; stepSize: 1
            onMoved: root.save()
        }

        RowLayout {
            Layout.fillWidth: true
            Text { text: "Index rate"; color: Theme.textDim; font.pixelSize: Theme.fontMd; font.bold: true; Layout.fillWidth: true }
            Text { text: (indexRateSlider.value / 100.0).toFixed(2); color: Theme.text; font.pixelSize: Theme.fontMd; font.bold: true }
        }
        GlassSlider {
            id: indexRateSlider
            Layout.fillWidth: true
            from: 0; to: 100; stepSize: 5
            onMoved: root.save()
        }

        RowLayout {
            Layout.fillWidth: true
            Text { text: "Protect"; color: Theme.textDim; font.pixelSize: Theme.fontMd; font.bold: true; Layout.fillWidth: true }
            Text { text: (protectSlider.value / 100.0).toFixed(2); color: Theme.text; font.pixelSize: Theme.fontMd; font.bold: true }
        }
        GlassSlider {
            id: protectSlider
            Layout.fillWidth: true
            from: 0; to: 50; stepSize: 1
            onMoved: root.save()
        }
    }

    Item { Layout.fillHeight: true }
}
