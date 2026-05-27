#include "app/calendar_sources_dialog.h"

#include "app/external_sync.h"
#include "storage/store.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace zb {

// --- SourceEditDialog --------------------------------------------------------

SourceEditDialog::SourceEditDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Calendar source"));

    m_name = new QLineEdit(this);
    m_kind = new QComboBox(this);
    m_kind->addItem(QStringLiteral("Local file"),
                    static_cast<int>(ExternalSource::Kind::File));
    m_kind->addItem(QStringLiteral("URL (ICS / webcal)"),
                    static_cast<int>(ExternalSource::Kind::Url));

    m_location = new QLineEdit(this);
    m_browse = new QPushButton(QStringLiteral("Browse…"), this);
    auto* locRow = new QHBoxLayout;
    locRow->addWidget(m_location, 1);
    locRow->addWidget(m_browse);

    m_color = new QPushButton(this);
    m_color->setFixedWidth(80);
    m_colorHex = QStringLiteral("#1e88e5");
    applyColorSwatch();

    m_enabled = new QCheckBox(QStringLiteral("Enabled"), this);
    m_enabled->setChecked(true);

    auto* form = new QFormLayout(this);
    form->addRow(QStringLiteral("Name"), m_name);
    form->addRow(QStringLiteral("Kind"), m_kind);
    form->addRow(QStringLiteral("Location"), locRow);
    form->addRow(QStringLiteral("Colour"), m_color);
    form->addRow(QString(), m_enabled);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        if (m_location->text().trimmed().isEmpty()) {
            QMessageBox::warning(this, windowTitle(),
                                 QStringLiteral("A location is required."));
            return;
        }
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    form->addRow(buttons);

    connect(m_browse, &QPushButton::clicked, this, &SourceEditDialog::browse);
    connect(m_color, &QPushButton::clicked, this, &SourceEditDialog::pickColor);

    m_name->setFocus();
}

void SourceEditDialog::applyColorSwatch()
{
    m_color->setText(m_colorHex);
    m_color->setStyleSheet(
        QStringLiteral("background:%1; color:white;").arg(m_colorHex));
}

void SourceEditDialog::setSource(const ExternalSource& s)
{
    m_id = s.id;
    m_name->setText(s.name);
    m_kind->setCurrentIndex(m_kind->findData(static_cast<int>(s.kind)));
    m_location->setText(s.location);
    if (!s.color.isEmpty() && QColor(s.color).isValid()) {
        m_colorHex = s.color;
        applyColorSwatch();
    }
    m_enabled->setChecked(s.enabled);
    setWindowTitle(QStringLiteral("Edit calendar source"));
}

ExternalSource SourceEditDialog::source() const
{
    ExternalSource s;
    s.id = m_id;
    s.name = m_name->text().trimmed().isEmpty()
                 ? QStringLiteral("Calendar")
                 : m_name->text().trimmed();
    s.kind = static_cast<ExternalSource::Kind>(m_kind->currentData().toInt());
    s.location = m_location->text().trimmed();
    s.color = m_colorHex;
    s.enabled = m_enabled->isChecked();
    return s;
}

void SourceEditDialog::browse()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Choose an iCalendar file"), QString(),
        QStringLiteral("iCalendar (*.ics *.ical *.ifb);;All files (*)"));
    if (!path.isEmpty()) {
        m_location->setText(path);
        m_kind->setCurrentIndex(
            m_kind->findData(static_cast<int>(ExternalSource::Kind::File)));
    }
}

void SourceEditDialog::pickColor()
{
    const QColor c = QColorDialog::getColor(QColor(m_colorHex), this,
                                            QStringLiteral("Source colour"));
    if (c.isValid()) {
        m_colorHex = c.name();
        applyColorSwatch();
    }
}

// --- CalendarSourcesDialog ---------------------------------------------------

