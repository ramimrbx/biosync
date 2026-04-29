#include "ApiPusher.h"
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <QDebug>

ApiPusher::ApiPusher(Database *db, QObject *parent)
    : QObject(parent), m_db(db)
{
    m_nam = new QNetworkAccessManager(this);

    m_timer = new QTimer(this);
    m_timer->setInterval(15000); // check every 15 seconds
    connect(m_timer, &QTimer::timeout, this, &ApiPusher::onTimerTick);
    m_timer->start();
}

void ApiPusher::setConfig(const QString &apiUrl, const QString &apiKey, long long institutionId) {
    m_apiUrl = apiUrl;
    m_apiKey = apiKey;
    m_institutionId = institutionId;
}

void ApiPusher::pushNow() {
    checkConnectivity();
}

void ApiPusher::onTimerTick() {
    if (!m_apiUrl.isEmpty() && !m_apiKey.isEmpty() && m_institutionId > 0) {
        checkConnectivity();
    }
}

void ApiPusher::checkConnectivity() {
    if (m_apiUrl.isEmpty()) return;

    // Use a HEAD request to check connectivity
    QString checkUrl = m_apiUrl;
    // Remove trailing path to get base url
    QUrl u(m_apiUrl);
    QString pingUrl = QString("%1://%2/actuator/health").arg(u.scheme(), u.host());
    if (u.port() > 0) pingUrl = QString("%1://%2:%3/actuator/health").arg(u.scheme(), u.host()).arg(u.port());

    QNetworkRequest req(pingUrl);
    req.setTransferTimeout(5000);
    QNetworkReply *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onConnectivityCheck(reply);
    });
}

void ApiPusher::onConnectivityCheck(QNetworkReply *reply) {
    bool wasOnline = m_isOnline;
    int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    m_isOnline = (reply->error() == QNetworkReply::NoError || httpStatus > 0);
    reply->deleteLater();

    if (m_isOnline && !wasOnline) {
        emit logMessage("Network connection restored. Pushing pending records...");
    } else if (!m_isOnline && wasOnline) {
        emit logMessage("Network connection lost. Records will be queued.");
    }

    emitStatus();

    if (m_isOnline && !m_pushing) {
        doPush();
    }
}

void ApiPusher::doPush() {
    if (m_pushing || m_apiUrl.isEmpty() || m_apiKey.isEmpty() || m_institutionId <= 0) return;

    // No limit — fetch every pending record
    QList<AttendanceRecord> pending = m_db->getPendingRecords();
    if (pending.isEmpty()) return;

    m_pushing = true;

    // Group by device serial
    QHash<QString, QList<AttendanceRecord>> byDevice;
    for (const auto &r : pending)
        byDevice[r.deviceSerial].append(r);

    int requestsInFlight = byDevice.size();
    auto *counter = new int(requestsInFlight);   // heap-allocated so lambda can share it

    for (auto it = byDevice.cbegin(); it != byDevice.cend(); ++it) {
        QJsonArray recordsArr;
        for (const auto &r : it.value()) {
            QJsonObject obj;
            obj["userPin"]    = r.userPin;
            obj["punchTime"]  = r.punchTime.toString("yyyy-MM-dd HH:mm:ss");
            obj["verifyType"] = r.verifyType;
            obj["punchStatus"]= r.punchStatus;
            if (!r.workCode.isEmpty()) obj["workCode"] = r.workCode;
            recordsArr.append(obj);
        }

        QJsonObject payload;
        payload["deviceSerial"]  = it.key();
        if (!it.value().first().deviceName.isEmpty())
            payload["deviceName"] = it.value().first().deviceName;
        payload["institutionId"] = static_cast<qint64>(m_institutionId);
        payload["records"]       = recordsArr;

        QNetworkRequest req(QUrl(m_apiUrl + "/api/v1/biosync/push"));
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        req.setRawHeader("X-BioSync-Key", m_apiKey.toUtf8());
        req.setTransferTimeout(60000);   // generous timeout for large payloads

        QList<int> batchIds;
        for (const auto &r : it.value()) batchIds.append(r.id);

        QNetworkReply *reply = m_nam->post(req,
            QJsonDocument(payload).toJson(QJsonDocument::Compact));

        connect(reply, &QNetworkReply::finished, this, [this, reply, batchIds, counter]() {
            int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            bool ok = (reply->error() == QNetworkReply::NoError && httpStatus == 200);
            for (int id : batchIds) {
                m_db->updateSyncStatus(id,
                    ok ? SyncStatus::Synced : SyncStatus::Failed,
                    ok ? QString() : reply->errorString());
            }
            if (ok) {
                emit recordsSynced(batchIds.size());
                emit logMessage(QString("Synced %1 attendance record(s).").arg(batchIds.size()));
            } else {
                emit logMessage(QString("Failed to sync %1 record(s): %2 (HTTP %3)")
                    .arg(batchIds.size()).arg(reply->errorString()).arg(httpStatus));
            }
            reply->deleteLater();

            // When last in-flight request finishes, unlock pushing
            if (--(*counter) == 0) {
                delete counter;
                m_pushing = false;
                emitStatus();
            }
        });
    }
}

