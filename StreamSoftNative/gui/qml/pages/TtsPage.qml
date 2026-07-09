import QtQuick
import QtQuick.Layouts
import StreamSoftGui

ColumnLayout {
    id: root
    spacing: 18

    property bool loading: false

    function applySettings(settings) {
        loading = true
        voiceRu.text = settings.tts_voice_ru || ""
        voiceEn.text = settings.tts_voice_en || ""
        rate.text = settings.tts_rate || "+0%"
        volume.value = settings.tts_volume !== undefined ? settings.tts_volume : 100
        sayAuthor.checked = !!settings.tts_say_author
        eventVolume.value = settings.event_volume !== undefined ? settings.event_volume : 100
        loading = false
    }

    function save() {
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
                    onEditingFinished: root.save()
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
                    onEditingFinished: root.save()
                }
            }
        }

        Text { text: "Скорость"; color: Theme.textDim; font.pixelSize: Theme.fontMd; font.bold: true }
        GlassTextField {
            id: rate
            Layout.fillWidth: true
            placeholderText: "+0%"
            onEditingFinished: root.save()
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
            onMoved: root.save()
        }

        GlassToggle {
            id: sayAuthor
            text: "Озвучивать имя автора перед сообщением"
            onToggled: root.save()
        }
    }

    GlassCard {
        Layout.fillWidth: true
        SectionHeader {
            Layout.fillWidth: true
            title: "Алерты"
            subtitle: "Громкость звука алертов (follow/sub/raid/cheer)."
        }
        RowLayout {
            Layout.fillWidth: true
            Text { text: "Громкость звука алертов"; color: Theme.textDim; font.pixelSize: Theme.fontMd; font.bold: true; Layout.fillWidth: true }
            Text { text: Math.round(eventVolume.value) + "%"; color: Theme.text; font.pixelSize: Theme.fontMd; font.bold: true }
        }
        GlassSlider {
            id: eventVolume
            Layout.fillWidth: true
            from: 0; to: 100; stepSize: 5
            onMoved: root.save()
        }
    }

    Item { Layout.fillHeight: true }
}
