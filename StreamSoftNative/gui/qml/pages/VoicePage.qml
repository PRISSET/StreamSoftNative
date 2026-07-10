import QtQuick
import QtQuick.Layouts
import StreamSoftGui

// TTS and RVC used to live on two separate nav pages even though RVC is
// nothing but a voice filter *on top of* TTS output (see tts_worker.hpp's
// speak() — it synthesizes via the TTS adapter, then optionally pipes that
// through RVC before playback) — same reasoning for why the alert-reading
// volume slider stays here instead of on AlertsPage: that page is about
// per-event gif/mp3 media triggers, this page is about how the voice itself
// sounds, alerts included.
ColumnLayout {
    id: root
    spacing: 18

    property bool loading: false
    property bool rvcAvailable: false

    function applySettings(settings) {
        loading = true
        voiceRu.text = settings.tts_voice_ru || ""
        voiceEn.text = settings.tts_voice_en || ""
        rate.text = settings.tts_rate || "+0%"
        volume.value = settings.tts_volume !== undefined ? settings.tts_volume : 100
        sayAuthor.checked = !!settings.tts_say_author
        eventVolume.value = settings.event_volume !== undefined ? settings.event_volume : 100

        rvcEnabledToggle.checked = !!settings.rvc_enabled
        rvcScopeCombo.currentIndex = rvcScopeCombo.indexOfValue(settings.rvc_scope || "alerts")
        rvcF0Combo.currentIndex = rvcF0Combo.indexOfValue(settings.rvc_f0method || "rmvpe")
        rvcPitchSlider.value = settings.rvc_pitch !== undefined ? settings.rvc_pitch : 12
        rvcIndexRateSlider.value = (settings.rvc_index_rate !== undefined ? settings.rvc_index_rate : 0.3) * 100
        rvcProtectSlider.value = (settings.rvc_protect !== undefined ? settings.rvc_protect : 0.5) * 100
        loading = false
    }

    function saveTts() {
        if (loading) return
        api.post("/api/settings", {
            tts_voice_ru: voiceRu.text,
            tts_voice_en: voiceEn.text,
            tts_rate: rate.text,
            tts_volume: Math.round(volume.value),
            tts_say_author: sayAuthor.checked,
            event_volume: Math.round(eventVolume.value)
        }, function () {})
    }

    function saveRvc() {
        if (loading) return
        api.post("/api/settings", {
            rvc_enabled: rvcEnabledToggle.checked,
            rvc_scope: rvcScopeCombo.currentValue,
            rvc_f0method: rvcF0Combo.currentValue,
            rvc_pitch: Math.round(rvcPitchSlider.value),
            rvc_index_rate: rvcIndexRateSlider.value / 100.0,
            rvc_protect: rvcProtectSlider.value / 100.0
        }, function () {})
    }

    function refreshRvcHealth() {
        api.get("/api/rvc/health", function (ok, data) {
            root.rvcAvailable = ok && !!data.available
        })
    }

    Component.onCompleted: refreshRvcHealth()

    Timer {
        interval: 5000
        running: true
        repeat: true
        onTriggered: root.refreshRvcHealth()
    }

    SectionHeader {
        Layout.fillWidth: true
        title: "Голос"
        subtitle: "Озвучка чата и алертов (TTS) и клон голоса поверх неё (RVC) — всё, что слышно, в одном месте."
    }

    SectionHeader {
        Layout.fillWidth: true
        title: "Озвучка (TTS)"
        subtitle: "Голоса подбираются автоматически по языку сообщения."
    }

    ModuleInstallCard {
        Layout.fillWidth: true
        moduleName: "tts"
        title: "Установка TTS-модуля"
    }

    GlassCard {
        Layout.fillWidth: true

        RowLayout {
            Layout.fillWidth: true
            spacing: 16
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 7
                Text { text: "Голос (русский)"; color: Theme.textDim; font.pixelSize: Theme.fontMd; font.bold: true }
                GlassTextField {
                    id: voiceRu
                    Layout.fillWidth: true
                    placeholderText: "ru-RU-DmitryNeural"
                    onEditingFinished: root.saveTts()
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 7
                Text { text: "Голос (английский)"; color: Theme.textDim; font.pixelSize: Theme.fontMd; font.bold: true }
                GlassTextField {
                    id: voiceEn
                    Layout.fillWidth: true
                    placeholderText: "en-US-GuyNeural"
                    onEditingFinished: root.saveTts()
                }
            }
        }

        Text { text: "Скорость"; color: Theme.textDim; font.pixelSize: Theme.fontMd; font.bold: true }
        GlassTextField {
            id: rate
            Layout.fillWidth: true
            placeholderText: "+0%"
            onEditingFinished: root.saveTts()
        }
    }

    GlassCard {
        Layout.fillWidth: true

        RowLayout {
            Layout.fillWidth: true
            Text { text: "Громкость TTS"; color: Theme.textDim; font.pixelSize: Theme.fontMd; font.bold: true; Layout.fillWidth: true }
            Text { text: Math.round(volume.value) + "%"; color: Theme.text; font.pixelSize: Theme.fontMd; font.bold: true }
        }
        GlassSlider {
            id: volume
            Layout.fillWidth: true
            from: 0; to: 200; stepSize: 5
            onMoved: root.saveTts()
        }

        GlassToggle {
            id: sayAuthor
            text: "Озвучивать имя автора перед сообщением"
            onToggled: root.saveTts()
        }
    }

    GlassCard {
        Layout.fillWidth: true
        RowLayout {
            Layout.fillWidth: true
            Text { text: "Громкость чтения алертов"; color: Theme.textDim; font.pixelSize: Theme.fontMd; font.bold: true; Layout.fillWidth: true }
            Text { text: Math.round(eventVolume.value) + "%"; color: Theme.text; font.pixelSize: Theme.fontMd; font.bold: true }
        }
        GlassSlider {
            id: eventVolume
            Layout.fillWidth: true
            from: 0; to: 100; stepSize: 5
            onMoved: root.saveTts()
        }
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
                text: root.rvcAvailable ? "RVC-сервис доступен" : "RVC-сервис недоступен — приложение запускает его само, подождите пару секунд после установки"
                color: Theme.textFaint
                font.pixelSize: Theme.fontMd
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }
    }

    GlassCard {
        Layout.fillWidth: true

        GlassToggle {
            id: rvcEnabledToggle
            text: "Включить смену голоса"
            onToggled: root.saveRvc()
        }

        Text { text: "Где применять"; color: Theme.textDim; font.pixelSize: Theme.fontMd; font.bold: true }
        GlassComboBox {
            id: rvcScopeCombo
            Layout.fillWidth: true
            textRole: "text"
            valueRole: "value"
            model: [
                { text: "Только алерты", value: "alerts" },
                { text: "Весь чат", value: "all" }
            ]
            onActivated: root.saveRvc()
        }

        Text { text: "Метод F0"; color: Theme.textDim; font.pixelSize: Theme.fontMd; font.bold: true }
        GlassComboBox {
            id: rvcF0Combo
            Layout.fillWidth: true
            textRole: "text"
            valueRole: "value"
            model: [
                { text: "rmvpe", value: "rmvpe" },
                { text: "crepe", value: "crepe" },
                { text: "harvest", value: "harvest" },
                { text: "pm", value: "pm" }
            ]
            onActivated: root.saveRvc()
        }

        RowLayout {
            Layout.fillWidth: true
            Text { text: "Питч"; color: Theme.textDim; font.pixelSize: Theme.fontMd; font.bold: true; Layout.fillWidth: true }
            Text {
                text: rvcPitchSlider.value > 0 ? "+" + Math.round(rvcPitchSlider.value) : Math.round(rvcPitchSlider.value)
                color: Theme.text; font.pixelSize: Theme.fontMd; font.bold: true
            }
        }
        GlassSlider {
            id: rvcPitchSlider
            Layout.fillWidth: true
            from: -24; to: 24; stepSize: 1
            onMoved: root.saveRvc()
        }

        RowLayout {
            Layout.fillWidth: true
            Text { text: "Index rate"; color: Theme.textDim; font.pixelSize: Theme.fontMd; font.bold: true; Layout.fillWidth: true }
            Text { text: (rvcIndexRateSlider.value / 100.0).toFixed(2); color: Theme.text; font.pixelSize: Theme.fontMd; font.bold: true }
        }
        GlassSlider {
            id: rvcIndexRateSlider
            Layout.fillWidth: true
            from: 0; to: 100; stepSize: 5
            onMoved: root.saveRvc()
        }

        RowLayout {
            Layout.fillWidth: true
            Text { text: "Protect"; color: Theme.textDim; font.pixelSize: Theme.fontMd; font.bold: true; Layout.fillWidth: true }
            Text { text: (rvcProtectSlider.value / 100.0).toFixed(2); color: Theme.text; font.pixelSize: Theme.fontMd; font.bold: true }
        }
        GlassSlider {
            id: rvcProtectSlider
            Layout.fillWidth: true
            from: 0; to: 50; stepSize: 1
            onMoved: root.saveRvc()
        }
    }

    Item { Layout.fillHeight: true }
}
