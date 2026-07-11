#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "api_client.hpp"
#include "core_app.hpp"
#include "overlay_ws_client.hpp"

#include <QAbstractNativeEventFilter>
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>

#include <thread>

#ifdef Q_OS_WIN
#include <dwmapi.h>

class SessionEndFilter : public QAbstractNativeEventFilter {
public:
    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override {
        if (eventType != "windows_generic_MSG") return false;
        auto* msg = static_cast<MSG*>(message);
        if (msg->message == WM_QUERYENDSESSION || msg->message == WM_ENDSESSION) {
            if (result) *result = TRUE;
            ExitProcess(0);
        }
        return false;
    }
};
#endif

int main(int argc, char* argv[]) {
#ifdef Q_OS_WIN
    CreateMutexW(nullptr, TRUE, L"Global\\StreamSoftNative_SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = FindWindowW(nullptr, L"StreamSoft — настройки");
        if (existing) {
            ShowWindow(existing, SW_RESTORE);
            SetForegroundWindow(existing);
        }
        return 0;
    }

    RegisterApplicationRestart(nullptr, RESTART_NO_CRASH | RESTART_NO_HANG | RESTART_NO_REBOOT);
#endif

    QQuickStyle::setStyle("Basic");

    std::thread coreThread([] { streamsoft::run_core(); });
    coreThread.detach();

    QGuiApplication app(argc, argv);
#ifdef Q_OS_WIN
    SessionEndFilter sessionEndFilter;
    app.installNativeEventFilter(&sessionEndFilter);
#endif
    QGuiApplication::setOrganizationName("PRISSETIK");
    QGuiApplication::setApplicationName("StreamSoft");

    int interFontId = QFontDatabase::addApplicationFont(":/qt/qml/StreamSoftGui/qml/assets/fonts/Inter.ttf");
    QStringList interFamilies = QFontDatabase::applicationFontFamilies(interFontId);
    if (!interFamilies.isEmpty()) {
        QGuiApplication::setFont(QFont(interFamilies.first()));
    }

    QQmlApplicationEngine engine;
    auto* api = new ApiClient(&engine, &engine);
    engine.rootContext()->setContextProperty("api", api);

    auto* overlayEvents = new OverlayWsClient(&engine);
    engine.rootContext()->setContextProperty("overlayEvents", overlayEvents);
    overlayEvents->start();

    engine.loadFromModule("StreamSoftGui", "Main");
    if (engine.rootObjects().isEmpty()) return -1;

#ifdef Q_OS_WIN
    if (auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first())) {
        BOOL useDarkMode = TRUE;
        HWND hwnd = reinterpret_cast<HWND>(window->winId());
        DwmSetWindowAttribute(hwnd, 20 , &useDarkMode, sizeof(useDarkMode));
    }
#endif

    return app.exec();
}
