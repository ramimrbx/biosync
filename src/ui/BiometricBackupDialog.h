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

class Database;

// Follows a "DATA QUERY FINGERTMP" device command queued in the shared DB. Whichever BioSync
// process runs the attendance server (background service or window) delivers it, receives the
// templates from the device and uploads them to RiTEMS; this dialog shows that progress.
class BiometricBackupDialog : public QDialog {
    Q_OBJECT
public:
    explicit BiometricBackupDialog(const QString &serial,
                                   const QString &deviceName,
                                   Database      *db,
                                   int            commandId,
                                   QWidget       *parent = nullptr);

signals:
    void backupFinished(const QString &serial, int total, bool success);

private slots:
    void onPoll();

private:
    void buildUi();
    void setStatus(const QString &text, const QString &color = "#374151");
    void finish(bool success, const QString &message);
    QString formatDuration(qint64 seconds) const;

    QString   m_serial;
    QString   m_deviceName;
    Database *m_db;
    int       m_commandId;

    int  m_total    = 0;
    bool m_finished = false;
    QString m_lastStatus;

    QDateTime m_startedAt;
    QTimer   *m_pollTimer = nullptr;

    QLabel       *m_lblDeviceInfo = nullptr;
    QLabel       *m_lblStatus     = nullptr;
    QProgressBar *m_progressBar   = nullptr;
    QLabel       *m_lblTotal      = nullptr;
    QLabel       *m_lblUploaded   = nullptr;
    QLabel       *m_lblStartedAt  = nullptr;
    QLabel       *m_lblElapsed    = nullptr;
    QPushButton  *m_btnClose      = nullptr;
};
