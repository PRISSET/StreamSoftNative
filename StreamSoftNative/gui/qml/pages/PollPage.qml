import QtQuick
import QtQuick.Layouts
import StreamSoftGui

// Zрители голосуют бросая "!1"/"!2"/... в чат (see core/include/poll.hpp) —
// this page just starts/stops the poll and shows the live tally, the
// actual counting happens in core regardless of whether this page is open.
ColumnLayout {
    id: root
    spacing: 18

    property bool active: false
    property string question: ""
    property var options: []
    property int totalVotes: 0

    function refresh() {
        api.get("/api/poll/status", function (ok, data) {
            if (!ok) return
            root.active = !!data.active
            root.question = data.question || ""
            root.options = data.options || []
            root.totalVotes = data.total_votes || 0
        })
    }

    function start() {
        var opts = []
        for (var i = 0; i < optionFields.length; i++) {
            var text = optionFields[i].text.trim()
            if (text) opts.push(text)
        }
        if (!questionField.text.trim() || opts.length < 2) return
        api.post("/api/poll/start", { question: questionField.text.trim(), options: opts }, function (ok) {
            if (ok) root.refresh()
        })
    }

    function stop() {
        api.post("/api/poll/stop", {}, function () { root.refresh() })
    }

    property var optionFields: [option1, option2, option3, option4]

    Component.onCompleted: refresh()

    Timer {
        interval: 2000
        running: root.active
        repeat: true
        onTriggered: root.refresh()
    }

    SectionHeader {
        Layout.fillWidth: true
        title: "Опрос"
        subtitle: "Зрители голосуют командами !1, !2 и т.д. прямо в чате — результат сразу на оверлее."
    }

    GlassCard {
        Layout.fillWidth: true
        visible: !root.active

        Text { text: "Вопрос"; color: Theme.textDim; font.pixelSize: Theme.fontMd; font.bold: true }
        GlassTextField {
            id: questionField
            Layout.fillWidth: true
            placeholderText: "Что играем дальше?"
        }

        Text { text: "Варианты (минимум 2)"; color: Theme.textDim; font.pixelSize: Theme.fontMd; font.bold: true }
        GlassTextField { id: option1; Layout.fillWidth: true; placeholderText: "Вариант 1" }
        GlassTextField { id: option2; Layout.fillWidth: true; placeholderText: "Вариант 2" }
        GlassTextField { id: option3; Layout.fillWidth: true; placeholderText: "Вариант 3 (необязательно)" }
        GlassTextField { id: option4; Layout.fillWidth: true; placeholderText: "Вариант 4 (необязательно)" }

        PillButton {
            text: "Начать опрос"
            onClicked: root.start()
        }
    }

    GlassCard {
        Layout.fillWidth: true
        visible: root.active

        RowLayout {
            Layout.fillWidth: true
            Text {
                text: root.question
                color: Theme.text
                font.pixelSize: Theme.fontLg
                font.bold: true
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
            PillButton {
                danger: true
                text: "Остановить"
                onClicked: root.stop()
            }
        }

        Repeater {
            model: root.options
            delegate: ColumnLayout {
                required property var modelData
                required property int index
                Layout.fillWidth: true
                spacing: 4

                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        text: "!" + (index + 1) + " — " + modelData.text
                        color: Theme.textDim; font.pixelSize: Theme.fontMd; Layout.fillWidth: true
                    }
                    Text {
                        text: root.totalVotes > 0 ? Math.round((modelData.votes / root.totalVotes) * 100) + "% (" + modelData.votes + ")" : "0%"
                        color: Theme.text; font.pixelSize: Theme.fontMd; font.bold: true
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    height: 8
                    radius: 4
                    color: Theme.glassFill
                    border.width: 1
                    border.color: Theme.glassBorder
                    Rectangle {
                        height: parent.height
                        radius: parent.radius
                        color: Theme.good
                        width: parent.width * (root.totalVotes > 0 ? modelData.votes / root.totalVotes : 0)
                        Behavior on width { NumberAnimation { duration: 200 } }
                    }
                }
            }
        }
    }

    Item { Layout.fillHeight: true }
}
