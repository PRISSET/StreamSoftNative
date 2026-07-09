#include "overlay_ws_client.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>

OverlayWsClient::OverlayWsClient(QObject* parent) : QObject(parent) {}

OverlayWsClient::~OverlayWsClient() { ws_.stop(); }

void OverlayWsClient::start() {
    ws_.setUrl("ws://127.0.0.1:8099/ws");

    ws_.setOnMessageCallback([this](const ix::WebSocketMessagePtr& msg) {
        // Runs on ixwebsocket's own thread — QMetaObject::invokeMethod with
        // QueuedConnection (the default when emitting to a receiver that
        // lives on a different thread, which this QObject does: it's
        // created on the Qt/GUI thread) marshals the Q_EMIT back safely.
        if (msg->type == ix::WebSocketMessageType::Open) {
            QMetaObject::invokeMethod(this, [this] { Q_EMIT connectionStateChanged(true); }, Qt::QueuedConnection);
            return;
        }
        if (msg->type == ix::WebSocketMessageType::Close || msg->type == ix::WebSocketMessageType::Error) {
            QMetaObject::invokeMethod(this, [this] { Q_EMIT connectionStateChanged(false); }, Qt::QueuedConnection);
            return;
        }
        if (msg->type != ix::WebSocketMessageType::Message) return;

        QByteArray raw = QByteArray::fromStdString(msg->str);
        QJsonDocument doc = QJsonDocument::fromJson(raw);
        if (!doc.isObject()) return;
        QJsonObject obj = doc.object();
        QString type = obj.value("type").toString();

        if (type == "chat") {
            QString platform = obj.value("platform").toString();
            QString author = obj.value("author").toString();
            QString text = obj.value("text").toString();
            QMetaObject::invokeMethod(
                this, [this, platform, author, text] { Q_EMIT chatMessage(platform, author, text); },
                Qt::QueuedConnection);
        } else if (type == "event") {
            QString kind = obj.value("kind").toString();
            QString user = obj.value("user").toString();
            QString detail = obj.value("detail").toString();
            QMetaObject::invokeMethod(
                this, [this, kind, user, detail] { Q_EMIT eventMessage(kind, user, detail); }, Qt::QueuedConnection);
        }
    });

    ws_.start();
}
