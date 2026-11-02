#include "app/SubtitlesPanel.h"

#include "app/Formatting.h"
#include "app/ProjectController.h"

#include <QAction>
#include <QCheckBox>
#include <QColorDialog>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <cmath>

namespace bc {

namespace {
constexpr int kIdRole = Qt::UserRole + 1;
const char *kDirKey = "subtitles/lastDir";

double fpsOf(const ProjectController *c)
{
    const double f = c->project().sequence.fps();
    return f > 0 ? f : 25.0;
}
} // namespace

SubtitlesPanel::SubtitlesPanel(ProjectController *controller, QWidget *parent)
    : QWidget(parent)
    , m_ctl(controller)
    , m_addAct(new QAction(this))
    , m_deleteAct(new QAction(this))
    , m_importAct(new QAction(this))
    , m_exportAct(new QAction(this))
    , m_addBtn(new QToolButton(this))
    , m_deleteBtn(new QToolButton(this))
    , m_importBtn(new QToolButton(this))
    , m_exportBtn(new QToolButton(this))
    , m_list(new QTreeWidget(this))
    , m_empty(new QLabel(this))
    , m_editBox(new QGroupBox(this))
    , m_startLabel(new QLabel(this))
    , m_lengthLabel(new QLabel(this))
    , m_start(new QDoubleSpinBox(this))
    , m_length(new QDoubleSpinBox(this))
    , m_text(new QPlainTextEdit(this))
    , m_styleBox(new QGroupBox(this))
    , m_show(new QCheckBox(this))
    , m_sizeLabel(new QLabel(this))
    , m_marginLabel(new QLabel(this))
    , m_size(new QSpinBox(this))
    , m_margin(new QSpinBox(this))
    , m_color(new QToolButton(this))
    , m_box(new QCheckBox(this))
{
    setObjectName(QStringLiteral("subtitlesPanel"));
    m_addAct->setObjectName(QStringLiteral("actAddSubtitle"));
    m_deleteAct->setObjectName(QStringLiteral("actDeleteSubtitle"));
    m_importAct->setObjectName(QStringLiteral("actImportSrt"));
    m_exportAct->setObjectName(QStringLiteral("actExportSrt"));
    m_list->setObjectName(QStringLiteral("subtitleList"));
    m_empty->setObjectName(QStringLiteral("subtitleEmpty"));
    m_start->setObjectName(QStringLiteral("subtitleStart"));
    m_length->setObjectName(QStringLiteral("subtitleLength"));
    m_text->setObjectName(QStringLiteral("subtitleText"));
    m_show->setObjectName(QStringLiteral("subtitleShow"));
    m_size->setObjectName(QStringLiteral("subtitleSize"));
    m_margin->setObjectName(QStringLiteral("subtitleMargin"));
    m_color->setObjectName(QStringLiteral("subtitleColor"));
    m_box->setObjectName(QStringLiteral("subtitleBox"));

    m_addBtn->setDefaultAction(m_addAct);
    m_deleteBtn->setDefaultAction(m_deleteAct);
    m_importBtn->setDefaultAction(m_importAct);
    m_exportBtn->setDefaultAction(m_exportAct);
    auto *bar = new QHBoxLayout;
    bar->addWidget(m_addBtn);
    bar->addWidget(m_deleteBtn);
    bar->addStretch();
    bar->addWidget(m_importBtn);
    bar->addWidget(m_exportBtn);

    m_list->setColumnCount(2);
    m_list->setRootIsDecorated(false);
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_list->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_list->header()->setStretchLastSection(true);
    m_empty->setWordWrap(true);
    m_empty->setStyleSheet(QStringLiteral("color:#8a8a8a;"));

    for (QDoubleSpinBox *s : {m_start, m_length}) {
        s->setDecimals(2);
        s->setSingleStep(0.1);
        s->setSuffix(tr(" s"));
        s->setKeyboardTracking(false);
    }
    m_start->setRange(0, 24 * 3600);
    m_length->setRange(0.04, 600);
    m_text->setTabChangesFocus(true);
    m_text->setMaximumHeight(90);
    auto *timeRow = new QHBoxLayout;
    timeRow->addWidget(m_startLabel);
    timeRow->addWidget(m_start, 1);
    timeRow->addWidget(m_lengthLabel);
    timeRow->addWidget(m_length, 1);
    auto *edit = new QVBoxLayout(m_editBox);
    edit->addLayout(timeRow);
    edit->addWidget(m_text);

    m_size->setRange(SubtitleStyle::kMinSize, SubtitleStyle::kMaxSize);
    m_size->setSuffix(QStringLiteral(" px"));
    m_margin->setRange(0, SubtitleStyle::kMaxMargin);
    m_margin->setSuffix(QStringLiteral(" %"));
    m_color->setFixedWidth(40);
    auto *styleForm = new QFormLayout(m_styleBox);
    styleForm->addRow(m_show);
    auto *sizeRow = new QHBoxLayout;
    sizeRow->addWidget(m_size, 1);
    sizeRow->addWidget(m_color);
    styleForm->addRow(m_sizeLabel, sizeRow);
    styleForm->addRow(m_marginLabel, m_margin);
    styleForm->addRow(m_box);

    auto *lay = new QVBoxLayout(this);
    lay->addLayout(bar);
    lay->addWidget(m_list, 1);
    lay->addWidget(m_empty);
    lay->addWidget(m_editBox);
    lay->addWidget(m_styleBox);

    connect(m_addAct, &QAction::triggered, this, [this] {
        const QUuid id = m_ctl->addSubtitle(m_ctl->playhead());
        if (!id.isNull()) {
            select(id);
            m_text->setFocus();
        }
    });
    connect(m_deleteAct, &QAction::triggered, this, [this] {
        QList<QUuid> ids;
        for (QTreeWidgetItem *it : m_list->selectedItems())
            ids << QUuid(it->data(0, kIdRole).toString());
        if (ids.isEmpty() && !m_current.isNull())
            ids << m_current;
        m_ctl->removeSubtitles(ids);
    });
    connect(m_importAct, &QAction::triggered, this, [this] { importFile(); });
    connect(m_exportAct, &QAction::triggered, this, [this] { exportFile(); });
    connect(m_list, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *it) {
        if (m_updating || !it)
            return;
        m_current = QUuid(it->data(0, kIdRole).toString());
        showCurrent();
        if (const Subtitle *s = m_ctl->timeline().subtitle(m_current)) {
            m_updating = true; // the playhead jump must not pick another row
            m_ctl->setPlayhead(s->start);
            m_updating = false;
        }
    });
    connect(m_start, &QDoubleSpinBox::valueChanged, this, &SubtitlesPanel::timingEdited);
    connect(m_length, &QDoubleSpinBox::valueChanged, this, &SubtitlesPanel::timingEdited);
    connect(m_text, &QPlainTextEdit::textChanged, this, [this] {
        if (!m_updating && !m_current.isNull())
            m_ctl->setSubtitleText(m_current, m_text->toPlainText());
    });
    connect(m_show, &QCheckBox::toggled, this, &SubtitlesPanel::styleEdited);
    connect(m_box, &QCheckBox::toggled, this, &SubtitlesPanel::styleEdited);
    connect(m_size, &QSpinBox::valueChanged, this, &SubtitlesPanel::styleEdited);
    connect(m_margin, &QSpinBox::valueChanged, this, &SubtitlesPanel::styleEdited);
    connect(m_color, &QToolButton::clicked, this, &SubtitlesPanel::pickColor);
    connect(m_ctl, &ProjectController::timelineChanged, this, &SubtitlesPanel::rebuild);
    connect(m_ctl, &ProjectController::projectReset, this, &SubtitlesPanel::rebuild);
    connect(m_ctl, &ProjectController::openSequenceChanged, this, &SubtitlesPanel::rebuild);
    connect(m_ctl, &ProjectController::sequenceChanged, this, &SubtitlesPanel::rebuild);
    connect(m_ctl, &ProjectController::playheadChanged, this, &SubtitlesPanel::followPlayhead);

