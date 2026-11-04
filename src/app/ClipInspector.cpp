#include "app/ClipInspector.h"

#include "app/ProjectController.h"
#include "app/TimelinePanel.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>

namespace bc {

ClipInspector::ClipInspector(ProjectController *controller, TimelineView *view, QWidget *parent)
    : QWidget(parent)
    , m_ctl(controller)
    , m_view(view)
    , m_header(new QLabel(this))
    , m_empty(new QLabel(this))
    , m_videoNote(new QLabel(this))
    , m_titleBox(new QGroupBox(this))
    , m_textLabel(new QLabel)
    , m_text(new QPlainTextEdit)
    , m_sizeLabel(new QLabel)
    , m_size(new QSpinBox)
    , m_colorLabel(new QLabel)
    , m_color(new QToolButton)
    , m_posLabel(new QLabel)
    , m_position(new QComboBox)
    , m_box(new QCheckBox)
    , m_audioBox(new QGroupBox(this))
    , m_gainLabel(new QLabel)
    , m_gain(new QDoubleSpinBox)
    , m_gainSlider(new QSlider(Qt::Horizontal))
    , m_fadeInLabel(new QLabel)
    , m_fadeIn(new QDoubleSpinBox)
    , m_fadeOutLabel(new QLabel)
    , m_fadeOut(new QDoubleSpinBox)
    , m_motionBox(new QGroupBox(this))
    , m_posXLabel(new QLabel)
    , m_posX(new QDoubleSpinBox)
    , m_posYLabel(new QLabel)
    , m_posY(new QDoubleSpinBox)
    , m_scaleLabel(new QLabel)
    , m_scale(new QDoubleSpinBox)
    , m_rotationLabel(new QLabel)
    , m_rotation(new QDoubleSpinBox)
    , m_opacityLabel(new QLabel)
    , m_opacity(new QDoubleSpinBox)
    , m_animate(new QCheckBox)
    , m_prevKey(new QToolButton)
    , m_key(new QToolButton)
    , m_nextKey(new QToolButton)
    , m_resetMotion(new QToolButton)
    , m_keyNote(new QLabel)
    , m_transBox(new QGroupBox(this))
    , m_headLabel(new QLabel)
    , m_headKind(new QComboBox)
    , m_headLen(new QDoubleSpinBox)
    , m_tailLabel(new QLabel)
    , m_tailKind(new QComboBox)
    , m_tailLen(new QDoubleSpinBox)
    , m_transNote(new QLabel)
{
    setObjectName(QStringLiteral("clipInspector"));
    m_empty->setObjectName(QStringLiteral("inspectorEmpty"));
    m_text->setObjectName(QStringLiteral("titleText"));
    m_size->setObjectName(QStringLiteral("titleSize"));
    m_color->setObjectName(QStringLiteral("titleColor"));
    m_position->setObjectName(QStringLiteral("titlePosition"));
    m_box->setObjectName(QStringLiteral("titleBox"));
    m_gain->setObjectName(QStringLiteral("clipGain"));
    m_gainSlider->setObjectName(QStringLiteral("clipGainSlider"));
    m_fadeIn->setObjectName(QStringLiteral("clipFadeIn"));
    m_fadeOut->setObjectName(QStringLiteral("clipFadeOut"));
    m_posX->setObjectName(QStringLiteral("motionX"));
    m_posY->setObjectName(QStringLiteral("motionY"));
    m_scale->setObjectName(QStringLiteral("motionScale"));
    m_rotation->setObjectName(QStringLiteral("motionRotation"));
    m_opacity->setObjectName(QStringLiteral("motionOpacity"));
    m_animate->setObjectName(QStringLiteral("motionAnimate"));
    m_prevKey->setObjectName(QStringLiteral("motionPrevKey"));
    m_key->setObjectName(QStringLiteral("motionKey"));
    m_nextKey->setObjectName(QStringLiteral("motionNextKey"));
    m_resetMotion->setObjectName(QStringLiteral("motionReset"));
    m_headKind->setObjectName(QStringLiteral("transitionHeadKind"));
    m_headLen->setObjectName(QStringLiteral("transitionHeadLength"));
    m_tailKind->setObjectName(QStringLiteral("transitionTailKind"));
    m_tailLen->setObjectName(QStringLiteral("transitionTailLength"));

    auto *lay = new QVBoxLayout(this);
    m_header->setStyleSheet(QStringLiteral("color:#d1a54a; font-weight:600;"));
    m_header->setWordWrap(true);
    lay->addWidget(m_header);
    m_empty->setAlignment(Qt::AlignCenter);
    m_empty->setWordWrap(true);
    m_empty->setStyleSheet(QStringLiteral("color:#8a8a8a;"));
    lay->addWidget(m_empty);
    m_videoNote->setWordWrap(true);
    m_videoNote->setStyleSheet(QStringLiteral("color:#8a8a8a;"));
    lay->addWidget(m_videoNote);

    // Title
    auto *tf = new QFormLayout(m_titleBox);
    m_text->setMaximumHeight(90);
    m_text->setTabChangesFocus(true);
    tf->addRow(m_textLabel, m_text);
    m_size->setRange(8, 400);
    tf->addRow(m_sizeLabel, m_size);
    m_color->setFixedSize(48, 22);
    tf->addRow(m_colorLabel, m_color);
    m_position->addItems({QString(), QString(), QString()});
    tf->addRow(m_posLabel, m_position);
    tf->addRow(QString(), m_box);
    lay->addWidget(m_titleBox);

    // Audio
    auto *af = new QFormLayout(m_audioBox);
    m_gain->setRange(-60, 12);
    m_gain->setSingleStep(0.5);
    m_gain->setDecimals(1);
    m_gain->setSuffix(QStringLiteral(" dB"));
    m_gainSlider->setRange(-600, 120);
    af->addRow(m_gainLabel, m_gain);
    af->addRow(QString(), m_gainSlider);
    for (QDoubleSpinBox *f : {m_fadeIn, m_fadeOut}) {
        f->setDecimals(2);
        f->setSingleStep(0.25);
    }
    af->addRow(m_fadeInLabel, m_fadeIn);
    af->addRow(m_fadeOutLabel, m_fadeOut);
    lay->addWidget(m_audioBox);

    // Motion
    auto *mf = new QFormLayout(m_motionBox);
    for (QDoubleSpinBox *b : {m_posX, m_posY}) {
        b->setRange(-20000, 20000);
        b->setDecimals(0);
        b->setSuffix(QStringLiteral(" px"));
    }
    m_scale->setRange(0, 1000);
    m_scale->setDecimals(1);
    m_scale->setSuffix(QStringLiteral(" %"));
    m_rotation->setRange(-3600, 3600);
    m_rotation->setDecimals(1);
    m_rotation->setSuffix(QStringLiteral("\u00b0"));
    m_opacity->setRange(0, 100);
    m_opacity->setDecimals(0);
    m_opacity->setSuffix(QStringLiteral(" %"));
    for (QDoubleSpinBox *b : {m_posX, m_posY, m_scale, m_rotation, m_opacity}) {
        b->setKeyboardTracking(false);
        b->setAccelerated(true);
    }
    mf->addRow(m_posXLabel, m_posX);
    mf->addRow(m_posYLabel, m_posY);
    mf->addRow(m_scaleLabel, m_scale);
    mf->addRow(m_rotationLabel, m_rotation);
    mf->addRow(m_opacityLabel, m_opacity);
    auto *keys = new QHBoxLayout;
    keys->setContentsMargins(0, 0, 0, 0);
    m_prevKey->setText(QStringLiteral("\u25c0"));
    m_key->setText(QStringLiteral("\u25c6"));
    m_key->setCheckable(true);
    m_nextKey->setText(QStringLiteral("\u25b6"));
    keys->addWidget(m_animate);
    keys->addStretch();
    keys->addWidget(m_prevKey);
    keys->addWidget(m_key);
    keys->addWidget(m_nextKey);
    mf->addRow(keys);
    m_keyNote->setWordWrap(true);
    m_keyNote->setStyleSheet(QStringLiteral("color:#8a8a8a;"));
    mf->addRow(m_keyNote);
    mf->addRow(QString(), m_resetMotion);
    lay->addWidget(m_motionBox);

    // Transitions
    auto *tf2 = new QFormLayout(m_transBox);
    for (QDoubleSpinBox *b : {m_headLen, m_tailLen}) {
        b->setDecimals(2);
        b->setSingleStep(0.25);
        b->setKeyboardTracking(false);
    }
    tf2->addRow(m_headLabel, m_headKind);
    tf2->addRow(QString(), m_headLen);
    tf2->addRow(m_tailLabel, m_tailKind);
    tf2->addRow(QString(), m_tailLen);
    m_transNote->setWordWrap(true);
    m_transNote->setStyleSheet(QStringLiteral("color:#8a8a8a;"));
    tf2->addRow(m_transNote);
    lay->addWidget(m_transBox);
    lay->addStretch();

    connect(m_text, &QPlainTextEdit::textChanged, this, &ClipInspector::applyTitle);
    connect(m_size, &QSpinBox::valueChanged, this, &ClipInspector::applyTitle);
    connect(m_position, &QComboBox::currentIndexChanged, this, &ClipInspector::applyTitle);
    connect(m_box, &QCheckBox::toggled, this, &ClipInspector::applyTitle);
    connect(m_color, &QToolButton::clicked, this, &ClipInspector::pickColor);
    connect(m_gain, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        const QSignalBlocker b(m_gainSlider);
        m_gainSlider->setValue(int(std::lround(v * 10)));
        applyAudio();
    });
    connect(m_gainSlider, &QSlider::valueChanged, this, [this](int v) { m_gain->setValue(v / 10.0); });
    connect(m_fadeIn, &QDoubleSpinBox::valueChanged, this, &ClipInspector::applyAudio);
    connect(m_fadeOut, &QDoubleSpinBox::valueChanged, this, &ClipInspector::applyAudio);

