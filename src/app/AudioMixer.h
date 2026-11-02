#pragma once

#include <QList>
#include <QUuid>
#include <QWidget>

class QDoubleSpinBox;
class QHBoxLayout;
class QLabel;
class QSlider;
class QTimer;
class QToolButton;

namespace bc {

class ProjectController;

// Two vertical level bars (left, right) in dBFS, -60 .. +6, with a short
// peak hold. Green below -12 dB, yellow up to -3 dB, red above.
class LevelMeter : public QWidget
{
    Q_OBJECT
public:
    explicit LevelMeter(QWidget *parent = nullptr);

    static constexpr double kFloorDb = -60;
    static constexpr double kTopDb = 6;

    void setLevels(double leftDb, double rightDb);
    double left() const { return m_level[0]; }
    double right() const { return m_level[1]; }

    QSize sizeHint() const override { return {22, 160}; }
    QSize minimumSizeHint() const override { return {16, 80}; }

protected:
    void paintEvent(QPaintEvent *e) override;

private:
    double m_level[2] = {kFloorDb, kFloorDb};
    double m_hold[2] = {kFloorDb, kFloorDb};
    int m_holdTicks[2] = {0, 0};
};

// "Audio" tab of the right panel (phase 10a): one strip per audio track of
// the open sequence (balance, meter, fader in dB, mute, solo) and a master
// strip. Every change goes through the controller (undoable; a fader drag is
// one step). Meters move while the Program monitor plays.
class AudioMixer : public QWidget
{
    Q_OBJECT
public:
    explicit AudioMixer(ProjectController *controller, QWidget *parent = nullptr);

    static QString panText(double pan); // "L 30", "C", "R 100"
    // Reads the meters once (the timer does it 25 times a second while visible).
    void updateMeters();

protected:
    void changeEvent(QEvent *e) override;
    void showEvent(QShowEvent *e) override;
    void hideEvent(QHideEvent *e) override;

private:
    struct Strip
    {
        QUuid trackId; // null = master
        QWidget *box = nullptr;
        QLabel *name = nullptr;
        QSlider *pan = nullptr;
        QLabel *panValue = nullptr;
        LevelMeter *meter = nullptr;
        QSlider *fader = nullptr;
        QDoubleSpinBox *gain = nullptr;
        QToolButton *mute = nullptr;
        QToolButton *solo = nullptr;
        qint64 serial = 0;
        double shown[2] = {LevelMeter::kFloorDb, LevelMeter::kFloorDb};
    };

    void retranslate();
    void rebuild();  // when the audio tracks change
    void refresh();  // values from the open sequence
    Strip makeStrip(const QUuid &trackId, const QString &key);
    int trackIndex(const QUuid &trackId) const;
    void setGain(const QUuid &trackId, double db);

    ProjectController *m_ctl;
    QList<Strip> m_strips; // audio tracks, then the master
    QList<QUuid> m_trackIds;
    QHBoxLayout *m_row;
    QLabel *m_empty;
    QLabel *m_note;
    QTimer *m_meterTimer;
    bool m_updating = false;
};

} // namespace bc
