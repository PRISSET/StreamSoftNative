#include "api_client.hpp"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

ApiClient::ApiClient(QJSEngine* engine, QObject* parent)
    : QObject(parent), engine_(engine), manager_(new QNetworkAccessManager(this)) {}

void ApiClient::finish(QNetworkReply* reply, QJSValue callback, bool notifyOnSuccess) {
    connect(reply, &QNetworkReply::finished, this, [this, reply, callback, notifyOnSuccess]() mutable {
        bool ok = reply->error() == QNetworkReply::NoError;
        QByteArray data = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        reply->deleteLater();
        if (ok && notifyOnSuccess) Q_EMIT mutationSucceeded();
        if (callback.isCallable()) {
            QJSValue jsData = engine_->toScriptValue(doc.isObject() ? doc.object().toVariantMap() : QVariant());
            callback.call({QJSValue(ok), jsData});
        }
    });
}

void ApiClient::get(const QString& path, const QJSValue& callback) {
    QNetworkRequest req{QUrl(baseUrl_ + path)};
    finish(manager_->get(req), callback);
}

void ApiClient::post(const QString& path, const QVariantMap& body, const QJSValue& callback, bool notify) {
    QJsonDocument doc = QJsonDocument::fromVariant(body);
    postRaw(path, doc.toJson(QJsonDocument::Compact), "application/json", callback, notify);
}

void ApiClient::postRaw(const QString& path, const QByteArray& body, const QString& contentType, QJSValue callback,
                        bool notifyOnSuccess) {
    QNetworkRequest req{QUrl(baseUrl_ + path)};
    req.setHeader(QNetworkRequest::ContentTypeHeader, contentType);
    finish(manager_->post(req, body), callback, notifyOnSuccess);
}

void ApiClient::uploadFile(const QString& path, const QUrl& fileUrl, const QString& contentType,
                           const QJSValue& callback) {
    QFile file(fileUrl.toLocalFile());
    if (!file.open(QIODevice::ReadOnly)) {
        if (callback.isCallable()) {
            QJSValue jsData = engine_->toScriptValue(QVariant());
            const_cast<QJSValue&>(callback).call({QJSValue(false), jsData});
        }
        return;
    }
    postRaw(path, file.readAll(), contentType, callback);
}

void ApiClient::del(const QString& path, const QJSValue& callback) {
    QNetworkRequest req{QUrl(baseUrl_ + path)};
    finish(manager_->deleteResource(req), callback, true);
}
