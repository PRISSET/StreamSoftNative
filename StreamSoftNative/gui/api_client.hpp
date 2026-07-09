#pragma once

#include <QJSEngine>
#include <QJSValue>
#include <QNetworkAccessManager>
#include <QObject>
#include <QUrl>

// Thin async JSON client for core's REST API (see
// core/include/overlay_server.hpp) — same contract the old Python
// settings.js and the Widgets-era ApiClient talked to, just invokable
// straight from QML with a JS callback instead of a C++ std::function.
// Exposed to QML as the context property "api" (see main.cpp).
class ApiClient : public QObject {
    Q_OBJECT
public:
    explicit ApiClient(QJSEngine* engine, QObject* parent = nullptr);

    Q_INVOKABLE void get(const QString& path, const QJSValue& callback);
    // `notify` lets call sites that aren't really "saving" anything —
    // test-alert/test-chat triggers, the OBS connect action (which already
    // shows its own inline status text) — opt out of the generic
    // mutationSucceeded "Сохранено" toast. Defaults on for the common case
    // (an actual settings write).
    Q_INVOKABLE void post(const QString& path, const QVariantMap& body, const QJSValue& callback,
                          bool notify = true);
    Q_INVOKABLE void uploadFile(const QString& path, const QUrl& fileUrl, const QString& contentType,
                                const QJSValue& callback);
    Q_INVOKABLE void del(const QString& path, const QJSValue& callback);

Q_SIGNALS:
    // Fired after any successful write (post/uploadFile/del) — never for
    // get, so loading a page's settings doesn't itself pop a "saved"
    // toast. Main.qml listens on this alone to drive the save
    // notification, instead of every page's save() wiring it up itself.
    void mutationSucceeded();

private:
    void postRaw(const QString& path, const QByteArray& body, const QString& contentType, QJSValue callback,
                 bool notifyOnSuccess = true);
    void finish(class QNetworkReply* reply, QJSValue callback, bool notifyOnSuccess = false);

    QJSEngine* engine_;
    QNetworkAccessManager* manager_;
    QString baseUrl_ = "http://127.0.0.1:8099";
};