    for (QDoubleSpinBox *b : {m_posX, m_posY, m_scale, m_rotation, m_opacity})
        connect(b, &QDoubleSpinBox::valueChanged, this, &ClipInspector::applyMotion);
    connect(m_animate, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_updating && !motionClip().isNull())
            m_ctl->setClipAnimated(motionClip(), on, m_ctl->playhead());
    });
    connect(m_key, &QToolButton::clicked, this, [this] {
        if (!motionClip().isNull())
            m_ctl->toggleClipKey(motionClip(), m_ctl->playhead());
        refreshMotion();
    });
    auto jump = [this](bool next) {
        const qint64 f = m_ctl->clipKeyNear(motionClip(), m_ctl->playhead(), next);
        if (f >= 0)
            m_ctl->setPlayhead(f);
    };
    connect(m_prevKey, &QToolButton::clicked, this, [jump] { jump(false); });
    connect(m_nextKey, &QToolButton::clicked, this, [jump] { jump(true); });
    connect(m_resetMotion, &QToolButton::clicked, this, [this] {
        if (!motionClip().isNull())
            m_ctl->resetClipMotion(motionClip());
    });
    connect(m_ctl, &ProjectController::playheadChanged, this, &ClipInspector::refreshMotion);
    connect(m_headKind, &QComboBox::currentIndexChanged, this, [this] { applyTransition(true); });
    connect(m_headLen, &QDoubleSpinBox::valueChanged, this, [this] { applyTransition(true); });
    connect(m_tailKind, &QComboBox::currentIndexChanged, this, [this] { applyTransition(false); });
    connect(m_tailLen, &QDoubleSpinBox::valueChanged, this, [this] { applyTransition(false); });
    connect(m_view, &TimelineView::selectionChanged, this, &ClipInspector::refresh);
    connect(m_ctl, &ProjectController::timelineChanged, this, &ClipInspector::refresh);
    connect(m_ctl, &ProjectController::projectReset, this, &ClipInspector::refresh);
    retranslate();
}

