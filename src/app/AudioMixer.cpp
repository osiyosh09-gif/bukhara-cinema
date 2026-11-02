#include "app/AudioMixer.h"

#include "app/ProjectController.h"

#include <QDoubleSpinBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace bc {

namespace {

constexpr int kHoldTicks = 25; // about a second at 25 updates per second
constexpr double kFallDb = 1.2; // per update: the bars fall smoothly

double toDb(float peak)
{
    return peak > 0 ? std::max(LevelMeter::kFloorDb, 20.0 * std::log10(double(peak))) : LevelMeter::kFloorDb;
}

} // namespace

// ---------------------------------------------------------------------------
// LevelMeter

LevelMeter::LevelMeter(QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
}

void LevelMeter::setLevels(double leftDb, double rightDb)
{
    const double in[2] = {leftDb, rightDb};
    for (int c = 0; c < 2; ++c) {
        m_level[c] = std::clamp(in[c], kFloorDb, kTopDb);
        if (m_level[c] >= m_hold[c] || --m_holdTicks[c] <= 0) {
            m_hold[c] = m_level[c];
            m_holdTicks[c] = kHoldTicks;
        }
    }
    update();
}

void LevelMeter::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const int h = height() - 2;
    const int barW = std::max(4, (width() - 3) / 2);
    auto yOf = [&](double db) { return 1 + int(std::lround((kTopDb - db) / (kTopDb - kFloorDb) * h)); };
    p.fillRect(rect(), QColor(16, 16, 16));
    const int yRed = yOf(-3), yYellow = yOf(-12);
    for (int c = 0; c < 2; ++c) {
        const int x = 1 + c * (barW + 1);
        const int top = yOf(m_level[c]);
        auto band = [&](int from, int to, const QColor &col) {
            const int a = std::max(from, top), b = to;
            if (b > a)
                p.fillRect(x, a, barW, b - a, col);
        };
        band(yYellow, h + 1, QColor(60, 190, 90));
        band(yRed, yYellow, QColor(220, 190, 50));
        band(1, yRed, QColor(225, 70, 55));
        if (m_hold[c] > kFloorDb) {
            const int y = yOf(m_hold[c]);
            p.fillRect(x, y, barW, 2, m_hold[c] > -3 ? QColor(225, 70, 55) : QColor(230, 230, 230));
        }
    }
    // 0 dB line.
    p.setPen(QColor(255, 255, 255, 90));
    p.drawLine(0, yOf(0), width(), yOf(0));
}

// ---------------------------------------------------------------------------
// AudioMixer

QString AudioMixer::panText(double pan)
{
    const int v = int(std::lround(pan * 100));
    if (v == 0)
        return tr("C");
    return v < 0 ? tr("L %1").arg(-v) : tr("R %1").arg(v);
}

AudioMixer::AudioMixer(ProjectController *controller, QWidget *parent)
    : QWidget(parent)
    , m_ctl(controller)
    , m_row(new QHBoxLayout)
    , m_empty(new QLabel)
    , m_note(new QLabel)
    , m_meterTimer(new QTimer(this))
{
    setObjectName(QStringLiteral("audioMixer"));
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(4, 4, 4, 4);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget;
    content->setLayout(m_row);
    m_row->setContentsMargins(0, 0, 0, 0);
    m_row->setSpacing(4);
    scroll->setWidget(content);
    outer->addWidget(scroll, 1);
    m_empty->setObjectName(QStringLiteral("mixerEmpty"));
    m_empty->setAlignment(Qt::AlignCenter);
    m_empty->setWordWrap(true);
    m_empty->setStyleSheet(QStringLiteral("color:#8a8a8a;"));
    outer->addWidget(m_empty);
    m_note->setWordWrap(true);
    m_note->setStyleSheet(QStringLiteral("color:#8a8a8a;"));
    outer->addWidget(m_note);

    m_meterTimer->setInterval(40);
    connect(m_meterTimer, &QTimer::timeout, this, &AudioMixer::updateMeters);
    connect(m_ctl, &ProjectController::timelineChanged, this, &AudioMixer::rebuild);
    connect(m_ctl, &ProjectController::projectReset, this, &AudioMixer::rebuild);
    connect(m_ctl, &ProjectController::openSequenceChanged, this, &AudioMixer::rebuild);
    rebuild();
    retranslate();
}

int AudioMixer::trackIndex(const QUuid &trackId) const
{
    const Timeline &tl = m_ctl->timeline();
    for (int i = 0; i < tl.tracks.size(); ++i)
        if (tl.tracks[i].id == trackId)
            return i;
    return -1;
}

