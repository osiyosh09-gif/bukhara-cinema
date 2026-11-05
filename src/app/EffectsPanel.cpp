#include "app/EffectsPanel.h"

#include "app/ProjectController.h"
#include "app/TimelinePanel.h"
#include "app/Vst3Plugins.h"
#include "core/Vst3Host.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QSet>
#include <QSignalBlocker>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

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

constexpr int kPluginRole = Qt::UserRole + 1; // combo: "path|uid|name" of a plug-in
constexpr int kMaxPluginParams = 48;          // more would not fit the panel
constexpr int kSliderSteps = 1000;

QString pluginKey(const ClipEffect &e)
{
    return e.pluginPath + QLatin1Char('|') + e.pluginId;
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
    case ClipEffect::Kind::Equalizer:
        return tr("Equalizer (EQ)");
    case ClipEffect::Kind::Compressor:
        return tr("Compressor");
    case ClipEffect::Kind::NoiseReduction:
        return tr("Noise reduction");
    case ClipEffect::Kind::HumRemoval:
        return tr("Hum removal (50/60 Hz)");
    case ClipEffect::Kind::Plugin:
        return tr("VST 3 plug-in");
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
    if (key == QLatin1String("low_cut"))
        return tr("Low cut, Hz (0 = off)");
    if (key == QLatin1String("low_gain"))
        return tr("Bass, dB");
    if (key == QLatin1String("low_freq"))
        return tr("Bass frequency, Hz");
    if (key == QLatin1String("mid_gain"))
        return tr("Middle, dB");
    if (key == QLatin1String("mid_freq"))
        return tr("Middle frequency, Hz");
    if (key == QLatin1String("mid_q"))
        return tr("Middle width (Q)");
    if (key == QLatin1String("high_gain"))
        return tr("Treble, dB");
    if (key == QLatin1String("high_freq"))
        return tr("Treble frequency, Hz");
    if (key == QLatin1String("threshold"))
        return tr("Threshold, dB");
    if (key == QLatin1String("ratio"))
        return tr("Ratio (x:1)");
    if (key == QLatin1String("attack"))
        return tr("Attack, ms");
    if (key == QLatin1String("release"))
        return tr("Release, ms");
    if (key == QLatin1String("makeup"))
        return tr("Make-up gain, dB");
    if (key == QLatin1String("reduction"))
        return tr("Reduction, dB");
    if (key == QLatin1String("sensitivity"))
        return tr("Sensitivity, %");
    if (key == QLatin1String("hum_freq"))
        return tr("Mains frequency, Hz");
    if (key == QLatin1String("harmonics"))
        return tr("Harmonics");
    if (key == QLatin1String("notch_width"))
        return tr("Notch width, Hz");
    return key;
}