void ClipInspector::changeEvent(QEvent *e)
{
    if (e->type() == QEvent::LanguageChange)
        retranslate();
    QWidget::changeEvent(e);
}

void ClipInspector::retranslate()
{
    m_titleBox->setTitle(tr("Text"));
    m_textLabel->setText(tr("Text:"));
    m_sizeLabel->setText(tr("Size:"));
    m_size->setSuffix(QStringLiteral(" px"));
    m_colorLabel->setText(tr("Color:"));
    m_color->setToolTip(tr("Choose the text color"));
    m_posLabel->setText(tr("Place:"));
    m_position->setItemText(0, tr("Top"));
    m_position->setItemText(1, tr("Center"));
    m_position->setItemText(2, tr("Bottom"));
    m_box->setText(tr("Dark box behind the text"));
    m_audioBox->setTitle(tr("Audio"));
    m_gainLabel->setText(tr("Volume:"));
    m_fadeInLabel->setText(tr("Fade in:"));
    m_fadeOutLabel->setText(tr("Fade out:"));
    m_fadeIn->setSuffix(tr(" s"));
    m_fadeOut->setSuffix(tr(" s"));
    m_videoNote->setText(tr("Speed: Clip > Speed / duration (Ctrl+R)."));
    m_motionBox->setTitle(tr("Motion"));
    m_posXLabel->setText(tr("Position X:"));
    m_posYLabel->setText(tr("Position Y:"));
    m_scaleLabel->setText(tr("Scale:"));
    m_rotationLabel->setText(tr("Rotation:"));
    m_opacityLabel->setText(tr("Opacity:"));
    m_animate->setText(tr("Animate"));
    m_animate->setToolTip(tr("Keyframes: the values change over time. Changing a value sets a keyframe at the playhead."));
    m_prevKey->setToolTip(tr("Previous keyframe"));
    m_key->setToolTip(tr("Add or remove a keyframe at the playhead"));
    m_nextKey->setToolTip(tr("Next keyframe"));
    m_transBox->setTitle(tr("Transitions"));
    m_headLabel->setText(tr("Start:"));
    m_tailLabel->setText(tr("End:"));
    m_headLen->setSuffix(tr(" s"));
    m_tailLen->setSuffix(tr(" s"));
    m_transNote->setText(tr("Between two touching clips the transition mixes them; at a free end the clip fades. "
                            "Ctrl+D adds a 1-second one at the edit nearest the playhead."));
    m_transAudio = !m_transAudio; // rebuild the lists in the new language
    m_resetMotion->setText(tr("Reset"));
    m_resetMotion->setToolTip(tr("Put the picture back in place: no move, scale, rotation or keyframes"));
    refresh();
}

