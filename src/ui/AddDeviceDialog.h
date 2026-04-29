#pragma once
#include <QDialog>
#include <QLineEdit>
#include <QSpinBox>
#include <QCheckBox>
#include <QTextEdit>
#include "../model/Device.h"

class AddDeviceDialog : public QDialog {
    Q_OBJECT
public:
    explicit AddDeviceDialog(QWidget *parent = nullptr, const Device &device = {});
    Device device() const;

private:
    QLineEdit *m_name;
    QLineEdit *m_serial;
    QLineEdit *m_ip;
    QSpinBox  *m_port;
    QLineEdit *m_location;
    QTextEdit *m_notes;
    QCheckBox *m_active;
    int m_editId = 0;
};