    retranslate();
    rebuild();
}

void SubtitlesPanel::changeEvent(QEvent *e)
{
    if (e->type() == QEvent::LanguageChange) {
        retranslate();
        rebuild();
    }
    QWidget::changeEvent(e);
}

void SubtitlesPanel::retranslate()
{
    m_addAct->setText(tr("Add subtitle"));
    m_addAct->setToolTip(tr("A new subtitle at the playhead (3 seconds)"));
    m_deleteAct->setText(tr("Delete"));
    m_deleteAct->setToolTip(tr("Delete the selected subtitles"));
    m_importAct->setText(tr("Import subtitles (SRT)..."));
    m_exportAct->setText(tr("Export subtitles (SRT)..."));
    m_addBtn->setText(tr("+ Add"));
    m_importBtn->setText(tr("Import SRT"));
    m_exportBtn->setText(tr("Export SRT"));
    m_list->setHeaderLabels({tr("Start"), tr("Text")});
    m_empty->setText(tr("No subtitles yet. Press \"+ Add\" to write one at the playhead, or import an .srt file."));
    m_editBox->setTitle(tr("Selected subtitle"));
    m_startLabel->setText(tr("Start:"));
    m_lengthLabel->setText(tr("Duration:"));
    m_text->setPlaceholderText(tr("Subtitle text (Enter starts a new line)"));
    m_styleBox->setTitle(tr("Look of all subtitles"));
    m_show->setText(tr("Show in the picture (and burn in on export)"));
    m_sizeLabel->setText(tr("Size:"));
    m_marginLabel->setText(tr("From the bottom:"));
    m_color->setToolTip(tr("Text colour"));
    m_box->setText(tr("Dark box behind the text"));
}