void ClipInspector::refresh()
{
    const Timeline &tl = m_ctl->timeline();
    m_title = m_audio = m_video = QUuid();
    const QList<QUuid> sel = m_view->selection();
    // One clip, or one linked video + audio pair.
    QSet<QUuid> groups;
    for (const QUuid &id : sel) {
        const TimelineClip *c = tl.clip(id);
        if (!c)
            continue;
        groups.insert(c->linkId.isNull() ? c->id : c->linkId);
        if (c->isTitle())
            m_title = id;
        else if (tl.kindOf(id) == Timeline::Kind::Audio)
            m_audio = id;
        else
            m_video = id;
    }
    const bool one = groups.size() == 1;
    if (!one)
        m_title = m_audio = m_video = QUuid();

    m_empty->setVisible(!one);
    m_empty->setText(groups.isEmpty() ? tr("Select a clip on the timeline to see its settings.")
                                      : tr("Several clips are selected. Select one clip."));
    m_titleBox->setVisible(!m_title.isNull());
    m_audioBox->setVisible(!m_audio.isNull());
    m_videoNote->setVisible(!m_video.isNull());
    m_motionBox->setVisible(!motionClip().isNull());
    m_transBox->setVisible(!transitionClip().isNull());

    QString header;
    if (const TimelineClip *c = tl.clip(m_title)) {
        header = tr("Title");
        m_updating = true;
        if (m_text->toPlainText() != c->title.text)
            m_text->setPlainText(c->title.text);
        {
            const QSignalBlocker b1(m_size), b2(m_position), b3(m_box);
            m_size->setValue(c->title.size);
            m_position->setCurrentIndex(int(c->title.position));
            m_box->setChecked(c->title.box);
        }
        showColor(c->title.color);
        m_updating = false;
        const bool locked = tl.tracks[tl.find(m_title).track].locked;
        m_titleBox->setEnabled(!locked);
    }
    const QUuid named = !m_video.isNull() ? m_video : m_audio;
    if (const TimelineClip *c = tl.clip(named)) {
        if (c->isNested())
            header = m_ctl->sequenceName(c->nestId);
        else if (const MediaItem *m = m_ctl->project().item(c->mediaId))
            header = m->name;
    }
    if (const TimelineClip *c = tl.clip(m_audio)) {
        const double fps = m_ctl->project().sequence.fps() > 0 ? m_ctl->project().sequence.fps() : 25.0;
        const QSignalBlocker b1(m_gain), b2(m_gainSlider), b3(m_fadeIn), b4(m_fadeOut);
        m_gain->setValue(c->gainDb);
        m_gainSlider->setValue(int(std::lround(c->gainDb * 10)));
        m_fadeIn->setRange(0, c->length / fps);
        m_fadeOut->setRange(0, c->length / fps);
        m_fadeIn->setValue(c->fadeIn / fps);
        m_fadeOut->setValue(c->fadeOut / fps);
        m_audioBox->setEnabled(!tl.tracks[tl.find(m_audio).track].locked);
    }
    m_header->setText(header);
    m_header->setVisible(!header.isEmpty());
    refreshMotion();
    refreshTransitions();
}

