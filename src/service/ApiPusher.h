#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include <functional>
#include "../db/Database.h"
#include "../model/BiometricTemplate.h"

class ApiPusher : public QObject {
    Q_OBJECT
public:
    explicit ApiPusher(Database *db, QObject *parent = nullptr);

    void setConfig(const QString &apiUrl, const QString &apiKey, long long institutionId);
    void pushNow();
    bool isOnline() const { return m_isOnline; }

    // Push biometric templates from a device to the API server.
    void pushBiometricTemplates(const QString &deviceSerial,
                                const QString &deviceName,
                                const QList<BiometricTemplate> &templates);

    // Fetch biometric templates for a device from the API server.
    // Invokes callback(templates) when complete; empty list on error.
    void fetchBiometricTemplates(const QString &deviceSerial,
                                 std::function<void(QList<BiometricTemplate>)> callback);

signals:
    void statusChanged(bool online, int pendingCount);
    void recordsSynced(int count);
    void logMessage(const QString &msg);

private slots:
    void onTimerTick();
    void onNetworkReply(QNetworkReply *reply);
    void onConnectivityCheck(QNetworkReply *reply);

private:
    Database *m_db;
    QNetworkAccessManager *m_nam;
    QTimer *m_timer;
    QString m_apiUrl;
    QString m_apiKey;
    long long m_institutionId = 0;
    bool m_isOnline = false;
    bool m_pushing = false;

    void checkConnectivity();
    void doPush();
    void emitStatus();
};
