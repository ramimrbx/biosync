#pragma once
#include <QDialog>
#include <QTableWidget>
#include <QPushButton>
#include <QLabel>
#include <QProgressBar>
#include <QSet>
#include "../service/DeviceDiscovery.h"
#include "../model/Device.h"

class DeviceDiscoveryDialog : public QDialog {
    Q_OBJECT
public:
    explicit DeviceDiscoveryDialog(const QSet<QString> &existingSerials, QWidget *parent = nullptr);
    QList<Device> selectedDevices() const;

private slots:
    void onDeviceFound(DiscoveredDevice device);
    void onScanFinished(int total);
    void onProgress(int percent, const QString &status);
    void onRescan();
    void onAddSelected();

private:
    DeviceDiscovery *m_discovery;
    QTableWidget    *m_table;
    QLabel          *m_emptyLabel;
    QPushButton     *m_btnRescan;
    QPushButton     *m_btnAdd;
    QLabel          *m_statusLabel;
    QProgressBar    *m_progress;
    QSet<QString>    m_existingSerials;
    QSet<QString>    m_seenSerials;

    void addDeviceRow(const DiscoveredDevice &dev);
};