void ClipInspector::refreshTransitions()
{
    const QUuid id = transitionClip();
    const Timeline &tl = m_ctl->timeline();
    const TimelineClip *c = tl.clip(id);
    if (!c)
        return;
    const bool audio = tl.kindOf(id) == Timeline::Kind::Audio;
    m_updating = true;
    const QSignalBlocker b1(m_headKind), b2(m_tailKind), b3(m_headLen), b4(m_tailLen);
    if (audio != m_transAudio || m_headKind->count() == 0) {
        m_transAudio = audio;
        for (QComboBox *box : {m_headKind, m_tailKind}) {
            box->clear();
            box->addItem(tr("None"), int(ClipTransition::Kind::None));
            if (audio) {
                box->addItem(tr("Crossfade"), int(ClipTransition::Kind::Dissolve));
            } else {
                box->addItem(tr("Cross dissolve"), int(ClipTransition::Kind::Dissolve));
                box->addItem(tr("Dip to black"), int(ClipTransition::Kind::DipBlack));
                box->addItem(tr("Dip to white"), int(ClipTransition::Kind::DipWhite));
                box->addItem(tr("Wipe from the left"), int(ClipTransition::Kind::WipeLeft));
                box->addItem(tr("Wipe from the right"), int(ClipTransition::Kind::WipeRight));
            }
        }
    }
    const double fps = m_ctl->project().sequence.fps() > 0 ? m_ctl->project().sequence.fps() : 25.0;
    for (bool head : {true, false}) {
        const ClipTransition t = tl.transitionAt(id, head);
        QComboBox *kind = head ? m_headKind : m_tailKind;
        QDoubleSpinBox *len = head ? m_headLen : m_tailLen;
        kind->setCurrentIndex(std::max(0, kind->findData(int(t.kind))));
        len->setRange(1.0 / fps, std::max(1.0 / fps, c->length / fps));
        len->setValue(t.isNone() ? std::min(1.0, c->length / fps) : t.length / fps);
        len->setEnabled(!t.isNone());
    }
    m_updating = false;
    m_transBox->setEnabled(!tl.tracks[tl.find(id).track].locked);
}

void ClipInspector::applyTransition(bool head)
{
    const QUuid id = transitionClip();
    if (m_updating || id.isNull())
        return;
    const double fps = m_ctl->project().sequence.fps() > 0 ? m_ctl->project().sequence.fps() : 25.0;
    QComboBox *kind = head ? m_headKind : m_tailKind;
    QDoubleSpinBox *len = head ? m_headLen : m_tailLen;
    ClipTransition t;
    t.kind = ClipTransition::Kind(kind->currentData().toInt());
    t.length = qint64(std::llround(len->value() * fps));
    m_ctl->setClipTransition(id, head, t);
}

