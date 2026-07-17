#pragma once

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
    void appNotice(const QString& title, const QString& detail);
    void connectionStateChanged(bool connected);

private:
    ix::WebSocket ws_;
};