void SubtitlesPanel::select(const QUuid &id)
{
    for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
        QTreeWidgetItem *it = m_list->topLevelItem(i);
        if (QUuid(it->data(0, kIdRole).toString()) == id) {
            m_list->setCurrentItem(it);
            m_current = id;
            showCurrent();
            return;
        }
    }
}

void SubtitlesPanel::rebuild()
{
    m_updating = true;
    const Timeline &tl = m_ctl->timeline();
    const double fps = fpsOf(m_ctl);
    if (!m_current.isNull() && !tl.subtitle(m_current))
        m_current = {};
    m_list->clear();
    QTreeWidgetItem *cur = nullptr;
    for (const Subtitle &s : tl.subtitles) {
        auto *it = new QTreeWidgetItem(m_list);
        it->setData(0, kIdRole, s.id.toString());
        it->setText(0, Fmt::frameTimecode(s.start, fps));
        QString line = s.text.simplified();
        it->setText(1, line.isEmpty() ? tr("(empty)") : line);
        it->setToolTip(1, s.text);
        if (s.id == m_current)
            cur = it;
    }
    if (cur)
        m_list->setCurrentItem(cur);
    m_empty->setVisible(tl.subtitles.isEmpty());
    m_deleteAct->setEnabled(!tl.subtitles.isEmpty());
    m_exportAct->setEnabled(!tl.subtitles.isEmpty());

    const SubtitleStyle &st = tl.subtitleStyle;
    m_show->setChecked(st.show);
    m_size->setValue(st.size);
    m_margin->setValue(st.margin);
    m_box->setChecked(st.box);
    m_color->setStyleSheet(QStringLiteral("background:%1;").arg(st.color));
    m_color->setProperty("color", st.color);
    m_updating = false;
    showCurrent();
}

void SubtitlesPanel::showCurrent()
{
    const Subtitle *s = m_ctl->timeline().subtitle(m_current);
    m_editBox->setEnabled(s != nullptr);
    const bool was = m_updating;
    m_updating = true;
    const double fps = fpsOf(m_ctl);
    m_start->setValue(s ? s->start / fps : 0);
    m_length->setValue(s ? s->length / fps : 0);
    const QString text = s ? s->text : QString();
    if (m_text->toPlainText() != text)
        m_text->setPlainText(text);
    m_updating = was;
}

