import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import QtCore
import Qt.labs.platform as Labs
import StreamSoftGui

ApplicationWindow {
    id: window
    width: 1180
    height: 780
    minimumWidth: 900
    minimumHeight: 600
    visible: true
    title: "StreamSoft — настройки"
    color: Theme.bg

    // Closing the window just hides it — the background service (chat
    // workers, overlay server, TTS) keeps running on its own thread
    // regardless of whether this settings window is open, so there's
    // nothing to actually shut down here. Real exit is only via the tray
    // menu's "Выход".
    //
    // `quitting` guards this: QCoreApplication::quit() tries to close all
    // top-level windows as part of shutdown, and if this handler always
    // vetoes that, quit() never actually returns from app.exec() — verified
    // via a standalone repro. The tray's "Выход" sets this before quitting.
    property bool quitting: false
    onClosing: (close) => {
        if (quitting) {
            close.accepted = true
        } else {
            close.accepted = false
            window.hide()
        }
    }

    Labs.SystemTrayIcon {
        id: trayIcon
        visible: true
        icon.source: "qrc:/qt/qml/StreamSoftGui/qml/assets/icons/app-icon.png"
        tooltip: "StreamSoft"

        menu: Labs.Menu {
            Labs.MenuItem {
                text: "Открыть StreamSoft"
                onTriggered: { window.show(); window.raise(); window.requestActivate() }
            }
            Labs.MenuSeparator {}
            Labs.MenuItem {
                text: "Выход"
                onTriggered: { window.quitting = true; Qt.quit() }
            }
        }

        onActivated: (reason) => {
            if (reason === Labs.SystemTrayIcon.Trigger || reason === Labs.SystemTrayIcon.DoubleClick) {
                window.show()
                window.raise()
                window.requestActivate()
            }
        }
    }

    property string backgroundStyle: "stars"
    property int activeCustomIndex: -1
    property string customThemesData: "[]"
    property var customThemes: []

    property bool blurBackgroundEnabled: true
    property real blurAmount: 50
    // Read by every GlassSurface (cards, sidebar, fields, nav, sliders...)
    // as their default blur tap radius — one slider drives the whole app.
    // 6px is the widest tap spacing that still reads as "glass" rather
    // than "mush" at typical card sizes; 0 fully disables the blur taps.
    readonly property real blurRadius: blurBackgroundEnabled ? (blurAmount / 100.0) * 6.0 : 0.0

    Settings {
        property alias backgroundStyle: window.backgroundStyle
        property alias activeCustomIndex: window.activeCustomIndex
        property alias customThemesData: window.customThemesData
        property alias blurBackgroundEnabled: window.blurBackgroundEnabled
        property alias blurAmount: window.blurAmount
    }

    function loadCustomThemes() {
        try {
            var parsed = JSON.parse(customThemesData)
            customThemes = Array.isArray(parsed) ? parsed : []
        } catch (e) {
            customThemes = []
        }
    }
    function saveCustomThemes() {
        customThemesData = JSON.stringify(customThemes)
    }
    function addCustomTheme(name, path) {
        var list = customThemes.slice()
        list.push({ name: name, path: path.toString() })
        customThemes = list
        saveCustomThemes()
        activeCustomIndex = list.length - 1
        backgroundStyle = "custom"
        notifySaved()
    }
    function removeCustomTheme(index) {
        var list = customThemes.slice()
        list.splice(index, 1)
        customThemes = list
        saveCustomThemes()
        if (activeCustomIndex === index) {
            activeCustomIndex = -1
            if (backgroundStyle === "custom") backgroundStyle = "stars"
        } else if (activeCustomIndex > index) {
            activeCustomIndex -= 1
        }
        notifySaved()
    }

    // One toast for every kind of change in the app: api.mutationSucceeded
    // covers every page's own save()/upload/delete (see api_client.cpp —
    // fires on POST/DELETE, never on GET, so loading a page never pops
    // this), local-only settings (background/blur/themes) call it directly
    // since those never touch the REST API at all.
    function notifySaved(message) {
        toast.show(message || "Сохранено")
    }

    Connections {
        target: api
        function onMutationSucceeded() { notifySaved() }
    }

    Item {
        id: backdrop
        anchors.fill: parent

        Rectangle {
            anchors.fill: parent
            color: "#000000"
        }

        // Blurred once here, at the wallpaper layer — not per-card. Every
        // GlassSurface still applies its own live refraction on top of
        // this, but the raw picture visible in the gaps between panels
        // (sidebar edges, page margins) now reads as soft "blurred
        // wallpaper" instead of a pin-sharp image poking through around
        // otherwise-blurred glass. Foreground UI (cards, buttons, text)
        // lives in a separate sibling below and is never part of this
        // layer, so it's never touched by this blur.
        Item {
            id: wallpaper
            anchors.fill: parent
            layer.enabled: window.blurBackgroundEnabled && window.blurAmount > 0
            layer.smooth: true
            layer.effect: MultiEffect {
                blurEnabled: true
                blur: 1.0
                blurMax: (window.blurAmount / 100.0) * 48.0
                autoPaddingEnabled: false
            }

            // Fixed, hand-placed layout (fractional so it scales with the
            // window) rather than Math.random() — keeps the composition
            // deliberate instead of risking an awkward cluster on resize.
            Item {
                anchors.fill: parent
                visible: window.backgroundStyle === "stars"

                Star { x: parent.width * 0.05; y: parent.height * 0.55; size: 30; driftRange: 10; driftDuration: 7200; driftDelay: 0 }
                Star { x: parent.width * 0.14; y: parent.height * 0.12; size: 48; driftRange: 16; driftDuration: 9000; driftDelay: 600 }
                Star { x: parent.width * 0.27; y: parent.height * 0.42; size: 32; driftRange: 12; driftDuration: 8100; driftDelay: 1400 }
                Star { x: parent.width * 0.24; y: parent.height * 0.78; size: 38; driftRange: 14; driftDuration: 7600; driftDelay: 2200 }
                Star { x: parent.width * 0.38; y: parent.height * 0.06; size: 24; driftRange: 9; driftDuration: 6600; driftDelay: 500 }
                Star { x: parent.width * 0.44; y: parent.height * 0.48; size: 34; driftRange: 13; driftDuration: 8600; driftDelay: 1800 }
                Star { x: parent.width * 0.37; y: parent.height * 0.92; size: 20; driftRange: 8; driftDuration: 6200; driftDelay: 900 }
                Star { x: parent.width * 0.60; y: parent.height * 0.20; size: 44; driftRange: 15; driftDuration: 8800; driftDelay: 300 }
                Star { x: parent.width * 0.68; y: parent.height * 0.60; size: 56; driftRange: 18; driftDuration: 9600; driftDelay: 1100 }
                Star { x: parent.width * 0.58; y: parent.height * 0.72; size: 26; driftRange: 10; driftDuration: 7000; driftDelay: 2600 }
                Star { x: parent.width * 0.86; y: parent.height * 0.10; size: 30; driftRange: 11; driftDuration: 7400; driftDelay: 1600 }
                Star { x: parent.width * 0.90; y: parent.height * 0.50; size: 50; driftRange: 17; driftDuration: 9200; driftDelay: 700 }
                Star { x: parent.width * 0.94; y: parent.height * 0.85; size: 34; driftRange: 12; driftDuration: 8000; driftDelay: 2000 }
                Star { x: parent.width * 0.78; y: parent.height * 0.90; size: 22; driftRange: 8; driftDuration: 6400; driftDelay: 400 }
            }

            Image {
                anchors.fill: parent
                visible: window.backgroundStyle === "holo"
                source: "qrc:/qt/qml/StreamSoftGui/qml/assets/bg-holo.png"
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                smooth: true
            }

            Image {
                anchors.fill: parent
                visible: window.backgroundStyle === "chess"
                source: "qrc:/qt/qml/StreamSoftGui/qml/assets/bg-chess-queen.png"
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                smooth: true
            }

            Image {
                readonly property bool valid: window.backgroundStyle === "custom"
                    && window.activeCustomIndex >= 0 && window.activeCustomIndex < window.customThemes.length
                anchors.fill: parent
                visible: valid
                source: valid ? window.customThemes[window.activeCustomIndex].path : ""
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                smooth: true
            }
        }
    }

    function loadSettings() {
        api.get("/api/settings", function (ok, data) {
            if (!ok) return
            overlayPage.applySettings(data)
            ttsPage.applySettings(data)
            mutedPage.applySettings(data)
            rvcPage.applySettings(data)
        })
    }

    Component.onCompleted: {
        loadCustomThemes()
        loadSettings()
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        GlassSurface {
            Layout.preferredWidth: 232
            Layout.fillHeight: true
            radiusPx: 0
            pad: 14
            refractPx: 12

            Rectangle {
                anchors.right: parent.right
                width: 1
                height: parent.height
                color: Theme.hairline
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 22
                spacing: 3

                ColumnLayout {
                    spacing: 2
                    Layout.bottomMargin: 20
                    Text { text: "Stream Panel"; color: Theme.text; font.pixelSize: 14; font.bold: true }
                    Text { text: "by PRISSETIK"; color: Theme.textFaint; font.pixelSize: 11 }
                }

                Repeater {
                    model: ["Оверлей", "Подключения", "Озвучка", "Алерты и медиа", "Команды чата", "Замьюченные", "Клон голоса", "Настройки"]
                    delegate: NavButton {
                        required property string modelData
                        required property int index
                        text: modelData
                        active: pageStack.currentIndex === index
                        onClicked: pageStack.currentIndex = index
                    }
                }

                Item { Layout.fillHeight: true }

                Text {
                    text: "Изменения сохраняются автоматически."
                    color: Theme.textFaint
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }
        }

        ScrollView {
            id: scroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            // StackLayout.implicitHeight is the max across ALL of its
            // children, not just the current one — binding contentHeight
            // to it left a huge scrollable void below short pages sized
            // for whichever page is tallest. Read the current page's own
            // implicitHeight directly instead.
            contentHeight: pageStack.y + currentPageHeight + 40
            clip: true

            readonly property real currentPageHeight: {
                var item = pageStack.currentIndex >= 0 ? pageStack.itemAt(pageStack.currentIndex) : null
                return item ? item.implicitHeight : 0
            }

            StackLayout {
                id: pageStack
                width: Math.min(760, parent.width - 88)
                x: 44
                y: 40
                currentIndex: 0

                OverlayPage { id: overlayPage; width: pageStack.width }
                ConnectionsPage { id: connectionsPage; width: pageStack.width }
                TtsPage { id: ttsPage; width: pageStack.width }
                AlertsPage { id: alertsPage; width: pageStack.width }
                CommandsPage { id: commandsPage; width: pageStack.width }
                MutedPage { id: mutedPage; width: pageStack.width }
                RvcPage { id: rvcPage; width: pageStack.width }
                SettingsPage { id: settingsPage; width: pageStack.width }
            }
        }
    }

    Item {
        id: toast
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.topMargin: visible ? 20 : -4
        anchors.rightMargin: 20
        z: 1000
        width: toastRow.implicitWidth + 30
        height: 42
        opacity: 0
        visible: opacity > 0.01

        property string message: "Сохранено"

        Behavior on anchors.topMargin { NumberAnimation { duration: 240; easing.type: Easing.OutCubic } }
        Behavior on opacity { NumberAnimation { duration: 240; easing.type: Easing.OutCubic } }

        function show(msg) {
            message = msg
            opacity = 1
            hideTimer.restart()
        }

        Timer { id: hideTimer; interval: 1800; onTriggered: toast.opacity = 0 }

        GlassSurface {
            anchors.fill: parent
            radiusPx: height / 2
            pad: 10
            refractPx: 8
            specularStrength: 0.5
            tintColor: Qt.rgba(0.06, 0.06, 0.07, 0.7)
            rimColor: Theme.glassBorderBright
        }

        RowLayout {
            id: toastRow
            anchors.centerIn: parent
            spacing: 8

            Text {
                text: toast.message
                color: Theme.text
                font.pixelSize: Theme.fontMd
                font.weight: Font.DemiBold
            }
        }
    }

    // Twitch's device-code flow needs the user to open a page and type in
    // a one-time code — the worker thread that runs it has nowhere to put
    // that up (see ScopedAuthPrompt in twitch_auth.hpp), so it publishes
    // it over REST and this polls + displays it. Global (not scoped to
    // ConnectionsPage) since the flow can kick off at startup from
    // previously-saved credentials, before the user ever opens that page.
    property bool twitchAuthPending: false
    property string twitchAuthUrl: ""
    property string twitchAuthCode: ""
    property string twitchAuthDismissedCode: ""

    // Outcome of the most recent attempt (see run_manual_auth() in
    // twitch_auth.hpp) — without this, the banner above just vanished the
    // moment `pending` went back to false, with no way to tell "connected"
    // from "failed, try again".
    property string twitchAuthResult: ""
    property string twitchAuthResultUsername: ""
    property string twitchAuthResultError: ""
    property string twitchAuthResultDismissedKey: ""
    readonly property string twitchAuthResultKey: twitchAuthResult + "|" + twitchAuthResultUsername + "|" + twitchAuthResultError

    Timer {
        interval: 3000
        running: true
        repeat: true
        triggeredOnStart: true
        onTriggered: {
            api.get("/api/twitch/auth-status", function (ok, data) {
                if (!ok || !data) return
                window.twitchAuthPending = !!data.pending
                window.twitchAuthUrl = data.verification_uri || ""
                window.twitchAuthCode = data.user_code || ""
                window.twitchAuthResult = data.last_result || ""
                window.twitchAuthResultUsername = data.last_username || ""
                window.twitchAuthResultError = data.last_error || ""
                if (window.twitchAuthResult === "") resultAutoHide.stop()
                else if (window.twitchAuthResultKey !== window.twitchAuthResultDismissedKey && !resultAutoHide.running) resultAutoHide.restart()
            })
        }
    }

    // Success/failure toast fades on its own after a few seconds — dismissing
    // it just records the key so it doesn't pop back on the next poll tick.
    Timer {
        id: resultAutoHide
        interval: 6000
        onTriggered: window.twitchAuthResultDismissedKey = window.twitchAuthResultKey
    }

    TextEdit {
        id: codeCopyHelper
        visible: false
    }

    Item {
        id: twitchBanner
        readonly property bool showAuth: window.twitchAuthPending
            && window.twitchAuthCode.length > 0
            && window.twitchAuthCode !== window.twitchAuthDismissedCode
        readonly property bool showResult: !window.twitchAuthPending
            && window.twitchAuthResult !== ""
            && window.twitchAuthResultKey !== window.twitchAuthResultDismissedKey
        readonly property bool shouldShow: showAuth || showResult
        readonly property bool isSuccess: showResult && window.twitchAuthResult === "success"

        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin: shouldShow ? 20 : -10
        z: 999
        width: bannerRow.implicitWidth + 32
        height: 48
        opacity: shouldShow ? 1 : 0
        visible: opacity > 0.01

        Behavior on anchors.topMargin { NumberAnimation { duration: 240; easing.type: Easing.OutCubic } }
        Behavior on opacity { NumberAnimation { duration: 240; easing.type: Easing.OutCubic } }

        GlassSurface {
            anchors.fill: parent
            radiusPx: height / 2
            pad: 12
            refractPx: 10
            specularStrength: 0.55
            tintColor: twitchBanner.showResult
                ? (twitchBanner.isSuccess ? Qt.rgba(0.11, 0.35, 0.2, 0.6) : Qt.rgba(0.4, 0.1, 0.1, 0.6))
                : Qt.rgba(0.45, 0.16, 0.04, 0.6)
            rimColor: twitchBanner.showResult
                ? (twitchBanner.isSuccess ? Qt.rgba(0.55, 1, 0.75, 0.65) : Qt.rgba(1, 0.5, 0.5, 0.65))
                : Qt.rgba(1, 0.72, 0.35, 0.65)
            Behavior on tintColor { ColorAnimation { duration: 200 } }
            Behavior on rimColor { ColorAnimation { duration: 200 } }
        }

        RowLayout {
            id: bannerRow
            anchors.centerIn: parent
            spacing: 10
            visible: twitchBanner.showAuth

            Text {
                text: "Авторизуй Twitch:"
                color: Theme.text
                font.pixelSize: Theme.fontMd
                font.weight: Font.DemiBold
            }
            Text {
                text: window.twitchAuthCode
                color: "#ffd27a"
                font.pixelSize: Theme.fontLg
                font.weight: Font.Bold
            }
            PillButton {
                text: "Скопировать код"
                implicitHeight: 30
                onClicked: {
                    codeCopyHelper.text = window.twitchAuthCode
                    codeCopyHelper.selectAll()
                    codeCopyHelper.copy()
                }
            }
            PillButton {
                text: "Открыть " + (window.twitchAuthUrl || "twitch.tv/activate")
                implicitHeight: 30
                onClicked: Qt.openUrlExternally(window.twitchAuthUrl)
            }
            PillButton {
                danger: true
                text: "✕"
                implicitHeight: 30
                implicitWidth: 36
                onClicked: window.twitchAuthDismissedCode = window.twitchAuthCode
            }
        }

        RowLayout {
            id: resultRow
            anchors.centerIn: parent
            spacing: 10
            visible: twitchBanner.showResult

            Text {
                text: twitchBanner.isSuccess ? "✓" : "!"
                color: twitchBanner.isSuccess ? Theme.good : Theme.bad
                font.pixelSize: Theme.fontLg
                font.weight: Font.Bold
            }
            Text {
                text: twitchBanner.isSuccess
                    ? "Twitch подключён: " + window.twitchAuthResultUsername
                    : "Ошибка авторизации Twitch: " + window.twitchAuthResultError
                color: Theme.text
                font.pixelSize: Theme.fontMd
                font.weight: Font.DemiBold
                elide: Text.ElideRight
                Layout.maximumWidth: 480
            }
            PillButton {
                danger: true
                text: "✕"
                implicitHeight: 30
                implicitWidth: 36
                onClicked: window.twitchAuthResultDismissedKey = window.twitchAuthResultKey
            }
        }
    }

    property Item backdropItem: backdrop

    // Referenced (only for its change notifications) inside each
    // GlassCard's sourceRect binding: mapToItem() alone registers no QML
    // dependencies, so without this the backdrop crop behind a card would
    // go stale the moment the page scrolls.
    property real backdropScrollY: scroll.contentItem ? scroll.contentItem.contentY : 0

    // Same idea, but for the case scrolling doesn't cover: a card shifting
    // position because a SIBLING elsewhere on the page changed size (e.g.
    // an expandable "Как настроить?" guide opening pushes everything below
    // it down). There's no single property to depend on for "some
    // ancestor's layout changed" in general, so this just ticks on a timer
    // and every GlassSurface re-samples its backdrop crop shortly after —
    // cheap, and a ~130ms lag on a one-off expand/collapse is imperceptible.
    property real layoutTick
    NumberAnimation on layoutTick {
        from: 0; to: 1
        duration: 130
        loops: Animation.Infinite
    }
}