void AudioMixer::setGain(const QUuid &trackId, double db)
{
    if (m_updating)
        return;
    if (trackId.isNull())
        m_ctl->setMasterGain(db);
    else
        m_ctl->setTrackGain(trackIndex(trackId), db);
}

AudioMixer::Strip AudioMixer::makeStrip(const QUuid &trackId, const QString &key)
{
    Strip s;
    s.trackId = trackId;
    s.box = new QWidget;
    s.box->setObjectName(QStringLiteral("mixerStrip_") + key);
    s.box->setStyleSheet(QStringLiteral("#mixerStrip_%1 { background:#262626; border-radius:4px; }").arg(key));
    auto *lay = new QVBoxLayout(s.box);
    lay->setContentsMargins(4, 4, 4, 4);
    lay->setSpacing(3);
    s.name = new QLabel;
    s.name->setAlignment(Qt::AlignCenter);
    s.name->setStyleSheet(QStringLiteral("font-weight:600;"));
    lay->addWidget(s.name);

    if (!trackId.isNull()) {
        s.pan = new QSlider(Qt::Horizontal);
        s.pan->setObjectName(QStringLiteral("mixerPan_") + key);
        s.pan->setRange(-100, 100);
        s.pan->setFixedWidth(52);
        s.panValue = new QLabel;
        s.panValue->setAlignment(Qt::AlignCenter);
        lay->addWidget(s.pan, 0, Qt::AlignHCenter);
        lay->addWidget(s.panValue);
        connect(s.pan, &QSlider::valueChanged, this, [this, trackId](int v) {
            if (!m_updating)
                m_ctl->setTrackPan(trackIndex(trackId), v / 100.0);
        });
    } else {
        // Keeps the faders of all strips at the same height.
        lay->addSpacing(QSlider(Qt::Horizontal).sizeHint().height() + QLabel(QStringLiteral("C")).sizeHint().height()
                        + 3);
    }

    auto *faders = new QHBoxLayout;
    s.meter = new LevelMeter;
    s.meter->setObjectName(QStringLiteral("mixerMeter_") + key);
    s.fader = new QSlider(Qt::Vertical);
    s.fader->setObjectName(QStringLiteral("mixerFader_") + key);
    s.fader->setRange(int(TimelineTrack::kMinGainDb * 10), int(TimelineTrack::kMaxGainDb * 10));
    s.fader->setPageStep(30);
    s.fader->setTickPosition(QSlider::TicksLeft);
    s.fader->setTickInterval(60);
    s.fader->setMinimumHeight(120);
    faders->addWidget(s.meter);
    faders->addWidget(s.fader);
    lay->addLayout(faders, 1);
    connect(s.fader, &QSlider::valueChanged, this, [this, trackId](int v) { setGain(trackId, v / 10.0); });

    s.gain = new QDoubleSpinBox;
    s.gain->setObjectName(QStringLiteral("mixerGain_") + key);
    s.gain->setRange(TimelineTrack::kMinGainDb, TimelineTrack::kMaxGainDb);
    s.gain->setDecimals(1);
    s.gain->setSingleStep(0.5);
    s.gain->setKeyboardTracking(false);
    s.gain->setButtonSymbols(QAbstractSpinBox::NoButtons);
    s.gain->setAlignment(Qt::AlignCenter);
    s.gain->setFixedWidth(56);
    lay->addWidget(s.gain, 0, Qt::AlignHCenter);
    connect(s.gain, &QDoubleSpinBox::valueChanged, this, [this, trackId](double v) { setGain(trackId, v); });

    if (!trackId.isNull()) {
        auto *buttons = new QHBoxLayout;
        s.mute = new QToolButton;
        s.mute->setObjectName(QStringLiteral("mixerMute_") + key);
        s.mute->setText(QStringLiteral("M"));
        s.mute->setCheckable(true);
        s.mute->setStyleSheet(QStringLiteral("QToolButton:checked { background:#b5523b; color:white; }"));
        s.solo = new QToolButton;
        s.solo->setObjectName(QStringLiteral("mixerSolo_") + key);
        s.solo->setText(QStringLiteral("S"));
        s.solo->setCheckable(true);
        s.solo->setStyleSheet(QStringLiteral("QToolButton:checked { background:#c9a227; color:black; }"));
        buttons->addWidget(s.mute);
        buttons->addWidget(s.solo);
        lay->addLayout(buttons);
        connect(s.mute, &QToolButton::toggled, this, [this, trackId](bool on) {
            if (!m_updating)
                m_ctl->setTrackFlag(trackIndex(trackId), QStringLiteral("muted"), on);
        });
        connect(s.solo, &QToolButton::toggled, this, [this, trackId](bool on) {
            if (!m_updating)
                m_ctl->setTrackFlag(trackIndex(trackId), QStringLiteral("solo"), on);
        });
    } else {
        lay->addSpacing(QToolButton().sizeHint().height());
    }
    return s;
}

