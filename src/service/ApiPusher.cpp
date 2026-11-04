#include "ApiPusher.h"
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <QDebug>
#include <QMessageAuthenticationCode>
#include <QCryptographicHash>
#include <QDateTime>
#include <memory>

// The app-signing secret is baked in at build time (-DBIOSYNC_APP_SECRET=...), never stored in
// config or the repo. Empty in an unsigned/dev build.
#ifndef BIOSYNC_APP_SECRET
#define BIOSYNC_APP_SECRET ""
#endif

// Signs a request: X-BioSync-Signature = HMAC-SHA256(secret, "<timestamp>.<body>"), X-BioSync-Timestamp.
static void signRequest(QNetworkRequest &req, const QByteArray &body) {
    static const QByteArray secret = QByteArrayLiteral(BIOSYNC_APP_SECRET);
    if (secret.isEmpty()) return;
    const QByteArray ts = QByteArray::number(QDateTime::currentMSecsSinceEpoch());
    const QByteArray sig = QMessageAuthenticationCode::hash(
        ts + "." + body, secret, QCryptographicHash::Sha256).toHex();
    req.setRawHeader("X-BioSync-Timestamp", ts);
    req.setRawHeader("X-BioSync-Signature", sig);
}

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
    if (m_autoPush) checkConnectivity();
}

