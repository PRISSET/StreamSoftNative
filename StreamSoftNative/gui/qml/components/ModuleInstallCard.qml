import QtQuick
import QtQuick.Layouts
import StreamSoftGui

// "Check & Install" card for an optional module (TTS/RVC) — see
// CLAUDE.md §2 and core/include/module_installer.hpp. Polls
// GET /api/modules/<name>/status on a slow timer normally, switches to a
// fast GET .../progress poll only while a download/extract is actually
// running, and posts .../install to kick one off. One component shared by
// both TTS and RVC sections of VoicePage.qml instead of duplicating this.
GlassCard {
    id: root
    property string moduleName: ""
    property string title: ""

    property bool checking: true
    property bool installed: false
    property bool requirementsMet: true
    property string requirementsReason: ""
    property real downloadMb: 0
    property string state: "idle"
    property int fileIndex: 0
    property int fileCount: 0
    property real bytesDownloaded: 0
    property real bytesTotal: 0
    property string currentStep: ""
    property string errorMsg: ""

    readonly property bool busy: state === "downloading" || state === "extracting" || state === "installing"
    // "installing" (RVC's live pip install) has no byte-accurate progress —
    // each pip/subprocess stage just advances fileIndex, so the bar shows
    // coarse "step X of N" progress instead of a byte count there.
    readonly property real percent: state === "installing"
        ? (fileCount > 0 ? (fileIndex / fileCount) * 100 : 0)
        : (bytesTotal > 0 ? (bytesDownloaded / bytesTotal) * 100 : 0)

    function refreshStatus() {
        api.get("/api/modules/" + moduleName + "/status", function (ok, data) {
            if (!ok) return
            root.checking = false
            root.installed = !!data.installed
            root.requirementsMet = !!data.requirements_met
            root.requirementsReason = data.requirements_reason || ""
            root.downloadMb = data.download_mb || 0
            root.state = data.state || "idle"
            root.errorMsg = data.error || ""
        })
    }

    function refreshProgress() {
        api.get("/api/modules/" + moduleName + "/progress", function (ok, data) {
            if (!ok) return
            root.state = data.state || root.state
            root.fileIndex = data.file_index || 0
            root.fileCount = data.file_count || 0
            root.bytesDownloaded = data.bytes_downloaded || 0
            root.bytesTotal = data.bytes_total || 0
            root.currentStep = data.current_step || ""
            root.errorMsg = data.error || ""
            if (root.state === "installed") root.installed = true
            if (root.state === "idle" || root.state === "installed" || root.state === "failed") root.refreshStatus()
        })
    }

    function install() {
        root.errorMsg = ""
        root.state = "downloading"
        api.post("/api/modules/" + moduleName + "/install", {}, function (ok, data) {
            if (!ok) {
                root.state = "failed"
                root.errorMsg = (data && data.error) || "Не удалось начать установку"
            }
        }, false)
    }

    Component.onCompleted: refreshStatus()

    Timer {
        interval: 3000
        running: !root.busy
        repeat: true
        onTriggered: root.refreshStatus()
    }
    Timer {
        interval: 600
        running: root.busy
        repeat: true
        onTriggered: root.refreshProgress()
    }

    SectionHeader {
        Layout.fillWidth: true
        title: root.title
        subtitle: root.installed
            ? "Установлено"
            : (root.downloadMb > 0 ? "Размер загрузки: ~" + root.downloadMb + " МБ" : "Проверка требований…")
    }

    Text {
        visible: !root.checking && !root.installed && !root.requirementsMet && root.state === "idle"
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        color: Theme.bad
        font.pixelSize: Theme.fontMd
        text: root.requirementsReason
    }

    RowLayout {
        visible: !root.checking && !root.installed && root.requirementsMet && root.state === "idle"
        Layout.fillWidth: true
        Text {
            Layout.fillWidth: true
            color: Theme.textDim
            font.pixelSize: Theme.fontMd
            text: "Готово к установке"
        }
        PillButton {
            text: "Установить"
            onClicked: root.install()
        }
    }

    ColumnLayout {
        visible: root.busy
        Layout.fillWidth: true
        spacing: 6
        Text {
            color: Theme.textDim
            font.pixelSize: Theme.fontMd
            text: {
                if (root.state === "installing") return root.currentStep || "Установка…"
                if (root.state === "extracting") return "Распаковка…"
                return "Скачивание" + (root.fileCount > 1 ? " (часть " + root.fileIndex + " из " + root.fileCount + ")" : "") + "…"
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
                width: parent.width * Math.max(0, Math.min(1, root.percent / 100))
                Behavior on width { NumberAnimation { duration: 200 } }
            }
        }
        Text {
            visible: root.state === "downloading" && root.bytesTotal > 0
            color: Theme.textFaint
            font.pixelSize: Theme.fontSm
            text: Math.round(root.bytesDownloaded / 1048576) + " / " + Math.round(root.bytesTotal / 1048576) + " МБ"
        }
    }

    ColumnLayout {
        visible: root.state === "failed"
        Layout.fillWidth: true
        spacing: 6
        Text {
            Layout.fillWidth: true
            color: Theme.bad
            font.pixelSize: Theme.fontMd
            wrapMode: Text.WordWrap
            text: "Ошибка установки: " + root.errorMsg
        }
        PillButton {
            text: "Повторить"
            onClicked: root.install()
        }
    }

    RowLayout {
        visible: root.installed
        Layout.fillWidth: true
        spacing: 8
        Rectangle { width: 9; height: 9; radius: 4.5; color: Theme.good }
        Text { color: Theme.textFaint; font.pixelSize: Theme.fontMd; text: "Модуль установлен" }
    }
}