EffectsPanel::EffectsPanel(ProjectController *controller, TimelineView *view, QWidget *parent)
    : QWidget(parent)
    , m_ctl(controller)
    , m_view(view)
    , m_header(new QLabel)
    , m_target(new QWidget)
    , m_targetVideo(new QToolButton)
    , m_targetAudio(new QToolButton)
    , m_empty(new QLabel)
    , m_body(new QWidget)
    , m_kind(new QComboBox)
    , m_add(new QToolButton)
    , m_findPlugins(new QToolButton)
    , m_list(new QListWidget)
    , m_up(new QToolButton)
    , m_down(new QToolButton)
    , m_remove(new QToolButton)
    , m_paramsBox(new QGroupBox)
    , m_form(new QFormLayout(m_paramsBox))
    , m_note(new QLabel)
    , m_pluginNote(new QLabel)
{
    setObjectName(QStringLiteral("effectsPanel"));
    m_empty->setObjectName(QStringLiteral("effectsEmpty"));
    m_kind->setObjectName(QStringLiteral("effectAddKind"));
    m_add->setObjectName(QStringLiteral("effectAdd"));
    m_findPlugins->setObjectName(QStringLiteral("effectFindPlugins"));
    m_pluginNote->setObjectName(QStringLiteral("effectPluginNote"));
    m_list->setObjectName(QStringLiteral("effectList"));
    m_up->setObjectName(QStringLiteral("effectUp"));
    m_down->setObjectName(QStringLiteral("effectDown"));
    m_remove->setObjectName(QStringLiteral("effectRemove"));
    m_paramsBox->setObjectName(QStringLiteral("effectParams"));

    auto *lay = new QVBoxLayout(this);
    m_header->setStyleSheet(QStringLiteral("color:#d1a54a; font-weight:600;"));
    m_header->setWordWrap(true);
    lay->addWidget(m_header);
    {
        auto *row = new QHBoxLayout(m_target);
        row->setContentsMargins(0, 0, 0, 0);
        m_targetVideo->setObjectName(QStringLiteral("effectTargetVideo"));
        m_targetAudio->setObjectName(QStringLiteral("effectTargetAudio"));
        for (QToolButton *b : {m_targetVideo, m_targetAudio}) {
            b->setCheckable(true);
            b->setAutoExclusive(true);
            b->setToolButtonStyle(Qt::ToolButtonTextOnly);
            row->addWidget(b);
        }
        row->addStretch(1);
        lay->addWidget(m_target);
        connect(m_targetAudio, &QToolButton::toggled, this, [this](bool on) {
            if (m_updating || m_wantAudio == on)
                return;
            m_wantAudio = on;
            refresh();
        });
    }
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
    m_findPlugins->setToolButtonStyle(Qt::ToolButtonTextOnly);
    addRow->addWidget(m_findPlugins);
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
    m_pluginNote->setWordWrap(true);
    m_pluginNote->setStyleSheet(QStringLiteral("color:#d1a54a;"));
    body->addWidget(m_pluginNote);
    m_note->setWordWrap(true);
    m_note->setStyleSheet(QStringLiteral("color:#8a8a8a;"));
    body->addWidget(m_note);
    lay->addWidget(m_body);
    lay->addStretch(1);

    connect(m_add, &QToolButton::clicked, this, &EffectsPanel::addEffect);
    connect(m_findPlugins, &QToolButton::clicked, this, [this] {
        const int n = Vst3Plugins::rescan();
        m_kindsFor = -1;
        fillKinds();
        m_findPlugins->setToolTip(tr("Look for VST 3 plug-ins again. Found: %1").arg(n));
    });
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

void EffectsPanel::fillKinds()
{
    const QSignalBlocker b(m_kind);
    const int cur = m_kindsFor == int(m_audio) ? m_kind->currentIndex() : 0;
    m_kind->clear();
    for (const ClipEffect::Kind k : ClipEffect::kinds(m_audio))
        m_kind->addItem(kindName(k), int(k));
    if (m_audio && Vst3Plugins::available()) {
        const QList<Vst3PluginEntry> plugins = Vst3Plugins::plugins();
        if (!plugins.isEmpty())
            m_kind->insertSeparator(m_kind->count());
        for (const Vst3PluginEntry &p : plugins) {
            const QString text = p.vendor.isEmpty() ? p.name : tr("%1 (%2)").arg(p.name, p.vendor);
            m_kind->addItem(tr("VST 3: %1").arg(text), int(ClipEffect::Kind::Plugin));
            m_kind->setItemData(m_kind->count() - 1, QStringList{p.path, p.uid, p.name}, kPluginRole);
            m_kind->setItemData(m_kind->count() - 1, QDir::toNativeSeparators(p.path), Qt::ToolTipRole);
        }
    }
    m_kind->setCurrentIndex(std::clamp(cur, 0, m_kind->count() - 1));
    m_kindsFor = int(m_audio);
}

void EffectsPanel::retranslate()
{
    fillKinds();
    m_targetVideo->setText(tr("Picture"));
    m_targetAudio->setText(tr("Sound"));
    m_add->setText(tr("Add"));
    m_add->setToolTip(tr("Add the chosen effect to the clip"));
    m_findPlugins->setText(tr("Find plug-ins"));
    m_findPlugins->setToolTip(tr("Look for VST 3 plug-ins again"));
    m_up->setToolTip(tr("Move up (effects work from top to bottom)"));
    m_down->setToolTip(tr("Move down"));
    m_remove->setText(tr("Remove"));
    m_paramsBox->setTitle(tr("Values"));
    m_formKind = -1; // rebuild the labels
    refresh();
}

void EffectsPanel::refresh()
{
    const Timeline &tl = m_ctl->timeline();
    // One clip, or one linked picture + sound pair (the switch picks the part).
    QSet<QUuid> groups;
    QUuid video, audio;
    for (const QUuid &id : m_view->selection()) {
        const TimelineClip *c = tl.clip(id);
        if (!c)
            continue;
        groups.insert(c->linkId.isNull() ? c->id : c->linkId);
        if (tl.kindOf(id) == Timeline::Kind::Video)
            video = id;
        else
            audio = id;
    }
    const bool one = groups.size() == 1;
    const bool both = one && !video.isNull() && !audio.isNull();
    m_audio = one && (video.isNull() || (both && m_wantAudio));
    m_clip = !one ? QUuid() : m_audio ? audio : video;
    if (m_kindsFor != int(m_audio))
        fillKinds();
    m_target->setVisible(both);
    m_findPlugins->setVisible(m_audio && Vst3Plugins::available());
    {
        const QSignalBlocker b1(m_targetVideo), b2(m_targetAudio);
        (m_audio ? m_targetAudio : m_targetVideo)->setChecked(true);
    }
    m_note->setText(m_audio ? tr("Sound effects work before the clip volume and the track fader. Untick an effect "
                                 "to compare without it.")
                            : tr("Effects are drawn before the motion settings. Untick an effect to compare without it."));

    const TimelineClip *c = tl.clip(m_clip);
    m_empty->setVisible(!c);
    m_body->setVisible(c);
    m_empty->setText(groups.isEmpty() ? tr("Select a clip on the timeline to add effects.")
                                      : tr("Several clips are selected. Select one clip."));
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
        item->setText(effects[i].kind == ClipEffect::Kind::Plugin
                          ? tr("%1 (VST 3)").arg(effects[i].pluginName.isEmpty() ? effects[i].pluginId
                                                                                 : effects[i].pluginName)
                          : kindName(effects[i].kind));
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
    m_pluginNote->setVisible(false);
    if (!has) {
        m_formClip = QUuid();
        m_formIndex = m_formKind = -1;
        m_formPlugin.clear();
        return;
    }
    const ClipEffect &e = c->effects[row];
    if (e.kind == ClipEffect::Kind::Plugin) {
        if (m_formClip != m_clip || m_formIndex != row || m_formKind != int(e.kind) || m_formPlugin != pluginKey(e))
            buildPluginForm(e);
        updatePluginValues(e);
        return;
    }
    m_formPlugin.clear();
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

void EffectsPanel::buildPluginForm(const ClipEffect &e)
{
    while (m_form->rowCount() > 0)
        m_form->removeRow(0);
    m_formClip = m_clip;
    m_formIndex = currentEffect();
    m_formKind = int(e.kind);
    m_formPlugin = pluginKey(e);
    QString error;
    vst3::Plugin *plugin = Vst3Plugins::uiInstance(e.pluginPath, e.pluginId, &error);
    if (!plugin) {
        auto *missing = new QLabel(tr("The plug-in \"%1\" is not on this computer or did not start, so the sound "
                                      "passes unchanged. Install it and press \"Find plug-ins\".\n%2")
                                       .arg(e.pluginName, QDir::toNativeSeparators(e.pluginPath)));
        missing->setObjectName(QStringLiteral("effectPluginMissing"));
        missing->setWordWrap(true);
        m_form->addRow(missing);
        m_pluginNote->clear();
        return;
    }
    int shown = 0, total = 0;
    for (const vst3::ParamInfo &p : plugin->parameters()) {
        if (p.hidden || p.readOnly || p.bypass)
            continue;
        ++total;
        if (shown >= kMaxPluginParams)
            continue;
        ++shown;
        const QString key = ClipEffect::pluginParamKey(p.id);
        QString title = QString::fromStdString(p.title);
        if (!p.units.empty())
            title = tr("%1, %2").arg(title, QString::fromStdString(p.units));
        if (p.stepCount == 1) {
            auto *box = new QCheckBox(title);
            box->setObjectName(paramObjectName(key));
            connect(box, &QCheckBox::toggled, this, [this, key](bool on) {
                if (!m_updating)
                    m_ctl->setClipEffectValue(m_clip, currentEffect(), key, on ? 1 : 0);
            });
            m_form->addRow(box);
            continue;
        }
        auto *row = new QWidget;
        auto *lay = new QHBoxLayout(row);
        lay->setContentsMargins(0, 0, 0, 0);
        auto *slider = new QSlider(Qt::Horizontal);
        slider->setObjectName(paramObjectName(key));
        // A plug-in with n steps gets exactly its n + 1 positions.
        slider->setRange(0, p.stepCount > 1 ? p.stepCount : kSliderSteps);
        auto *text = new QLabel;
        text->setObjectName(paramObjectName(key) + QStringLiteral("_text"));
        text->setMinimumWidth(64);
        lay->addWidget(slider, 1);
        lay->addWidget(text);
        const quint32 id = p.id;
        connect(slider, &QSlider::valueChanged, this, [this, key, id, slider, text, plugin](int v) {
            const double norm = double(v) / std::max(1, slider->maximum());
            text->setText(QString::fromStdString(plugin->valueText(id, norm)));
            if (!m_updating)
                m_ctl->setClipEffectValue(m_clip, currentEffect(), key, norm);
        });
        m_form->addRow(title, row);
    }
    if (total == 0) {
        auto *none = new QLabel(tr("This plug-in has no values to set here."));
        none->setWordWrap(true);
        m_form->addRow(none);
    }
    m_pluginNote->setText(total > shown ? tr("Only the first %1 of %2 values are shown. The plug-in's own window "
                                             "comes in a later version.").arg(shown).arg(total)
                                        : QString());
    m_pluginNote->setProperty("for", m_formPlugin);
}

void EffectsPanel::updatePluginValues(const ClipEffect &e)
{
    vst3::Plugin *plugin = Vst3Plugins::uiInstance(e.pluginPath, e.pluginId);
    m_pluginNote->setVisible(plugin && !m_pluginNote->text().isEmpty()
                             && m_pluginNote->property("for").toString() == pluginKey(e));
    if (!plugin)
        return;
    m_updating = true;
    for (const vst3::ParamInfo &p : plugin->parameters()) {
        const QString key = ClipEffect::pluginParamKey(p.id);
        QWidget *w = m_paramsBox->findChild<QWidget *>(paramObjectName(key));
        if (!w)
            continue;
        const double v = e.params.contains(key) ? e.value(key) : p.defaultValue;
        if (auto *box = qobject_cast<QCheckBox *>(w)) {
            box->setChecked(v >= 0.5);
        } else if (auto *slider = qobject_cast<QSlider *>(w)) {
            const int pos = int(std::lround(v * slider->maximum()));
            if (slider->value() == pos) // still refresh the text (first build)
                emit slider->valueChanged(pos);
            else
                slider->setValue(pos);
        }
    }
    m_paramsBox->setEnabled(e.enabled);
    m_updating = false;
}

void EffectsPanel::addEffect()
{
    if (m_clip.isNull())
        return;
    if (m_kind->currentData().toInt() == int(ClipEffect::Kind::Plugin)) {
        const QStringList p = m_kind->currentData(kPluginRole).toStringList();
        if (p.size() != 3)
            return;
        QString error;
        const int index = m_ctl->addClipPlugin(m_clip, p[0], p[1], p[2], &error);
        if (index >= 0) {
            m_list->setCurrentRow(index);
        } else {
            m_pluginNote->setText(tr("The plug-in did not start: %1").arg(error));
            m_pluginNote->setProperty("for", QString());
            m_pluginNote->setVisible(true);
        }
        return;
    }
    const int index = m_ctl->addClipEffect(m_clip, ClipEffect::Kind(m_kind->currentData().toInt()));
    if (index >= 0)
        m_list->setCurrentRow(index);
}

} // namespace bc