void ApiPusher::onNetworkReply(QNetworkReply *reply) {
    Q_UNUSED(reply)
}

void ApiPusher::pushBiometricTemplates(const QString &deviceSerial,
                                        const QString &deviceName,
                                        const QList<BiometricTemplate> &templates)
{
    if (m_apiUrl.isEmpty() || m_apiKey.isEmpty() || m_institutionId <= 0) return;

    QJsonArray arr;
    for (const auto &t : templates) {
        QJsonObject obj;
        obj["userPin"]         = t.userPin;
        obj["fingerIndex"]     = t.fingerIndex;
        obj["templateData"]    = t.templateData;
        obj["templateVersion"] = QString();
        arr.append(obj);
    }

    QJsonObject payload;
    payload["deviceSerial"]  = deviceSerial;
    payload["deviceName"]    = deviceName;
    payload["institutionId"] = static_cast<qint64>(m_institutionId);
    payload["templates"]     = arr;

    QNetworkRequest req(QUrl(m_apiUrl + "/api/v1/biosync/templates/backup"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("X-BioSync-Key", m_apiKey.toUtf8());
    req.setTransferTimeout(120000);  // large payload, generous timeout

    int count = templates.size();
    QNetworkReply *reply = m_nam->post(req, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, deviceSerial, count]() {
        int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        bool ok = (reply->error() == QNetworkReply::NoError && httpStatus == 200);
        if (ok)
            emit logMessage(QString("Backed up %1 biometric template(s) for device %2.").arg(count).arg(deviceSerial));
        else
            emit logMessage(QString("Failed to back up templates for %1: %2 (HTTP %3)")
                            .arg(deviceSerial, reply->errorString()).arg(httpStatus));
        reply->deleteLater();
    });
}

void ApiPusher::fetchBiometricTemplates(const QString &deviceSerial,
                                         std::function<void(QList<BiometricTemplate>)> callback)
{
    if (m_apiUrl.isEmpty() || m_apiKey.isEmpty()) {
        callback({});
        return;
    }

    QNetworkRequest req(QUrl(m_apiUrl + "/api/v1/biosync/templates/device/" + deviceSerial));
    req.setRawHeader("X-BioSync-Key", m_apiKey.toUtf8());
    req.setTransferTimeout(10000);

    QNetworkReply *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, deviceSerial, callback]() {
        QList<BiometricTemplate> templates;
        int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() == QNetworkReply::NoError && httpStatus == 200) {
            QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            QJsonArray data = doc.object()["data"].toArray();
            for (const QJsonValue &v : data) {
                QJsonObject obj = v.toObject();
                BiometricTemplate t;
                t.userPin      = obj["userPin"].toString();
                t.fingerIndex  = obj["fingerIndex"].toString().toInt();  // enum name → need mapping
                t.templateData = obj["templateData"].toString();
                // fingerIndex comes back as enum name like "RIGHT_INDEX" — map to int
                static const QHash<QString,int> fingerMap = {
                    {"RIGHT_THUMB",0},{"RIGHT_INDEX",1},{"RIGHT_MIDDLE",2},
                    {"RIGHT_RING",3},{"RIGHT_PINKY",4},{"LEFT_THUMB",5},
                    {"LEFT_INDEX",6},{"LEFT_MIDDLE",7},{"LEFT_RING",8},
                    {"LEFT_PINKY",9},{"FACE",10}
                };
                t.fingerIndex = fingerMap.value(obj["fingerIndex"].toString(), 0);
                t.valid = true;
                if (!t.userPin.isEmpty() && !t.templateData.isEmpty())
                    templates.append(t);
            }
            emit logMessage(QString("Fetched %1 biometric template(s) for device %2.")
                            .arg(templates.size()).arg(deviceSerial));
        } else {
            emit logMessage(QString("Failed to fetch templates for %1: %2 (HTTP %3)")
                            .arg(deviceSerial, reply->errorString()).arg(httpStatus));
        }
        reply->deleteLater();
        callback(templates);
    });
}

void ApiPusher::emitStatus() {
    emit statusChanged(m_isOnline, m_db->getPendingCount());
}
