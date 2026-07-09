#include "api_client.hpp"
#include "core_app.hpp"
#include "overlay_ws_client.hpp"

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
#endif

int main(int argc, char* argv[]) {
    // Must be set before QGuiApplication/engine exist. Native styles (macOS,
    // Windows, WindowsVista) refuse to let QML override a control's
    // background/handle — Basic is the only style that honors full custom
    // styling, same reasoning as forcing Fusion for the old Qt Widgets GUI.
    QQuickStyle::setStyle("Basic");

    // The whole background service (chat workers, TTS, overlay HTTP/WS
    // server) runs on its own thread inside this same process/exe — one
    // program instead of core.exe + gui.exe talking over localhost. The GUI
    // still goes through the same REST API (ApiClient -> 127.0.0.1:8099),
    // just now the server on the other end lives in-process.
    std::thread coreThread([] { streamsoft::run_core(); });
    coreThread.detach();

    QGuiApplication app(argc, argv);
    // Qt.labs.settings' Settings QML type (used to persist the chosen
    // background theme) stores under Software/<Organization>/<Application>
    // in the registry on Windows — without these it falls back to the
    // executable name for both, which still works but is fragile if the
    // exe is ever renamed.
    QGuiApplication::setOrganizationName("PRISSETIK");
    QGuiApplication::setApplicationName("StreamSoft");

    // Every QML Text/Control picks this up as its default family (they
    // only ever set pixelSize/weight, never family) — one registration
    // re-fonts the whole app instead of touching every .qml file. Variable
    // weight axis (Thin..Black) covers font.bold/font.weight without
    // needing separate static-weight files; full Cyrillic coverage
    // verified before bundling, since this app's UI is entirely Russian.
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
    // Match the native title bar to the dark content instead of the jarring
    // light Windows default strip above a black window.
    if (auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first())) {
        BOOL useDarkMode = TRUE;
        HWND hwnd = reinterpret_cast<HWND>(window->winId());
        DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &useDarkMode, sizeof(useDarkMode));
    }
#endif

    return app.exec();
}
