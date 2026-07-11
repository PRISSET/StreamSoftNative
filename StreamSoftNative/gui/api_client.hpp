#pragma once

#include <QJSEngine>
#include <QJSValue>
#include <QNetworkAccessManager>
#include <QObject>
#include <QUrl>

class ApiClient : public QObject {
    Q_OBJECT
public:
    explicit ApiClient(QJSEngine* engine, QObject* parent = nullptr);

    Q_INVOKABLE void get(const QString& path, const QJSValue& callback);
    Q_INVOKABLE void post(const QString& path, const QVariantMap& body, const QJSValue& callback,
                          bool notify = true);
    Q_INVOKABLE void uploadFile(const QString& path, const QUrl& fileUrl, const QString& contentType,
                                const QJSValue& callback);
    Q_INVOKABLE void del(const QString& path, const QJSValue& callback);

Q_SIGNALS:
    void mutationSucceeded();

private:
    void postRaw(const QString& path, const QByteArray& body, const QString& contentType, QJSValue callback,
                 bool notifyOnSuccess = true);
    void finish(class QNetworkReply* reply, QJSValue callback, bool notifyOnSuccess = false);

    QJSEngine* engine_;
    QNetworkAccessManager* manager_;
    QString baseUrl_ = "http://127.0.0.1:8099";
};
