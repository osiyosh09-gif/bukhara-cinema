#include "app/EffectsPanel.h"

#include "app/ProjectController.h"
#include "app/TimelinePanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QSet>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>

namespace bc {

namespace {

bool isSwitch(const EffectParamSpec &s)
{
    return s.min == 0 && s.max == 1 && s.step == 1;
}

QString paramObjectName(const QString &key)
{
    return QStringLiteral("effectParam_") + key;
}

} // namespace

QString EffectsPanel::kindName(ClipEffect::Kind kind)
{
    switch (kind) {
    case ClipEffect::Kind::BrightnessContrast:
        return tr("Brightness and contrast");
    case ClipEffect::Kind::Saturation:
        return tr("Saturation");
    case ClipEffect::Kind::Blur:
        return tr("Blur");
    case ClipEffect::Kind::Sharpen:
        return tr("Sharpen");
    case ClipEffect::Kind::Crop:
        return tr("Crop");
    case ClipEffect::Kind::Mirror:
        return tr("Mirror");
    case ClipEffect::Kind::ChromaKey:
        return tr("Chroma key (remove a colour)");
    case ClipEffect::Kind::Mask:
        return tr("Mask (rectangle or ellipse)");
    }
    return {};
}

QString EffectsPanel::paramName(const QString &key)
{
    if (key == QLatin1String("brightness"))
        return tr("Brightness");
    if (key == QLatin1String("contrast"))
        return tr("Contrast");
    if (key == QLatin1String("saturation"))
        return tr("Saturation, %");
    if (key == QLatin1String("radius"))
        return tr("Radius, px");
    if (key == QLatin1String("amount"))
        return tr("Amount, %");
    if (key == QLatin1String("left"))
        return tr("Left, %");
    if (key == QLatin1String("right"))
        return tr("Right, %");
    if (key == QLatin1String("top"))
        return tr("Top, %");
    if (key == QLatin1String("bottom"))
        return tr("Bottom, %");
    if (key == QLatin1String("horizontal"))
        return tr("Flip left and right");
    if (key == QLatin1String("vertical"))
        return tr("Flip upside down");
    if (key == QLatin1String("hue"))
        return tr("Key colour (hue), °");
    if (key == QLatin1String("tolerance"))
        return tr("Tolerance, °");
    if (key == QLatin1String("softness"))
        return tr("Softness, °");
    if (key == QLatin1String("minimum"))
        return tr("Keep greys below, %");
    if (key == QLatin1String("spill"))
        return tr("Spill removal, %");
    if (key == QLatin1String("ellipse"))
        return tr("Ellipse");
    if (key == QLatin1String("x"))
        return tr("Centre X, %");
    if (key == QLatin1String("y"))
        return tr("Centre Y, %");
    if (key == QLatin1String("width"))
        return tr("Width, %");
    if (key == QLatin1String("height"))
        return tr("Height, %");
    if (key == QLatin1String("feather"))
        return tr("Soft edge, px");
    if (key == QLatin1String("invert"))
        return tr("Invert (hide the inside)");
    return key;
}

EffectsPanel::EffectsPanel(ProjectController *controller, TimelineView *view, QWidget *parent)
    : QWidget(parent)
    , m_ctl(controller)
    , m_view(view)
    , m_header(new QLabel)
    , m_empty(new QLabel)
    , m_body(new QWidget)
    , m_kind(new QComboBox)
    , m_add(new QToolButton)
    , m_list(new QListWidget)
    , m_up(new QToolButton)
    , m_down(new QToolButton)
    , m_remove(new QToolButton)
    , m_paramsBox(new QGroupBox)
    , m_form(new QFormLayout(m_paramsBox))
    , m_note(new QLabel)
{
    setObjectName(QStringLiteral("effectsPanel"));
    m_empty->setObjectName(QStringLiteral("effectsEmpty"));
    m_kind->setObjectName(QStringLiteral("effectAddKind"));
    m_add->setObjectName(QStringLiteral("effectAdd"));
    m_list->setObjectName(QStringLiteral("effectList"));
    m_up->setObjectName(QStringLiteral("effectUp"));
    m_down->setObjectName(QStringLiteral("effectDown"));
    m_remove->setObjectName(QStringLiteral("effectRemove"));
    m_paramsBox->setObjectName(QStringLiteral("effectParams"));

    auto *lay = new QVBoxLayout(this);
    m_header->setStyleSheet(QStringLiteral("color:#d1a54a; font-weight:600;"));
    m_header->setWordWrap(true);
    lay->addWidget(m_header);
    m_empty->setAlignment(Qt::AlignCenter);
    m_empty->setWordWrap(true);
    m_empty->setStyleSheet(QStringLiteral("color:#8a8a8a;"));
    lay->addWidget(m_empty);

    auto *body = new QVBoxLayout(m_body);
    body->setContentsMargins(0, 0, 0, 0);
    auto *addRow = new QHBoxLayout;
    addRow->addWidget(m_kind, 1);
    m_add->setToolButtonStyle(Qt::ToolButtonTextOnly);
    addRow->addWidget(m_add);
    body->addLayout(addRow);
    m_list->setMaximumHeight(150);
    body->addWidget(m_list);
    auto *btnRow = new QHBoxLayout;
    m_up->setText(QStringLiteral("▲"));
    m_down->setText(QStringLiteral("▼"));
    btnRow->addWidget(m_up);
    btnRow->addWidget(m_down);
    btnRow->addStretch(1);
    btnRow->addWidget(m_remove);
    body->addLayout(btnRow);
    body->addWidget(m_paramsBox);
    m_note->setWordWrap(true);
    m_note->setStyleSheet(QStringLiteral("color:#8a8a8a;"));
    body->addWidget(m_note);
    lay->addWidget(m_body);
    lay->addStretch(1);

    connect(m_add, &QToolButton::clicked, this, &EffectsPanel::addEffect);
    connect(m_list, &QListWidget::currentRowChanged, this, [this] {
        if (!m_updating)
            refreshParams();
    });
    connect(m_list, &QListWidget::itemChanged, this, [this](QListWidgetItem *item) {
        if (m_updating || m_clip.isNull())
            return;
        m_ctl->setClipEffectEnabled(m_clip, m_list->row(item), item->checkState() == Qt::Checked);
    });
    auto move = [this](int delta) {
        const int row = currentEffect();
        if (row >= 0 && m_ctl->moveClipEffect(m_clip, row, delta))
            m_list->setCurrentRow(row + delta);
    };
    connect(m_up, &QToolButton::clicked, this, [move] { move(-1); });
    connect(m_down, &QToolButton::clicked, this, [move] { move(1); });
    connect(m_remove, &QToolButton::clicked, this, [this] {
        const int row = currentEffect();
        if (row >= 0)
            m_ctl->removeClipEffect(m_clip, row);
    });
    connect(m_view, &TimelineView::selectionChanged, this, &EffectsPanel::refresh);
    connect(m_ctl, &ProjectController::timelineChanged, this, &EffectsPanel::refresh);
    connect(m_ctl, &ProjectController::projectReset, this, &EffectsPanel::refresh);
    retranslate();
}

int EffectsPanel::currentEffect() const
{
    return m_clip.isNull() ? -1 : m_list->currentRow();
}

void EffectsPanel::changeEvent(QEvent *e)
{
    if (e->type() == QEvent::LanguageChange)
        retranslate();
    QWidget::changeEvent(e);
}

void EffectsPanel::retranslate()
{
    {
        const QSignalBlocker b(m_kind);
        const int cur = m_kind->currentIndex();
        m_kind->clear();
        for (const ClipEffect::Kind k : ClipEffect::kinds())
            m_kind->addItem(kindName(k), int(k));
        m_kind->setCurrentIndex(std::max(0, cur));
    }
    m_add->setText(tr("Add"));
    m_add->setToolTip(tr("Add the chosen effect to the clip"));
    m_up->setToolTip(tr("Move up (effects work from top to bottom)"));
    m_down->setToolTip(tr("Move down"));
    m_remove->setText(tr("Remove"));
    m_paramsBox->setTitle(tr("Values"));
    m_note->setText(tr("Effects are drawn before the motion settings. Untick an effect to compare without it."));
    m_formKind = -1; // rebuild the labels
    refresh();
}

void EffectsPanel::refresh()
{
    const Timeline &tl = m_ctl->timeline();
    // One video clip (a linked sound part may be selected with it).
    QSet<QUuid> groups;
    QUuid video;
    for (const QUuid &id : m_view->selection()) {
        const TimelineClip *c = tl.clip(id);
        if (!c)
            continue;
        groups.insert(c->linkId.isNull() ? c->id : c->linkId);
        if (tl.kindOf(id) == Timeline::Kind::Video)
            video = id;
    }
    m_clip = groups.size() == 1 ? video : QUuid();

    const TimelineClip *c = tl.clip(m_clip);
    m_empty->setVisible(!c);
    m_body->setVisible(c);
    m_empty->setText(groups.isEmpty()     ? tr("Select a video clip on the timeline to add effects.")
                     : groups.size() > 1 ? tr("Several clips are selected. Select one clip.")
                                         : tr("Effects work on pictures. Select a video clip."));
    QString header;
    if (c) {
        if (c->isTitle())
            header = tr("Title");
        else if (c->isNested())
            header = m_ctl->sequenceName(c->nestId);
        else if (const MediaItem *m = m_ctl->project().item(c->mediaId))
            header = m->name;
    }
    m_header->setText(header);
    m_header->setVisible(!header.isEmpty());

    m_updating = true;
    const int keep = m_list->currentRow();
    const ClipEffects effects = c ? c->effects : ClipEffects();
    if (m_list->count() != effects.size())
        m_list->clear();
    for (int i = 0; i < effects.size(); ++i) {
        QListWidgetItem *item = m_list->item(i);
        if (!item) {
            item = new QListWidgetItem;
            item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
            m_list->addItem(item);
        }
        item->setText(kindName(effects[i].kind));
        item->setCheckState(effects[i].enabled ? Qt::Checked : Qt::Unchecked);
    }
    if (!effects.isEmpty())
        m_list->setCurrentRow(std::clamp(keep, 0, int(effects.size()) - 1));
    m_updating = false;

    const bool locked = c && tl.tracks[tl.find(m_clip).track].locked;
    m_body->setEnabled(!locked);
    refreshParams();
}

void EffectsPanel::refreshParams()
{
    const TimelineClip *c = m_ctl->timeline().clip(m_clip);
    const int row = currentEffect();
    const bool has = c && row >= 0 && row < c->effects.size();
    m_up->setEnabled(has && row > 0);
    m_down->setEnabled(has && c && row + 1 < c->effects.size());
    m_remove->setEnabled(has);
    m_paramsBox->setVisible(has);
    if (!has) {
        m_formClip = QUuid();
        m_formIndex = m_formKind = -1;
        return;
    }
    const ClipEffect &e = c->effects[row];
    const QList<EffectParamSpec> specs = ClipEffect::specs(e.kind);
    if (m_formClip != m_clip || m_formIndex != row || m_formKind != int(e.kind)) {
        while (m_form->rowCount() > 0)
            m_form->removeRow(0);
        for (const EffectParamSpec &s : specs) {
            const QString key = s.key;
            if (isSwitch(s)) {
                auto *box = new QCheckBox(paramName(key));
                box->setObjectName(paramObjectName(key));
                connect(box, &QCheckBox::toggled, this, [this, key](bool on) {
                    if (!m_updating)
                        m_ctl->setClipEffectValue(m_clip, currentEffect(), key, on ? 1 : 0);
                });
                m_form->addRow(box);
            } else {
                auto *spin = new QDoubleSpinBox;
                spin->setObjectName(paramObjectName(key));
                spin->setRange(s.min, s.max);
                spin->setSingleStep(s.step);
                spin->setDecimals(s.decimals);
                spin->setKeyboardTracking(false);
                connect(spin, &QDoubleSpinBox::valueChanged, this, [this, key](double v) {
                    if (!m_updating)
                        m_ctl->setClipEffectValue(m_clip, currentEffect(), key, v);
                });
                m_form->addRow(paramName(key), spin);
            }
        }
        m_formClip = m_clip;
        m_formIndex = row;
        m_formKind = int(e.kind);
    }
    m_updating = true;
    for (const EffectParamSpec &s : specs) {
        QWidget *w = m_paramsBox->findChild<QWidget *>(paramObjectName(s.key));
        if (auto *box = qobject_cast<QCheckBox *>(w))
            box->setChecked(e.value(s.key) >= 0.5);
        else if (auto *spin = qobject_cast<QDoubleSpinBox *>(w))
            spin->setValue(e.value(s.key));
    }
    m_paramsBox->setEnabled(e.enabled);
    m_updating = false;
}

void EffectsPanel::addEffect()
{
    if (m_clip.isNull())
        return;
    const int index = m_ctl->addClipEffect(m_clip, ClipEffect::Kind(m_kind->currentData().toInt()));
    if (index >= 0)
        m_list->setCurrentRow(index);
}

} // namespace bc