void ClipInspector::refreshMotion()
{
    const QUuid id = motionClip();
    const Timeline &tl = m_ctl->timeline();
    const TimelineClip *c = tl.clip(id);
    if (!c)
        return;
    const SequenceSettings &seq = m_ctl->project().sequence;
    const qint64 ph = m_ctl->playhead();
    const ClipTransform t = m_ctl->clipTransformAt(id, ph);
    m_updating = true;
    {
        const QSignalBlocker b1(m_posX), b2(m_posY), b3(m_scale), b4(m_rotation), b5(m_opacity), b6(m_animate);
        m_posX->setValue(std::round(t.x * seq.width));
        m_posY->setValue(std::round(t.y * seq.height));
        m_scale->setValue(t.scale);
        m_rotation->setValue(t.rotation);
        m_opacity->setValue(t.opacity);
        m_animate->setChecked(c->motion.animated());
    }
    m_updating = false;
    const bool animated = c->motion.animated();
    const bool inside = c->covers(ph);
    m_key->setEnabled(inside);
    m_key->setChecked(m_ctl->hasClipKey(id, ph));
    m_prevKey->setEnabled(animated && m_ctl->clipKeyNear(id, ph, false) >= 0);
    m_nextKey->setEnabled(animated && m_ctl->clipKeyNear(id, ph, true) >= 0);
    m_resetMotion->setEnabled(c->motion != ClipMotion());
    m_keyNote->setVisible(animated);
    m_keyNote->setText(inside ? tr("Keyframes: %1. Changes set a keyframe at the playhead.").arg(c->motion.keys.size())
                              : tr("Move the playhead over the clip to change its keyframes."));
    m_motionBox->setEnabled(!tl.tracks[tl.find(id).track].locked);
}

void ClipInspector::applyMotion()
{
    const QUuid id = motionClip();
    if (m_updating || id.isNull())
        return;
    const SequenceSettings &seq = m_ctl->project().sequence;
    ClipTransform t;
    t.x = seq.width > 0 ? m_posX->value() / seq.width : 0;
    t.y = seq.height > 0 ? m_posY->value() / seq.height : 0;
    t.scale = m_scale->value();
    t.rotation = m_rotation->value();
    t.opacity = m_opacity->value();
    m_ctl->setClipTransform(id, t, m_ctl->playhead());
}

void ClipInspector::applyTitle()
{
    if (m_updating || m_title.isNull())
        return;
    const TimelineClip *c = m_ctl->timeline().clip(m_title);
    if (!c)
        return;
    TitleData t = c->title;
    t.text = m_text->toPlainText();
    t.size = m_size->value();
    t.color = m_colorValue.isEmpty() ? t.color : m_colorValue;
    t.position = TitleData::Position(std::clamp(m_position->currentIndex(), 0, 2));
    t.box = m_box->isChecked();
    m_ctl->setClipTitle(m_title, t);
}

void ClipInspector::applyAudio()
{
    if (m_audio.isNull())
        return;
    const double fps = m_ctl->project().sequence.fps() > 0 ? m_ctl->project().sequence.fps() : 25.0;
    m_ctl->setClipAudio(m_audio, m_gain->value(), qint64(std::llround(m_fadeIn->value() * fps)),
                        qint64(std::llround(m_fadeOut->value() * fps)));
}

void ClipInspector::pickColor()
{
    const QColor c = QColorDialog::getColor(QColor(m_colorValue), this, tr("Text color"));
    if (!c.isValid())
        return;
    showColor(c.name());
    applyTitle();
}

void ClipInspector::showColor(const QString &color)
{
    m_colorValue = color;
    m_color->setStyleSheet(QStringLiteral("QToolButton { background:%1; border:1px solid #555; }").arg(color));
}

void ClipInspector::focusTitleText()
{
    if (m_title.isNull())
        return;
    m_text->setFocus();
    m_text->selectAll();
}

void ClipInspector::focusGain()
{
    if (!m_audio.isNull())
        m_gain->setFocus();
}

} // namespace bc