void AudioMixer::rebuild()
{
    const Timeline &tl = m_ctl->timeline();
    QList<QUuid> ids;
    for (const TimelineTrack &t : tl.tracks)
        if (t.kind == Timeline::Kind::Audio)
            ids << t.id;
    if (ids != m_trackIds || m_strips.isEmpty()) {
        for (const Strip &s : m_strips)
            delete s.box;
        m_strips.clear();
        while (QLayoutItem *item = m_row->takeAt(0))
            delete item;
        m_trackIds = ids;
        for (const QUuid &id : ids) {
            const int index = trackIndex(id);
            m_strips << makeStrip(id, tl.trackName(index));
            m_row->addWidget(m_strips.last().box);
        }
        m_row->addStretch(1);
        auto *line = new QFrame;
        line->setFrameShape(QFrame::VLine);
        line->setStyleSheet(QStringLiteral("color:#444;"));
        m_row->addWidget(line);
        m_strips << makeStrip(QUuid(), QStringLiteral("master"));
        m_row->addWidget(m_strips.last().box);
        retranslate();
    }
    refresh();
}

void AudioMixer::refresh()
{
    const Timeline &tl = m_ctl->timeline();
    m_updating = true;
    for (Strip &s : m_strips) {
        const QSignalBlocker b1(s.fader), b2(s.gain);
        if (s.trackId.isNull()) {
            s.fader->setValue(int(std::lround(tl.masterGainDb * 10)));
            s.gain->setValue(tl.masterGainDb);
            continue;
        }
        const int index = trackIndex(s.trackId);
        if (index < 0)
            continue;
        const TimelineTrack &t = tl.tracks[index];
        s.name->setText(tl.trackName(index));
        s.fader->setValue(int(std::lround(t.gainDb * 10)));
        s.gain->setValue(t.gainDb);
        const QSignalBlocker b3(s.pan), b4(s.mute), b5(s.solo);
        s.pan->setValue(int(std::lround(t.pan * 100)));
        s.panValue->setText(panText(t.pan));
        s.mute->setChecked(t.muted);
        s.solo->setChecked(t.solo);
    }
    m_empty->setVisible(m_trackIds.isEmpty());
    m_updating = false;
}

void AudioMixer::updateMeters()
{
    for (Strip &s : m_strips) {
        const EngineServices::MeterLevel m = m_ctl->meterLevel(s.trackId);
        const bool fresh = m.serial != s.serial && m_ctl->isPlaying();
        s.serial = m.serial;
        const double in[2] = {fresh ? toDb(m.left) : LevelMeter::kFloorDb,
                              fresh ? toDb(m.right) : LevelMeter::kFloorDb};
        for (int c = 0; c < 2; ++c)
            s.shown[c] = std::max(in[c], s.shown[c] - kFallDb);
        s.meter->setLevels(s.shown[0], s.shown[1]);
    }
}

void AudioMixer::showEvent(QShowEvent *e)
{
    m_meterTimer->start();
    QWidget::showEvent(e);
}

void AudioMixer::hideEvent(QHideEvent *e)
{
    m_meterTimer->stop();
    QWidget::hideEvent(e);
}

void AudioMixer::changeEvent(QEvent *e)
{
    if (e->type() == QEvent::LanguageChange)
        retranslate();
    QWidget::changeEvent(e);
}

void AudioMixer::retranslate()
{
    for (Strip &s : m_strips) {
        s.fader->setToolTip(tr("Volume, dB (0 = unchanged)"));
        s.gain->setSuffix(tr(" dB"));
        s.meter->setToolTip(tr("Level, dBFS: keep the loudest parts below 0 (red)"));
        if (s.trackId.isNull()) {
            s.name->setText(tr("Master"));
            continue;
        }
        s.pan->setToolTip(tr("Balance: left / right"));
        s.mute->setToolTip(tr("Mute the track"));
        s.solo->setToolTip(tr("Solo: hear only the solo tracks"));
        const int index = trackIndex(s.trackId);
        if (index >= 0)
            s.panValue->setText(panText(m_ctl->timeline().tracks[index].pan));
    }
    m_empty->setText(tr("This sequence has no audio tracks."));
    m_note->setText(tr("Track faders and balance act after the clip volume. The meters move while the Program monitor plays."));
}

} // namespace bc
