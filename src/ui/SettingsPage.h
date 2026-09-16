#pragma once
#include <QWidget>
#include <QLineEdit>
#include <QSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include "../db/Database.h"

class SettingsPage : public QWidget {
    Q_OBJECT
public:
    explicit SettingsPage(Database *db, QWidget *parent = nullptr);

    void updateServerStatus(bool running, quint16 port);
    // Briefly shows a coloured confirmation line under the server buttons (e.g. after a Restart).
    void flashServerAction(const QString &message, bool ok);

signals:
    void settingsSaved();
    void serverStartRequested();
    void serverStopRequested();
    void serverRestartRequested();

private slots:
    void onSave();

private:
    Database   *m_db;
    QLineEdit  *m_apiUrl;
    QLineEdit  *m_apiKey;
    QLineEdit  *m_institutionId;
    QSpinBox   *m_serverPort;
    QLabel     *m_serverStatusLabel;
    QPushButton *m_btnStart;
    QPushButton *m_btnStop;
    QPushButton *m_btnRestart;
    QCheckBox   *m_autoStart;
    QLabel     *m_actionToast;

    void loadSettings();
    QLabel *styledLabel(const QString &text);
    QLabel *hint(const QString &text);
};
