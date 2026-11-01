#include "app/ColorPanel.h"

#include "app/ColorWheel.h"
#include "app/ProjectController.h"
#include "app/TimelinePanel.h"
#include "core/Color.h"
#include "core/Lut.h"

#include <QDoubleSpinBox>
#include <QEvent>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QSettings>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QSet>
#include <QSignalBlocker>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>

namespace bc {

QString ColorPanel::valueName(const QString &key)
{
    if (key == QLatin1String("temperature"))
        return tr("Temperature");
    if (key == QLatin1String("tint"))
        return tr("Tint");
    if (key == QLatin1String("exposure"))
        return tr("Exposure");
    if (key == QLatin1String("contrast"))
        return tr("Contrast");
    if (key == QLatin1String("highlights"))
        return tr("Highlights");
    if (key == QLatin1String("shadows"))
        return tr("Shadows");
    if (key == QLatin1String("saturation"))
        return tr("Saturation, %");
    if (key == QLatin1String("vibrance"))
        return tr("Vibrance");
    if (key == QLatin1String("lift_l"))
        return tr("Lift");
    if (key == QLatin1String("gamma_l"))
        return tr("Gamma");
    if (key == QLatin1String("gain_l"))
        return tr("Gain");
    if (key == QLatin1String("lut_amount"))
        return tr("Strength, %");
    return key;
}

ColorPanel::ColorPanel(ProjectController *controller, TimelineView *view, QWidget *parent)
    : QWidget(parent)
    , m_ctl(controller)
    , m_view(view)
    , m_header(new QLabel)
    , m_empty(new QLabel)
    , m_body(new QWidget)
    , m_balanceBox(new QGroupBox)
    , m_toneBox(new QGroupBox)
    , m_wheelsBox(new QGroupBox)
    , m_colorBox(new QGroupBox)
    , m_lutBox(new QGroupBox)
    , m_lutName(new QLabel)
    , m_lutChoose(new QToolButton)
    , m_lutRemove(new QToolButton)
    , m_reset(new QToolButton)
    , m_note(new QLabel)
    , m_wheelsReset(new QToolButton)
{
    setObjectName(QStringLiteral("colorPanel"));
    m_empty->setObjectName(QStringLiteral("colorEmpty"));
    m_reset->setObjectName(QStringLiteral("colorReset"));
    m_lutName->setObjectName(QStringLiteral("colorLutName"));
    m_lutChoose->setObjectName(QStringLiteral("colorLutChoose"));
    m_lutRemove->setObjectName(QStringLiteral("colorLutRemove"));

    // Many groups: the content scrolls when the panel is short.
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    outer->addWidget(scroll);
    auto *content = new QWidget;
    scroll->setWidget(content);
    auto *lay = new QVBoxLayout(content);
    m_header->setStyleSheet(QStringLiteral("color:#d1a54a; font-weight:600;"));
    m_header->setWordWrap(true);
    lay->addWidget(m_header);
    m_empty->setAlignment(Qt::AlignCenter);
    m_empty->setWordWrap(true);
    m_empty->setStyleSheet(QStringLiteral("color:#8a8a8a;"));
    lay->addWidget(m_empty);

    auto *body = new QVBoxLayout(m_body);
    body->setContentsMargins(0, 0, 0, 0);
    const QHash<QString, QGroupBox *> boxOf = {
        {QStringLiteral("temperature"), m_balanceBox}, {QStringLiteral("tint"), m_balanceBox},
        {QStringLiteral("exposure"), m_toneBox},       {QStringLiteral("contrast"), m_toneBox},
        {QStringLiteral("highlights"), m_toneBox},     {QStringLiteral("shadows"), m_toneBox},
        {QStringLiteral("saturation"), m_colorBox},    {QStringLiteral("vibrance"), m_colorBox},
        {QStringLiteral("lut_amount"), m_lutBox}};
    for (QGroupBox *box : {m_balanceBox, m_toneBox, m_wheelsBox, m_colorBox, m_lutBox}) {
        new QGridLayout(box);
        body->addWidget(box);
    }
    {
        // Colour wheels side by side: name, wheel, master slider and value.
        auto *grid = static_cast<QGridLayout *>(m_wheelsBox->layout());
        grid->setHorizontalSpacing(6);
        const QStringList names = ClipColor::wheels();
        for (int col = 0; col < names.size(); ++col) {
            const QString wheel = names[col];
            auto *w = new ColorWheel;
            w->setObjectName(QStringLiteral("colorWheel_") + wheel);
            grid->addWidget(w, 1, col);
            grid->setColumnStretch(col, 1);
            connect(w, &ColorWheel::moved, this, [this, wheel](double x, double y) {
                if (!m_updating && !m_clip.isNull())
                    m_ctl->setClipColorWheel(m_clip, wheel, x, y);
            });
            m_wheels.insert(wheel, w);
        }
        m_wheelsReset->setObjectName(QStringLiteral("colorWheelsReset"));
        m_wheelsReset->setToolButtonStyle(Qt::ToolButtonTextOnly);
        grid->addWidget(m_wheelsReset, 4, 0, 1, names.size(), Qt::AlignRight);
    }
    {
        // LUT file row first in its group.
        auto *grid = static_cast<QGridLayout *>(m_lutBox->layout());
        m_lutName->setWordWrap(true);
        auto *buttons = new QHBoxLayout;
        m_lutChoose->setToolButtonStyle(Qt::ToolButtonTextOnly);
        m_lutRemove->setToolButtonStyle(Qt::ToolButtonTextOnly);
        buttons->addWidget(m_lutChoose);
        buttons->addWidget(m_lutRemove);
        buttons->addStretch(1);
        grid->addWidget(m_lutName, 0, 0, 1, 3);
        grid->addLayout(buttons, 1, 0, 1, 3);
    }
    for (const EffectParamSpec &s : ClipColor::specs()) {
        const bool wheelKey = ClipColor::isWheelKey(s.key);
        if (wheelKey && !s.key.endsWith(QLatin1String("_l")))
            continue; // the point on the wheel itself
        QGroupBox *box = wheelKey ? m_wheelsBox : boxOf.value(s.key, m_colorBox);
        auto *grid = static_cast<QGridLayout *>(box->layout());
        Row row{new QLabel, new QSlider(Qt::Horizontal), new QDoubleSpinBox, s.step};
        row.slider->setObjectName(QStringLiteral("colorSlider_") + s.key);
        row.spin->setObjectName(QStringLiteral("color_") + s.key);
        row.slider->setRange(int(std::lround(s.min / s.step)), int(std::lround(s.max / s.step)));
        row.spin->setRange(s.min, s.max);
        row.spin->setSingleStep(s.step);
        row.spin->setDecimals(s.decimals);
        row.spin->setKeyboardTracking(false);
        if (wheelKey) {
            // Under its wheel: the name above, the master slider and value below.
            const int col = int(ClipColor::wheels().indexOf(s.key.chopped(2)));
            row.label->setAlignment(Qt::AlignCenter);
            row.slider->setMinimumWidth(40);
            grid->addWidget(row.label, 0, col);
            grid->addWidget(row.slider, 2, col);
            grid->addWidget(row.spin, 3, col);
        } else {
            const int r = grid->rowCount();
            grid->addWidget(row.label, r, 0);
            grid->addWidget(row.slider, r, 1);
            grid->addWidget(row.spin, r, 2);
            grid->setColumnStretch(1, 1);
        }
        const QString key = s.key;
        const double step = s.step;
        connect(row.slider, &QSlider::valueChanged, this, [this, key, step](int v) { apply(key, v * step); });
        connect(row.spin, &QDoubleSpinBox::valueChanged, this, [this, key](double v) { apply(key, v); });
        m_rows.insert(key, row);
    }
    auto *resetRow = new QHBoxLayout;
    resetRow->addStretch(1);
    m_reset->setToolButtonStyle(Qt::ToolButtonTextOnly);
    resetRow->addWidget(m_reset);
    body->addLayout(resetRow);
    m_note->setWordWrap(true);
    m_note->setStyleSheet(QStringLiteral("color:#8a8a8a;"));
    body->addWidget(m_note);
    lay->addWidget(m_body);
    lay->addStretch(1);

    connect(m_lutChoose, &QToolButton::clicked, this, &ColorPanel::chooseLut);
    connect(m_lutRemove, &QToolButton::clicked, this, [this] {
        if (!m_clip.isNull())
            m_ctl->setClipLut(m_clip, QString());
    });
    connect(m_wheelsReset, &QToolButton::clicked, this, [this] {
        if (m_clip.isNull())
            return;
        // One undo step for all three wheels.
        m_ctl->resetClipColorWheels(m_clip);
    });
    connect(m_reset, &QToolButton::clicked, this, [this] {
        if (!m_clip.isNull())
            m_ctl->resetClipColor(m_clip);
    });
    connect(m_view, &TimelineView::selectionChanged, this, &ColorPanel::refresh);
    connect(m_ctl, &ProjectController::timelineChanged, this, &ColorPanel::refresh);
    connect(m_ctl, &ProjectController::projectReset, this, &ColorPanel::refresh);
    retranslate();
}

void ColorPanel::changeEvent(QEvent *e)
{
    if (e->type() == QEvent::LanguageChange)
        retranslate();
    QWidget::changeEvent(e);
}

void ColorPanel::retranslate()
{
    m_balanceBox->setTitle(tr("White balance"));
    m_toneBox->setTitle(tr("Light"));
    m_wheelsBox->setTitle(tr("Colour wheels"));
    m_wheelsReset->setText(tr("Reset wheels"));
    for (auto it = m_wheels.begin(); it != m_wheels.end(); ++it)
        it.value()->setToolTip(tr("Drag the point towards the colour to add; double-click puts it back"));
    m_rows[QStringLiteral("lift_l")].slider->setToolTip(tr("Lift: the dark tones (black level)"));
    m_rows[QStringLiteral("gamma_l")].slider->setToolTip(tr("Gamma: the middle tones"));
    m_rows[QStringLiteral("gain_l")].slider->setToolTip(tr("Gain: the bright tones (white level)"));
    m_colorBox->setTitle(tr("Colour"));
    m_lutBox->setTitle(tr("Look (LUT)"));
    m_lutChoose->setText(tr("Choose .cube file..."));
    m_lutRemove->setText(tr("Remove"));
    for (auto it = m_rows.begin(); it != m_rows.end(); ++it)
        it->label->setText(valueName(it.key()));
    m_rows[QStringLiteral("temperature")].slider->setToolTip(tr("Left: cooler (blue), right: warmer (yellow)"));
    m_rows[QStringLiteral("tint")].slider->setToolTip(tr("Left: greener, right: more magenta"));
    m_rows[QStringLiteral("exposure")].slider->setToolTip(tr("In stops: +1 is twice as bright"));
    m_reset->setText(tr("Reset colour"));
    m_note->setText(tr("Colour correction is applied before the effects. Values do not change over time yet."));
    refresh();
}

void ColorPanel::refresh()
{
    const Timeline &tl = m_ctl->timeline();
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
    m_empty->setText(groups.isEmpty()     ? tr("Select a video clip on the timeline to correct its colour.")
                     : groups.size() > 1 ? tr("Several clips are selected. Select one clip.")
                                         : tr("Colour correction works on pictures. Select a video clip."));
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
    if (!c)
        return;
    m_updating = true;
    for (auto it = m_rows.begin(); it != m_rows.end(); ++it) {
        const double v = c->color.value(it.key());
        const QSignalBlocker b1(it->slider), b2(it->spin);
        it->slider->setValue(int(std::lround(v / it->step)));
        it->spin->setValue(v);
    }
    bool wheelsUsed = false;
    for (auto it = m_wheels.begin(); it != m_wheels.end(); ++it) {
        const double x = c->color.value(it.key() + QStringLiteral("_x"));
        const double y = c->color.value(it.key() + QStringLiteral("_y"));
        it.value()->setPoint(x, y);
        wheelsUsed |= x != 0 || y != 0 || c->color.value(it.key() + QStringLiteral("_l")) != 0;
    }
    m_wheelsReset->setEnabled(wheelsUsed);
    m_reset->setEnabled(!c->color.isNeutral() || !c->color.lut.isEmpty());
    const QString lut = c->color.lut;
    if (lut.isEmpty()) {
        m_lutName->setText(tr("No LUT"));
        m_lutName->setStyleSheet(QStringLiteral("color:#8a8a8a;"));
    } else if (!Lut::cached(lut)) {
        m_lutName->setText(tr("%1 (not found or not a valid .cube file: the picture is left as it is)")
                               .arg(QFileInfo(lut).fileName()));
        m_lutName->setStyleSheet(QStringLiteral("color:#e06c5a;"));
    } else {
        m_lutName->setText(QFileInfo(lut).fileName());
        m_lutName->setStyleSheet(QString());
    }
    m_lutName->setToolTip(QDir::toNativeSeparators(lut));
    m_lutRemove->setEnabled(!lut.isEmpty());
    const Row &amount = m_rows[QStringLiteral("lut_amount")];
    for (QWidget *w : {static_cast<QWidget *>(amount.label), static_cast<QWidget *>(amount.slider),
                       static_cast<QWidget *>(amount.spin)})
        w->setEnabled(!lut.isEmpty());
    m_body->setEnabled(!tl.tracks[tl.find(m_clip).track].locked);
    m_updating = false;
}

void ColorPanel::chooseLut()
{
    if (m_clip.isNull())
        return;
    QSettings settings;
    const QString path = QFileDialog::getOpenFileName(this, tr("Choose a LUT"),
                                                      settings.value(QStringLiteral("color/lutDir")).toString(),
                                                      tr("LUT files (*.cube)"));
    if (path.isEmpty())
        return;
    settings.setValue(QStringLiteral("color/lutDir"), QFileInfo(path).absolutePath());
    QString error;
    if (!Lut::load(path, &error)) {
        QMessageBox::warning(this, tr("Choose a LUT"),
                             tr("This file could not be read as a .cube LUT:\n%1").arg(error));
        return;
    }
    m_ctl->setClipLut(m_clip, QFileInfo(path).absoluteFilePath());
}

void ColorPanel::apply(const QString &key, double value)
{
    if (m_updating || m_clip.isNull())
        return;
    m_ctl->setClipColorValue(m_clip, key, value);
}

} // namespace bc