void SubtitlesPanel::followPlayhead(qint64 frame)
{
    if (m_updating || m_text->hasFocus())
        return;
    const Subtitle *s = m_ctl->timeline().subtitleAt(frame);
    if (!s || s->id == m_current)
        return;
    m_updating = true;
    for (int i = 0; i < m_list->topLevelItemCount(); ++i)
        if (QUuid(m_list->topLevelItem(i)->data(0, kIdRole).toString()) == s->id)
            m_list->setCurrentItem(m_list->topLevelItem(i));
    m_current = s->id;
    m_updating = false;
    showCurrent();
}

void SubtitlesPanel::timingEdited()
{
    if (m_updating || m_current.isNull())
        return;
    const double fps = fpsOf(m_ctl);
    m_ctl->setSubtitleTiming(m_current, qint64(std::llround(m_start->value() * fps)),
                             std::max<qint64>(1, qint64(std::llround(m_length->value() * fps))));
}

void SubtitlesPanel::styleEdited()
{
    if (m_updating)
        return;
    SubtitleStyle st = m_ctl->timeline().subtitleStyle;
    st.show = m_show->isChecked();
    st.size = m_size->value();
    st.margin = m_margin->value();
    st.box = m_box->isChecked();
    m_ctl->setSubtitleStyle(st);
}

void SubtitlesPanel::pickColor()
{
    const QColor c = QColorDialog::getColor(QColor(m_ctl->timeline().subtitleStyle.color), this, tr("Text colour"));
    if (!c.isValid())
        return;
    SubtitleStyle st = m_ctl->timeline().subtitleStyle;
    st.color = c.name();
    m_ctl->setSubtitleStyle(st);
}

int SubtitlesPanel::importFile(const QString &given)
{
    QSettings settings;
    QString path = given;
    if (path.isEmpty()) {
        path = QFileDialog::getOpenFileName(this, tr("Import subtitles"), settings.value(QString::fromLatin1(kDirKey)).toString(),
                                            tr("Subtitles (*.srt);;All files (*)"));
        if (path.isEmpty())
            return 0;
    }
    bool replace = false;
    if (given.isEmpty() && !m_ctl->timeline().subtitles.isEmpty()) {
        QMessageBox box(QMessageBox::Question, tr("Import subtitles"),
                        tr("The sequence already has subtitles. Replace them or add the new ones?"),
                        QMessageBox::Cancel, this);
        QPushButton *rep = box.addButton(tr("Replace"), QMessageBox::DestructiveRole);
        box.addButton(tr("Add"), QMessageBox::AcceptRole);
        box.exec();
        if (box.clickedButton() == box.button(QMessageBox::Cancel))
            return 0;
        replace = box.clickedButton() == reinterpret_cast<QAbstractButton *>(rep);
    }
    QString error;
    int skipped = 0;
    const int n = m_ctl->importSrt(path, 0, replace, &error, &skipped);
    settings.setValue(QString::fromLatin1(kDirKey), QFileInfo(path).absolutePath());
    if (given.isEmpty()) {
        if (n == 0)
            QMessageBox::warning(this, tr("Import subtitles"), error);
        else if (skipped > 0)
            QMessageBox::information(this, tr("Import subtitles"),
                                     tr("%1 subtitles imported; %2 broken blocks were skipped.").arg(n).arg(skipped));
    }
    return n;
}

bool SubtitlesPanel::exportFile(const QString &given)
{
    QSettings settings;
    QString path = given;
    if (path.isEmpty()) {
        const QString dir = settings.value(QString::fromLatin1(kDirKey)).toString();
        path = QFileDialog::getSaveFileName(this, tr("Export subtitles"),
                                            QDir(dir).filePath(m_ctl->project().name + QStringLiteral(".srt")),
                                            tr("Subtitles (*.srt)"));
        if (path.isEmpty())
            return false;
        if (QFileInfo(path).suffix().isEmpty())
            path += QStringLiteral(".srt");
    }
    QString error;
    const bool ok = m_ctl->exportSrt(path, &error);
    if (ok)
        settings.setValue(QString::fromLatin1(kDirKey), QFileInfo(path).absolutePath());
    else if (given.isEmpty())
        QMessageBox::warning(this, tr("Export subtitles"), error);
    return ok;
}

} // namespace bc
