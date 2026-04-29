#pragma once
#include <QDialog>
#include <QProgressBar>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QDateTime>
#include <QNetworkAccessManager>
#include <QList>
#include "../model/BiometricTemplate.h"

class BiometricBackupDialog : public QDialog {
    Q_OBJECT
public:
    explicit BiometricBackupDialog(const QString &serial,
                                   const QString &deviceName,
                                   const QString &apiUrl,
                                   const QString &apiKey,
                                   long long      institutionId,
                                   QWidget       *parent = nullptr);

    // Called by MainWindow when the device delivers its templates via ADMS POST.
    void startUpload(const QList<BiometricTemplate> &templates);

signals:
    void backupFinished(const QString &serial, int total, bool success);

private slots:
    void onStatsTimer();

private:
    void buildUi();
    void setStatus(const QString &text, const QString &color = "#374151");
    void doUpload();
    void finishUpload(bool success);
    QString formatDuration(qint64 seconds) const;

    // Config
    QString   m_serial;
    QString   m_deviceName;
    QString   m_apiUrl;
    QString   m_apiKey;
    long long m_institutionId = 0;

    // Upload state
    QList<BiometricTemplate> m_templates;
    int  m_total     = 0;
    int  m_uploaded  = 0;
    bool m_uploading = false;
    bool m_finished  = false;

    // Timing
    QDateTime m_startedAt;
    QDateTime m_uploadStartedAt;
    QTimer   *m_statsTimer = nullptr;

    // Network
    QNetworkAccessManager *m_nam = nullptr;

    // Widgets
    QLabel       *m_lblDeviceInfo = nullptr;
    QLabel       *m_lblStatus     = nullptr;
    QProgressBar *m_progressBar   = nullptr;
    QLabel       *m_lblTotal      = nullptr;
    QLabel       *m_lblUploaded   = nullptr;
    QLabel       *m_lblSpeed      = nullptr;
    QLabel       *m_lblStartedAt  = nullptr;
    QLabel       *m_lblElapsed    = nullptr;
    QLabel       *m_lblEta        = nullptr;
    QPushButton  *m_btnClose      = nullptr;
};