CalendarSourcesDialog::CalendarSourcesDialog(Store& store, ExternalSync& sync,
                                             QWidget* parent)
    : QDialog(parent), m_store(store), m_sync(sync)
{
    setWindowTitle(QStringLiteral("Calendars"));
    resize(460, 320);

    m_list = new QListWidget(this);

    auto* add = new QPushButton(QStringLiteral("Add…"), this);
    m_edit = new QPushButton(QStringLiteral("Edit…"), this);
    m_remove = new QPushButton(QStringLiteral("Remove"), this);
    auto* sideBtns = new QVBoxLayout;
    sideBtns->addWidget(add);
    sideBtns->addWidget(m_edit);
    sideBtns->addWidget(m_remove);
    sideBtns->addStretch(1);

    auto* row = new QHBoxLayout;
    row->addWidget(m_list, 1);
    row->addLayout(sideBtns);

    auto* refresh = new QPushButton(QStringLiteral("Refresh now"), this);
    auto* close = new QPushButton(QStringLiteral("Close"), this);
    auto* bottom = new QHBoxLayout;
    bottom->addWidget(refresh);
    bottom->addStretch(1);
    bottom->addWidget(close);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(
        new QLabel(QStringLiteral("Read-only calendars overlaid on the grid. "
                                  "A URL can be a provider's secret iCal "
                                  "address (Google / iCloud / Outlook)."),
                   this));
    layout->addLayout(row, 1);
    layout->addLayout(bottom);

    connect(add, &QPushButton::clicked, this, &CalendarSourcesDialog::add);
    connect(m_edit, &QPushButton::clicked, this, &CalendarSourcesDialog::edit);
    connect(m_list, &QListWidget::itemDoubleClicked, this,
            &CalendarSourcesDialog::edit);
    connect(m_remove, &QPushButton::clicked, this,
            &CalendarSourcesDialog::remove);
    connect(refresh, &QPushButton::clicked, this,
            [this] { m_sync.refreshAll(); });
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_list, &QListWidget::itemSelectionChanged, this, [this] {
        const bool has = selectedId() > 0;
        m_edit->setEnabled(has);
        m_remove->setEnabled(has);
    });

    reloadList();
}

void CalendarSourcesDialog::reloadList()
{
    m_list->clear();
    for (const ExternalSource& s : m_store.listExternalSources()) {
        const QString kind = s.kind == ExternalSource::Kind::File
                                 ? QStringLiteral("file")
                                 : QStringLiteral("url");
        auto* item = new QListWidgetItem(
            QStringLiteral("%1  —  %2 (%3)%4")
                .arg(s.name, s.location, kind,
                     s.enabled ? QString() : QStringLiteral("  [disabled]")),
            m_list);
        item->setData(Qt::UserRole, static_cast<qlonglong>(s.id));
        if (!s.color.isEmpty() && QColor(s.color).isValid())
            item->setForeground(QColor(s.color));
    }
    m_edit->setEnabled(false);
    m_remove->setEnabled(false);
}

Id CalendarSourcesDialog::selectedId() const
{
    auto* item = m_list->currentItem();
    return item ? item->data(Qt::UserRole).toLongLong() : -1;
}

void CalendarSourcesDialog::add()
{
    SourceEditDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    const Id id = m_store.createExternalSource(dlg.source());
    reloadList();
    // Pull its events immediately so the grid reflects it.
    for (const ExternalSource& s : m_store.listExternalSources())
        if (s.id == id)
            m_sync.refreshSource(s);
}

void CalendarSourcesDialog::edit()
{
    const Id id = selectedId();
    if (id <= 0)
        return;
    ExternalSource current;
    for (const ExternalSource& s : m_store.listExternalSources())
        if (s.id == id)
            current = s;

    SourceEditDialog dlg(this);
    dlg.setSource(current);
    if (dlg.exec() != QDialog::Accepted)
        return;
    m_store.updateExternalSource(dlg.source());
    reloadList();
    for (const ExternalSource& s : m_store.listExternalSources())
        if (s.id == id)
            m_sync.refreshSource(s);
}

void CalendarSourcesDialog::remove()
{
    const Id id = selectedId();
    if (id <= 0)
        return;
    if (QMessageBox::question(this, QStringLiteral("Remove calendar"),
                              QStringLiteral("Remove this calendar and its "
                                             "cached events?"))
        != QMessageBox::Yes)
        return;
    m_store.deleteExternalSource(id); // cached events cascade
    reloadList();
}

} // namespace zb
