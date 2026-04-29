#include "AddDeviceDialog.h"
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QLabel>
#include <QFrame>
#include <QPushButton>

static QString inputCss() {
    return "QLineEdit, QSpinBox {"
           "  background:#FFFFFF; border:1.5px solid #E5E7EB; border-radius:8px;"
           "  padding:8px 12px; font-size:13px; color:#111827;"
           "}"
           "QLineEdit:focus, QSpinBox:focus { border-color:#8222E3; }"
           "QLineEdit::placeholder { color:#9CA3AF; }"
           "QTextEdit { background:#FFFFFF; border:1.5px solid #E5E7EB; border-radius:8px;"
           "  padding:8px 12px; font-size:13px; color:#111827; }"
           "QTextEdit:focus { border-color:#8222E3; }"
           "QCheckBox { font-size:13px; color:#374151; spacing:8px; }"
           "QCheckBox::indicator { width:18px; height:18px; border-radius:5px;"
           "  border:1.5px solid #D1D5DB; background:#FFFFFF; }"
           "QCheckBox::indicator:checked { background:#8222E3; border-color:#8222E3; }"
           "QCheckBox::indicator:checked:hover { background:#6A1AB8; }";
}

AddDeviceDialog::AddDeviceDialog(QWidget *parent, const Device &device)
    : QDialog(parent), m_editId(device.id)
{
    setWindowTitle(device.id == 0 ? "Add New Device" : "Edit Device");
    setMinimumWidth(460);
    setStyleSheet(
        "QDialog { background:#F4F6FB; }"
        + inputCss());

    // ── Title bar area ────────────────────────────────────────────────────────
    auto *titleBar = new QWidget();
    titleBar->setStyleSheet(
        "background: qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #8222E3,stop:1 #A855F7);"
        "border-radius:0px;");
    titleBar->setFixedHeight(58);
    auto *tbLayout = new QHBoxLayout(titleBar);
    tbLayout->setContentsMargins(20, 0, 20, 0);
    auto *tbTitle = new QLabel(device.id == 0 ? "Add New Device" : "Edit Device");
    tbTitle->setStyleSheet("color:#FFFFFF; font-size:16px; font-weight:700;");
    auto *tbSub = new QLabel(device.id == 0 ? "Register a ZKTeco device" : "Update device configuration");
    tbSub->setStyleSheet("color:rgba(255,255,255,0.7); font-size:11px;");
    auto *tbTextCol = new QVBoxLayout();
    tbTextCol->setSpacing(2);
    tbTextCol->addWidget(tbTitle);
    tbTextCol->addWidget(tbSub);
    tbLayout->addLayout(tbTextCol);

    // ── Fields ────────────────────────────────────────────────────────────────
    m_name   = new QLineEdit(device.name);
    m_serial = new QLineEdit(device.serialNumber);
    m_ip     = new QLineEdit(device.ipAddress);
    m_port   = new QSpinBox();
    m_port->setRange(1, 65535);
    m_port->setValue(device.port > 0 ? device.port : 4370);
    m_location = new QLineEdit(device.location);
    m_notes    = new QTextEdit(device.notes);
    m_notes->setFixedHeight(66);
    m_active   = new QCheckBox("Active");
    m_active->setChecked(device.isActive);

    m_name->setPlaceholderText("e.g. Main Entrance Device");
    m_serial->setPlaceholderText("e.g. ABC1234567");
    m_ip->setPlaceholderText("e.g. 192.168.1.201");
    m_location->setPlaceholderText("e.g. Ground Floor, Gate 1");

    auto mkLabel = [](const QString &text, bool required = false) {
        auto *lbl = new QLabel(required ? text + " <span style='color:#EF4444'>*</span>" : text);
        lbl->setStyleSheet("font-size:12px; color:#374151; font-weight:500;");
        lbl->setTextFormat(Qt::RichText);
        return lbl;
    };

    auto mkField = [](QWidget *w) {
        auto *row = new QVBoxLayout();
        row->setSpacing(6);
        return row;
    };

    auto addField = [&](QVBoxLayout *col, const QString &label, QWidget *widget, bool required = false) {
        col->addWidget(new QLabel(
            required ? label + " <span style='color:#EF4444'>*</span>" : label));
        col->addWidget(widget);
        col->itemAt(col->count()-2)->widget()->setStyleSheet(
            "font-size:12px; color:#374151; font-weight:500;");
        static_cast<QLabel*>(col->itemAt(col->count()-2)->widget())->setTextFormat(Qt::RichText);
    };

    auto *col1 = new QVBoxLayout(); col1->setSpacing(4);
    auto *col2 = new QVBoxLayout(); col2->setSpacing(4);

    // Row 1
    auto *row1 = new QHBoxLayout(); row1->setSpacing(14);
    auto *nameCol = new QVBoxLayout(); nameCol->setSpacing(4);
    nameCol->addWidget(mkLabel("Device Name", true)); nameCol->addWidget(m_name);
    auto *serialCol = new QVBoxLayout(); serialCol->setSpacing(4);
    serialCol->addWidget(mkLabel("Serial Number", true)); serialCol->addWidget(m_serial);
    row1->addLayout(nameCol); row1->addLayout(serialCol);

    // Row 2
    auto *row2 = new QHBoxLayout(); row2->setSpacing(14);
    auto *ipCol = new QVBoxLayout(); ipCol->setSpacing(4);
    ipCol->addWidget(mkLabel("IP Address", true)); ipCol->addWidget(m_ip);
    auto *portCol = new QVBoxLayout(); portCol->setSpacing(4);
    portCol->addWidget(mkLabel("Port")); portCol->addWidget(m_port);
    portCol->setStretch(1, 0);
    row2->addLayout(ipCol, 3); row2->addLayout(portCol, 1);

    // Row 3
    auto *locCol = new QVBoxLayout(); locCol->setSpacing(4);
    locCol->addWidget(mkLabel("Location")); locCol->addWidget(m_location);

    // Row 4
    auto *notesCol = new QVBoxLayout(); notesCol->setSpacing(4);
    notesCol->addWidget(mkLabel("Notes")); notesCol->addWidget(m_notes);

    // ── Divider ──────────────────────────────────────────────────────────────
    auto *divider = new QFrame();
    divider->setFrameShape(QFrame::HLine);
    divider->setStyleSheet("color:#E5E7EB;");

    // ── Buttons ───────────────────────────────────────────────────────────────
    auto *btnCancel = new QPushButton("Cancel");
    auto *btnOk     = new QPushButton(device.id == 0 ? "Add Device" : "Save Changes");
    btnCancel->setStyleSheet(
        "QPushButton { background:#FFFFFF; color:#374151; border:1.5px solid #E5E7EB;"
        "  padding:8px 20px; border-radius:8px; font-weight:500; }"
        "QPushButton:hover { background:#F9F5FF; border-color:#8222E3; color:#8222E3; }");
    btnOk->setStyleSheet(
        "QPushButton { background:#8222E3; color:#FFFFFF; border:none;"
        "  padding:9px 24px; border-radius:8px; font-weight:700; font-size:13px; }"
        "QPushButton:hover   { background:#6A1AB8; }"
        "QPushButton:pressed { background:#5a14a0; }");
    btnCancel->setCursor(Qt::PointingHandCursor);
    btnOk->setCursor(Qt::PointingHandCursor);

    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(btnOk,     &QPushButton::clicked, this, [this]() {
        if (m_name->text().trimmed().isEmpty() ||
            m_serial->text().trimmed().isEmpty() ||
            m_ip->text().trimmed().isEmpty()) {
            QMessageBox::warning(this, "Validation",
                "Device Name, Serial Number and IP Address are required.");
            return;
        }
        accept();
    });

    auto *btnRow = new QHBoxLayout();
    btnRow->addWidget(m_active);
    btnRow->addStretch();
    btnRow->addWidget(btnCancel);
    btnRow->addWidget(btnOk);

    // ── Assemble ──────────────────────────────────────────────────────────────
    auto *form = new QVBoxLayout();
    form->setContentsMargins(24, 20, 24, 20);
    form->setSpacing(14);
    form->addLayout(row1);
    form->addLayout(row2);
    form->addLayout(locCol);
    form->addLayout(notesCol);
    form->addWidget(divider);
    form->addLayout(btnRow);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(titleBar);
    root->addLayout(form);
}

Device AddDeviceDialog::device() const {
    Device d;
    d.id           = m_editId;
    d.name         = m_name->text().trimmed();
    d.serialNumber = m_serial->text().trimmed();
    d.ipAddress    = m_ip->text().trimmed();
    d.port         = m_port->value();
    d.location     = m_location->text().trimmed();
    d.notes        = m_notes->toPlainText().trimmed();
    d.isActive     = m_active->isChecked();
    return d;
}
