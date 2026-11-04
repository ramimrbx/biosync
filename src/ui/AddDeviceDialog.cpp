#include "AddDeviceDialog.h"
#include "ui/Theme.h"
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QLabel>
#include <QFrame>
#include <QPushButton>

// Field/input styling matches ritems' Input.tsx: surface-muted background, bordered, rounded-xl,
// accent border on focus.
static QString inputCss() {
    const ThemePalette &t = Theme::current();
    return QString(
           "QLineEdit, QSpinBox {"
           "  background:%1; border:1.5px solid %2; border-radius:12px;"
           "  padding:9px 13px; font-size:13px; color:%3;"
           "}"
           "QLineEdit:focus, QSpinBox:focus { border-color:%4; background:%5; }"
           "QLineEdit::placeholder { color:%6; }"
           "QTextEdit { background:%1; border:1.5px solid %2; border-radius:12px;"
           "  padding:9px 13px; font-size:13px; color:%3; }"
           "QTextEdit:focus { border-color:%4; background:%5; }"
           "QCheckBox { font-size:13px; color:%7; spacing:8px; }"
           "QCheckBox::indicator { width:18px; height:18px; border-radius:5px;"
           "  border:1.5px solid %2; background:%5; }"
           "QCheckBox::indicator:checked { background:%4; border-color:%4; }"
           "QCheckBox::indicator:checked:hover { background:%8; }")
        .arg(t.headerBg.name(), t.border.name(), t.textPrimary.name(), t.accent.name(),
             t.cardBg.name(), t.placeholder.name(), t.textSecondary.name(), t.accentHover.name());
}

AddDeviceDialog::AddDeviceDialog(QWidget *parent, const Device &device)
    : QDialog(parent), m_editId(device.id)
{
    const ThemePalette &t = Theme::current();
    setWindowTitle(device.id == 0 ? "Add New Device" : "Edit Device");
    setMinimumWidth(460);
    setStyleSheet(
        QString("QDialog { background:%1; }").arg(t.pageBg.name())
        + inputCss());

    // ── Header ───────────────────────────────────────────────────────────────
    // Mirrors ritems' Modal.tsx "default" header: a soft accent wash with an accent-colored
    // title, not a solid brand-color banner.
    auto *titleBar = new QWidget();
    titleBar->setAttribute(Qt::WA_StyledBackground, true);
    titleBar->setStyleSheet(QString(
        "background:%1; border-bottom:1px solid %2;")
        .arg(t.accentSoft(), t.border.name()));
    titleBar->setFixedHeight(58);
    auto *tbLayout = new QHBoxLayout(titleBar);
    tbLayout->setContentsMargins(20, 0, 20, 0);
    auto *tbTitle = new QLabel(device.id == 0 ? "Add New Device" : "Edit Device");
    tbTitle->setStyleSheet(QString("color:%1; font-size:16px; font-weight:700;").arg(t.accent.name()));
    auto *tbSub = new QLabel(device.id == 0 ? "Register a ZKTeco or RitOS device" : "Update device configuration");
    tbSub->setStyleSheet(QString("color:%1; font-size:11px;").arg(t.textSecondary.name()));
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

    auto mkLabel = [&t](const QString &text, bool required = false) {
        auto *lbl = new QLabel(required
            ? text + QString(" <span style='color:%1'>*</span>").arg(t.danger.name())
            : text);
        lbl->setStyleSheet(QString("font-size:12px; color:%1; font-weight:500;").arg(t.textSecondary.name()));
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
    divider->setStyleSheet(QString("color:%1;").arg(t.border.name()));

    // ── Buttons ───────────────────────────────────────────────────────────────
    auto *btnCancel = new QPushButton("Cancel");
    auto *btnOk     = new QPushButton(device.id == 0 ? "Add Device" : "Save Changes");
    btnCancel->setStyleSheet(QString(
        "QPushButton { background:%1; color:%2; border:1.5px solid %3;"
        "  padding:9px 20px; border-radius:12px; font-weight:600; }"
        "QPushButton:hover { background:%4; border-color:%5; color:%5; }")
        .arg(t.cardBg.name(), t.textSecondary.name(), t.border.name(), t.accentSoft(), t.accent.name()));
    btnOk->setStyleSheet(QString(
        "QPushButton { background:%1; color:%2; border:none;"
        "  padding:10px 24px; border-radius:12px; font-weight:700; font-size:13px; }"
        "QPushButton:hover   { background:%3; }"
        "QPushButton:pressed { background:%4; }")
        .arg(t.accent.name(), t.onAccent.name(), t.accentHover.name(), t.accentPressed.name()));
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
