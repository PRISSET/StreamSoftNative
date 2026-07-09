#pragma once

// Bridges the overlay's own /ws feed (the same one OBS Browser Source
// connects to) into QML, so alerts/chat can be previewed natively in the
// app instead of requiring an external browser tab. Qt has no bundled
// WebSocket *client* module in this build (QtWebSockets isn't installed),
// but ixwebsocket already is — it's core's own WS client dependency, reused
// here instead of pulling in another library for the same job.

#include <QObject>
#include <QString>

#include <ixwebsocket/IXWebSocket.h>

class OverlayWsClient : public QObject {
    Q_OBJECT
public:
    explicit OverlayWsClient(QObject* parent = nullptr);
    ~OverlayWsClient() override;

    Q_INVOKABLE void start();

Q_SIGNALS:
    void chatMessage(const QString& platform, const QString& author, const QString& text);
    void eventMessage(const QString& kind, const QString& user, const QString& detail);
    void connectionStateChanged(bool connected);

private:
    ix::WebSocket ws_;
};
