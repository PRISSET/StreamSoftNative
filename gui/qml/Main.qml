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
    readonly property real blurRadius: blurBackgroundEnabled ? (blurAmount / 100.0) * 6.0 : 0.0

    property bool sidebarCollapsed: false
    property bool flatTheme: true
    property int hoveredNavIndex: -1

    Binding { target: Theme; property: "flatMode"; value: window.flatTheme }

    Settings {
        property alias backgroundStyle: window.backgroundStyle
        property alias activeCustomIndex: window.activeCustomIndex
        property alias customThemesData: window.customThemesData
        property alias blurBackgroundEnabled: window.blurBackgroundEnabled
        property alias blurAmount: window.blurAmount
        property alias sidebarCollapsed: window.sidebarCollapsed
        property alias flatTheme: window.flatTheme
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
            color: window.flatTheme ? Theme.bg : "#000000"
        }

        Item {
            id: wallpaper
            anchors.fill: parent
            visible: !window.flatTheme
            layer.enabled: !window.flatTheme && window.blurBackgroundEnabled && window.blurAmount > 0
            layer.smooth: true
            layer.effect: MultiEffect {
                blurEnabled: true
                blur: 1.0
                blurMax: (window.blurAmount / 100.0) * 48.0
                autoPaddingEnabled: false
            }

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
            voicePage.applySettings(data)
            mutedPage.applySettings(data)
            musicPage.applySettings(data)
            cs2Page.applySettings(data)
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
            id: sidebar
            Layout.preferredWidth: window.sidebarCollapsed ? 76 : 232
            Layout.fillHeight: true
            radiusPx: 0
            pad: 14
            refractPx: 12
            rimColor: "#00ffffff"

            Behavior on Layout.preferredWidth { NumberAnimation { duration: Theme.motionMed; easing.type: Theme.motionEasing } }

            Rectangle {
                anchors.right: parent.right
                width: 1
                height: parent.height
                color: Theme.hairline
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: window.sidebarCollapsed ? 12 : 22
                spacing: 3

                Behavior on anchors.margins { NumberAnimation { duration: Theme.motionMed; easing.type: Theme.motionEasing } }

                Item {
                    Layout.fillWidth: true
                    Layout.bottomMargin: 20
                    implicitHeight: 34

                    ColumnLayout {
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 2
                        opacity: window.sidebarCollapsed ? 0 : 1
                        visible: opacity > 0.01
                        Behavior on opacity { NumberAnimation { duration: Theme.motionFast } }

                        Text { text: "Stream Panel"; color: Theme.text; font.pixelSize: 14; font.bold: true }
                        Text { text: "by PRISSETIK"; color: Theme.textFaint; font.pixelSize: 11 }
                    }

                    Item {
                        id: collapseToggle
                        width: 28
                        height: 28
                        anchors.verticalCenter: parent.verticalCenter
                        x: window.sidebarCollapsed ? (parent.width - width) / 2 : parent.width - width
                        Behavior on x { NumberAnimation { duration: Theme.motionMed; easing.type: Theme.motionEasing } }

                        GlassSurface {
                            anchors.fill: parent
                            radiusPx: height / 2
                            pad: 6
                            refractPx: 6
                            specularStrength: 0.5
                            flatShadow: false
                            tintColor: collapseHover.hovered ? Theme.glassFillHover : Theme.glassFill
                            rimColor: collapseHover.hovered ? Theme.fieldBorderHover : Theme.glassBorder
                            Behavior on tintColor { ColorAnimation { duration: Theme.motionFast } }
                            Behavior on rimColor { ColorAnimation { duration: Theme.motionFast } }
                        }

                        HoverHandler { id: collapseHover }
                        TapHandler {
                            onTapped: window.sidebarCollapsed = !window.sidebarCollapsed
                            cursorShape: Qt.PointingHandCursor
                        }

                        Text {
                            anchors.centerIn: parent
                            text: window.sidebarCollapsed ? "›" : "‹"
                            color: Theme.text
                            font.pixelSize: Theme.fontMd
                            font.bold: true
                        }
                    }
                }

                Item {
                    id: navListArea
                    Layout.fillWidth: true
                    implicitHeight: navColumn.implicitHeight

                    HoverHandler {
                        onHoveredChanged: if (!hovered) window.hoveredNavIndex = -1
                    }

                    ColumnLayout {
                        id: navColumn
                        width: parent.width
                        spacing: 3

                        Repeater {
                            model: [
                                { text: "Оверлей", icon: "overlay" },
                                { text: "Подключения", icon: "connections" },
                                { text: "Голос", icon: "voice" },
                                { text: "Алерты и медиа", icon: "alerts" },
                                { text: "Команды чата", icon: "commands" },
                                { text: "Опрос", icon: "poll" },
                                { text: "Музыка", icon: "music" },
                                { text: "Гифки", icon: "gifs" },
                                { text: "Соцсети", icon: "social" },
                                { text: "Faceit", icon: "faceit" },
                                { text: "CS2", icon: "cs2" },
                                { text: "Замьюченные", icon: "muted" },
                                { text: "Настройки", icon: "settings" },
                                { text: "Обновления", icon: "updates" }
                            ]
                            delegate: NavButton {
                                required property var modelData
                                required property int index
                                text: modelData.text
                                iconName: modelData.icon
                                active: pageStack.currentIndex === index
                                collapsed: window.sidebarCollapsed
                                hovered: window.hoveredNavIndex === index
                                onHoverEntered: window.hoveredNavIndex = index
                                onClicked: pageStack.currentIndex = index
                            }
                        }
                    }
                }

                Item { Layout.fillHeight: true }

                Text {
                    text: "Изменения сохраняются автоматически."
                    color: Theme.textFaint
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                    opacity: window.sidebarCollapsed ? 0 : 1
                    visible: opacity > 0.01
                    Behavior on opacity { NumberAnimation { duration: Theme.motionFast } }
                }
            }
        }

        ScrollView {
            id: scroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
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
                VoicePage { id: voicePage; width: pageStack.width }
                AlertsPage { id: alertsPage; width: pageStack.width }
                CommandsPage { id: commandsPage; width: pageStack.width }
                PollPage { id: pollPage; width: pageStack.width }
                MusicPage { id: musicPage; width: pageStack.width }
                GifsPage { id: gifsPage; width: pageStack.width }
                SocialPage { id: socialPage; width: pageStack.width }
                FaceitPage { id: faceitPage; width: pageStack.width }
                Cs2Page { id: cs2Page; width: pageStack.width }
                MutedPage { id: mutedPage; width: pageStack.width }
                SettingsPage { id: settingsPage; width: pageStack.width }
                UpdatesPage { id: updatesPage; width: pageStack.width }
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

    property bool twitchAuthPending: false
    property string twitchAuthUrl: ""
    property string twitchAuthCode: ""
    property string twitchAuthDismissedCode: ""

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

    property real backdropScrollY: scroll.contentItem ? scroll.contentItem.contentY : 0

    property real layoutTick
    NumberAnimation on layoutTick {
        from: 0; to: 1
        duration: 130
        loops: Animation.Infinite
    }
}