void ApiPusher::onTimerTick() {
    if (m_autoPush && !m_apiUrl.isEmpty() && !m_apiKey.isEmpty() && m_institutionId > 0) {
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

    if (m_isOnline && !m_pushing && m_autoPush) {
        doPush();
    }
}

// A push that fails is NOT a lost punch: records stay 'pending' and are retried with backoff
// (15s → 30s → 1m → … → 5m) until RiTEMS accepts them. Only a request RiTEMS rejects as invalid
// (HTTP 400/422 — retrying the same data can never succeed) is marked 'failed'. Re-sending is
// safe: RiTEMS de-duplicates punches.
static constexpr int BATCH_SIZE      = 500;     // records per request
static constexpr int CYCLE_LIMIT     = 5000;    // records per push cycle
static constexpr qint64 MAX_BACKOFF_MS = 5 * 60 * 1000;

static bool isPermanentRejection(int httpStatus) {
    return httpStatus == 400 || httpStatus == 422;
}

void ApiPusher::doPush() {
    if (m_pushing || m_apiUrl.isEmpty() || m_apiKey.isEmpty() || m_institutionId <= 0) return;

    // Back off after a failed attempt instead of hammering a server that is down.
    if (QDateTime::currentMSecsSinceEpoch() < m_nextAttemptMs) return;

    QList<AttendanceRecord> pending = m_db->getPendingRecords(CYCLE_LIMIT);
    if (pending.isEmpty()) return;

    m_pushing = true;

    // Group by device serial, then split into batches so one huge backlog never becomes one
    // huge request that times out.
    QHash<QString, QList<AttendanceRecord>> byDevice;
    for (const auto &r : pending)
        byDevice[r.deviceSerial].append(r);

    QList<QList<AttendanceRecord>> batches;
    for (auto it = byDevice.cbegin(); it != byDevice.cend(); ++it)
        for (qsizetype i = 0; i < it.value().size(); i += BATCH_SIZE)
            batches.append(it.value().mid(i, BATCH_SIZE));

    struct Cycle { int inFlight = 0; bool anyTransientFailure = false; };
    auto cycle = std::make_shared<Cycle>();
    cycle->inFlight = batches.size();
    const bool mayHaveMore = pending.size() >= CYCLE_LIMIT;

    for (const auto &batch : batches) {
        QJsonArray recordsArr;
        for (const auto &r : batch) {
            QJsonObject obj;
            obj["userPin"]    = r.userPin;
            obj["punchTime"]  = r.punchTime.toString("yyyy-MM-dd HH:mm:ss");
            obj["verifyType"] = r.verifyType;
            obj["punchStatus"]= r.punchStatus;
            if (!r.workCode.isEmpty()) obj["workCode"] = r.workCode;
            if (r.answered) {
                // Interactive answer given on the device: RiTEMS classifies the punch with it and
                // shows no popup. (An older RiTEMS ignores the field and shows the popup as before.)
                QJsonObject answer;
                if (!r.answerDirection.isEmpty()) answer["direction"] = r.answerDirection;
                if (!r.answerExitType.isEmpty())  answer["exitType"]  = r.answerExitType;
                if (!r.answerReason.isEmpty())    answer["reason"]    = r.answerReason;
                answer["mistake"] = r.answerMistake;
                obj["answer"] = answer;
            }
            recordsArr.append(obj);
        }

        QJsonObject payload;
        payload["deviceSerial"]  = batch.first().deviceSerial;
        if (!batch.first().deviceName.isEmpty())
            payload["deviceName"] = batch.first().deviceName;
        payload["institutionId"] = static_cast<qint64>(m_institutionId);
        payload["records"]       = recordsArr;

        const QByteArray body = QJsonDocument(payload).toJson(QJsonDocument::Compact);

        QNetworkRequest req(QUrl(m_apiUrl + "/api/v1/biosync/push"));
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        req.setRawHeader("X-BioSync-Key", m_apiKey.toUtf8());
        // Prove this push came from a genuine BioSync build: HMAC-SHA256 over "<timestamp>.<body>"
        // with the app secret baked into this binary (never in config or on the wire).
        signRequest(req, body);
        req.setTransferTimeout(60000);

        QList<int> batchIds;
        for (const auto &r : batch) batchIds.append(r.id);

        QNetworkReply *reply = m_nam->post(req, body);

        connect(reply, &QNetworkReply::finished, this, [this, reply, batchIds, cycle, mayHaveMore]() {
            const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const bool ok = (reply->error() == QNetworkReply::NoError && httpStatus == 200);
            QString error = reply->errorString();
            if (!ok) {
                // Prefer RiTEMS' own message (e.g. "Invalid BioSync API key.") when it sent one.
                const QString serverMsg = QJsonDocument::fromJson(reply->readAll()).object()
                                              .value("message").toString();
                if (!serverMsg.isEmpty()) error = serverMsg;
            }

            if (ok) {
                for (int id : batchIds) m_db->updateSyncStatus(id, SyncStatus::Synced);
                emit recordsSynced(batchIds.size());
                emit logMessage(QString("Synced %1 attendance record(s).").arg(batchIds.size()));
            } else if (isPermanentRejection(httpStatus)) {
                for (int id : batchIds) m_db->updateSyncStatus(id, SyncStatus::Failed, error);
                emit logMessage(QString("RiTEMS rejected %1 record(s) as invalid (HTTP %2): %3 — not retried.")
                                    .arg(batchIds.size()).arg(httpStatus).arg(error));
            } else {
                for (int id : batchIds) m_db->recordPushError(id, error);
                cycle->anyTransientFailure = true;
                emit logMessage(QString("Could not sync %1 record(s) (HTTP %2): %3 — kept pending, will retry.")
                                    .arg(batchIds.size()).arg(httpStatus).arg(error));
            }
            reply->deleteLater();

            if (--cycle->inFlight == 0) {
                m_pushing = false;
                if (cycle->anyTransientFailure) {
                    m_backoffMs = m_backoffMs == 0 ? 15000 : qMin(m_backoffMs * 2, MAX_BACKOFF_MS);
                    m_nextAttemptMs = QDateTime::currentMSecsSinceEpoch() + m_backoffMs;
                    emit logMessage(QString("Next sync attempt in %1s.").arg(m_backoffMs / 1000));
                } else {
                    m_backoffMs = 0;
                    m_nextAttemptMs = 0;
                    if (mayHaveMore) QTimer::singleShot(0, this, &ApiPusher::doPush);   // drain backlog
                }
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
                                        const QList<BiometricTemplate> &templates,
                                        std::function<void(bool, QString)> done)
{
    if (m_apiUrl.isEmpty() || m_apiKey.isEmpty() || m_institutionId <= 0) {
        if (done) done(false, "API is not configured (server URL / key / institution).");
        return;
    }

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
    connect(reply, &QNetworkReply::finished, this, [this, reply, deviceSerial, count, done]() {
        int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        bool ok = (reply->error() == QNetworkReply::NoError && httpStatus == 200);
        QString msg;
        if (ok) {
            msg = QString("Backed up %1 biometric template(s) for device %2.").arg(count).arg(deviceSerial);
        } else {
            msg = QString("Failed to back up templates for %1: %2 (HTTP %3)")
                      .arg(deviceSerial, reply->errorString()).arg(httpStatus);
        }
        emit logMessage(msg);
        reply->deleteLater();
        if (done) done(ok, msg);
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

void ApiPusher::fetchPunchContext(const QString &deviceSerial, const QString &userPin,
                                  const QDateTime &punchTime,
                                  std::function<void(bool, QJsonObject, QString)> done)
{
    if (m_apiUrl.isEmpty() || m_apiKey.isEmpty() || m_institutionId <= 0) {
        done(false, {}, "BioSync is not connected to RiTEMS (server URL / API key / institution not set).");
        return;
    }

    QJsonObject payload;
    payload["deviceSerial"]  = deviceSerial;
    payload["institutionId"] = static_cast<qint64>(m_institutionId);
    payload["userPin"]       = userPin;
    payload["punchTime"]     = punchTime.toString("yyyy-MM-dd HH:mm:ss");
    const QByteArray body = QJsonDocument(payload).toJson(QJsonDocument::Compact);

    QNetworkRequest req(QUrl(m_apiUrl + "/api/v1/biosync/punch-context"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("X-BioSync-Key", m_apiKey.toUtf8());
    signRequest(req, body);
    req.setTransferTimeout(PUNCH_CONTEXT_TIMEOUT_MS);   // the person is waiting at the device

    QNetworkReply *reply = m_nam->post(req, body);
    connect(reply, &QNetworkReply::finished, this, [reply, done]() {
        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError && httpStatus == 200 && root.value("data").isObject()) {
            done(true, root.value("data").toObject(), {});
        } else if (httpStatus == 404) {
            done(false, {}, "This RiTEMS server does not support answering on the device yet.");
        } else {
            const QString msg = root.value("message").toString();
            done(false, {}, msg.isEmpty() ? QString("HTTP %1: %2").arg(httpStatus).arg(reply->errorString()) : msg);
        }
    });
}
