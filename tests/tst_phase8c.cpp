// Phase 8 (part 3) acceptance tests: chroma key and masks (pixel work,
// engine compositing over a lower track, Effects panel).
#include "app/EffectsPanel.h"
#include "app/MainWindow.h"
#include "app/ProjectController.h"
#include "app/TimelinePanel.h"
#include "core/Effects.h"
#include "core/MediaCache.h"
#include "engine/TimelinePlayer.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QSettings>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QToolButton>
#include <QUndoStack>
#include <QtTest>

using namespace bc;
using EK = ClipEffect::Kind;

namespace {

void waitIdle(ProjectController &ctl)
{
    for (int i = 0; i < 100; ++i) {
        ctl.waitForBackgroundWork();
        bool probing = false;
        for (const MediaItem &m : ctl.project().items)
            probing |= ctl.isProbing(m.id);
        if (!probing)
            return;
        QTest::qWait(50);
    }
}

QColor at(const QImage &img, double fx, double fy = 0.5)
{
    return img.pixelColor(int(fx * (img.width() - 1)), int(fy * (img.height() - 1)));
}

TimelineClip clipOf(const QUuid &media, qint64 start, qint64 length)
{
    TimelineClip c;
    c.id = QUuid::createUuid();
    c.mediaId = media;
    c.start = start;
    c.length = length;
    return c;
}

QImage render(const Project &p, qint64 frame)
{
    TimelinePlayer player;
    player.setPreviewLimit(QSize(160, 90));
    player.setProject(p);
    return player.renderFrame(frame);
}

std::vector<uint8_t> pixels(const QList<QColor> &colors)
{
    std::vector<uint8_t> img;
    for (const QColor &c : colors)
        img.insert(img.end(), {uint8_t(c.red()), uint8_t(c.green()), uint8_t(c.blue()), 255});
    return img;
}

std::vector<uint8_t> flat(int w, int h, QColor c = Qt::white)
{
    QList<QColor> list;
    for (int i = 0; i < w * h; ++i)
        list << c;
    return pixels(list);
}

int alphaAt(const std::vector<uint8_t> &img, int w, int x, int y)
{
    return img[(size_t(y) * w + x) * 4 + 3];
}

} // namespace

class TestPhase8c : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    QString m_green, m_blue;

    QUuid import(ProjectController &ctl, const QString &path)
    {
        ctl.importFiles({path});
        waitIdle(ctl);
        for (const MediaItem &m : ctl.project().items)
            if (m.path == path)
                return m.id;
        return {};
    }

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("BukharaCinemaTests"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_phase8c"));
        QStandardPaths::setTestModeEnabled(true);
        QSettings().clear();
        QSettings().setValue(QStringLiteral("proxy/auto"), false);
        QDir(MediaCache::root()).removeRecursively();
        QVERIFY(m_dir.isValid());
        // A "green screen" shot: green background, a red square in the middle.
        m_green = m_dir.filePath(QStringLiteral("greenscreen.png"));
        QImage img(160, 90, QImage::Format_RGB32);
        img.fill(QColor(30, 200, 40));
        for (int y = 30; y < 60; ++y)
            for (int x = 60; x < 100; ++x)
                img.setPixelColor(x, y, QColor(220, 30, 30));
        QVERIFY(img.save(m_green));
        m_blue = m_dir.filePath(QStringLiteral("blue.png"));
        img.fill(QColor(0, 0, 255));
        QVERIFY(img.save(m_blue));
    }

    void chromaPixels()
    {
        // green, a darker green, red, grey, white, a green-blue mix far from green
        auto img = pixels({QColor(30, 200, 40), QColor(20, 120, 30), QColor(220, 30, 30), QColor(128, 128, 128),
                           QColor(255, 255, 255), QColor(0, 120, 255)});
        applyEffects(img.data(), 6, 1, {ClipEffect::make(EK::ChromaKey)}, QRectF(0, 0, 6, 1));
        QCOMPARE(int(img[3]), 0);
        QCOMPARE(int(img[7]), 0);
        QCOMPARE(int(img[11]), 255);
        QCOMPARE(int(img[15]), 255); // greys stay
        QCOMPARE(int(img[19]), 255);
        QCOMPARE(int(img[23]), 255);
        QCOMPARE(int(img[8]), 220); // red is untouched (far from green: no spill)

        // Softness: a hue between the tolerance and its end is partly transparent.
        ClipEffect soft = ClipEffect::make(EK::ChromaKey);
        soft.setValue(QStringLiteral("tolerance"), 10);
        soft.setValue(QStringLiteral("softness"), 60);
        soft.setValue(QStringLiteral("spill"), 0);
        img = pixels({QColor(120, 220, 20)}); // yellow-green, about 30° from green
        applyEffects(img.data(), 1, 1, {soft}, QRectF(0, 0, 1, 1));
        QVERIFY2(img[3] > 20 && img[3] < 235, qPrintable(QString::number(img[3])));

        // Spill: a greenish edge that stays loses some green.
        ClipEffect spill = ClipEffect::make(EK::ChromaKey);
        spill.setValue(QStringLiteral("tolerance"), 5);
        spill.setValue(QStringLiteral("softness"), 0);
        spill.setValue(QStringLiteral("spill"), 100);
        img = pixels({QColor(100, 200, 40)}); // ~ 98°: kept, but close to green
        applyEffects(img.data(), 1, 1, {spill}, QRectF(0, 0, 1, 1));
        QCOMPARE(int(img[3]), 255);
        QVERIFY(img[1] < 200);

        // Blue screen.
        ClipEffect blue = ClipEffect::make(EK::ChromaKey);
        blue.setValue(QStringLiteral("hue"), 240);
        img = pixels({QColor(20, 40, 230), QColor(30, 200, 40)});
        applyEffects(img.data(), 2, 1, {blue}, QRectF(0, 0, 2, 1));
        QCOMPARE(int(img[3]), 0);
        QCOMPARE(int(img[7]), 255);
    }

    void maskPixels()
    {
        const int w = 40, h = 20;
        const QRectF all(0, 0, w, h);
        ClipEffect m = ClipEffect::make(EK::Mask); // ellipse 60% in the middle, soft edge
        m.setValue(QStringLiteral("feather"), 0);
        auto img = flat(w, h);
        applyEffects(img.data(), w, h, {m}, all);
        QCOMPARE(alphaAt(img, w, 20, 10), 255);
        QCOMPARE(alphaAt(img, w, 0, 0), 0);
        QCOMPARE(alphaAt(img, w, 9, 4), 0);    // inside the box, outside the ellipse
        QCOMPARE(img[(size_t(10) * w + 20) * 4], uint8_t(255)); // colour untouched

        m.setValue(QStringLiteral("ellipse"), 0);
        img = flat(w, h);
        applyEffects(img.data(), w, h, {m}, all);
        QCOMPARE(alphaAt(img, w, 9, 4), 255); // rectangle corner area is inside
        QCOMPARE(alphaAt(img, w, 7, 4), 0);

        m.setValue(QStringLiteral("invert"), 1);
        img = flat(w, h);
        applyEffects(img.data(), w, h, {m}, all);
        QCOMPARE(alphaAt(img, w, 20, 10), 0);
        QCOMPARE(alphaAt(img, w, 0, 0), 255);

        // Soft edge: alpha rises from the edge inwards; size scales with the frame.
        ClipEffect f = ClipEffect::make(EK::Mask);
        f.setValue(QStringLiteral("ellipse"), 0);
        f.setValue(QStringLiteral("width"), 100);
        f.setValue(QStringLiteral("height"), 100);
        f.setValue(QStringLiteral("feather"), 108); // 108 px of 1080 = 10 px of a 100-line frame
        img = flat(100, 100);
        applyEffects(img.data(), 100, 100, {f}, QRectF(0, 0, 100, 100));
        QCOMPARE(alphaAt(img, 100, 50, 50), 255);
        const int edge = alphaAt(img, 100, 50, 0), mid = alphaAt(img, 100, 50, 5);
        QVERIFY2(edge < 20 && mid > 110 && mid < 170, qPrintable(QStringLiteral("%1 %2").arg(edge).arg(mid)));

        // Centre moves with x/y (in % of the picture inside the bars).
        ClipEffect moved = ClipEffect::make(EK::Mask);
        moved.setValue(QStringLiteral("feather"), 0);
        moved.setValue(QStringLiteral("x"), 25);
        moved.setValue(QStringLiteral("width"), 20);
        moved.setValue(QStringLiteral("height"), 100);
        img = flat(w, h);
        applyEffects(img.data(), w, h, {moved}, QRectF(10, 0, 20, h)); // picture between bars
        QCOMPARE(alphaAt(img, w, 15, 10), 255); // 10 + 25% of 20
        QCOMPARE(alphaAt(img, w, 20, 10), 0);
        QCOMPARE(alphaAt(img, w, 2, 10), 0); // the bars go too
    }

    void engine()
    {
        ProjectController ctl;
        if (!ctl.canPlay())
            QSKIP("video engine not available");
        const QUuid green = import(ctl, m_green), blue = import(ctl, m_blue);
        Project p = ctl.project();
        p.timeline = Timeline::makeDefault();
        // V1: blue background; V2: the green-screen shot.
        p.timeline.tracks[0].clips << clipOf(blue, 0, 50);
        TimelineClip fg = clipOf(green, 0, 50);
        fg.effects << ClipEffect::make(EK::ChromaKey);
        p.timeline.tracks[1].clips << fg;
        QImage img = render(p, 10);
        QVERIFY2(at(img, 0.1).blue() > 200 && at(img, 0.1).green() < 60, qPrintable(at(img, 0.1).name()));
        QVERIFY2(at(img, 0.5).red() > 180, qPrintable(at(img, 0.5).name())); // the subject stays

        // A mask instead: only an ellipse in the middle of the green shot shows.
        p.timeline.tracks[1].clips[0].effects = {ClipEffect::make(EK::Mask)};
        img = render(p, 10);
        QVERIFY(at(img, 0.02, 0.05).blue() > 200);
        QVERIFY(at(img, 0.5).red() > 180);
        QVERIFY(at(img, 0.3).green() > 150); // green inside the ellipse, beside the square
    }

    void panel()
    {
        MainWindow w;
        w.resize(1366, 768);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        ProjectController *ctl = w.controller();
        const QUuid m = import(*ctl, m_green);
        ctl->placeMedia({m}, 0, 0);
        const QUuid id = ctl->timeline().tracks[0].clips[0].id;
        w.findChild<TimelineView *>(QStringLiteral("timelineView"))->setSelection({id});
        auto *panel = w.findChild<EffectsPanel *>(QStringLiteral("effectsPanel"));
        w.findChild<QTabWidget *>(QStringLiteral("rightTabs"))->setCurrentWidget(panel);
        auto *kind = panel->findChild<QComboBox *>(QStringLiteral("effectAddKind"));
        auto *add = panel->findChild<QToolButton *>(QStringLiteral("effectAdd"));
        QVERIFY(kind->findData(int(EK::ChromaKey)) >= 0 && kind->findData(int(EK::Mask)) >= 0);

        kind->setCurrentIndex(kind->findData(int(EK::ChromaKey)));
        add->click();
        auto *hue = panel->findChild<QDoubleSpinBox *>(QStringLiteral("effectParam_hue"));
        QVERIFY(hue);
        QCOMPARE(hue->value(), 120.0);
        hue->setValue(240);
        QCOMPARE(ctl->clipEffects(id)[0].value(QStringLiteral("hue")), 240.0);

        kind->setCurrentIndex(kind->findData(int(EK::Mask)));
        add->click();
        auto *ellipse = panel->findChild<QCheckBox *>(QStringLiteral("effectParam_ellipse"));
        auto *invert = panel->findChild<QCheckBox *>(QStringLiteral("effectParam_invert"));
        QVERIFY(ellipse && ellipse->isChecked() && invert && !invert->isChecked());
        invert->setChecked(true);
        QCOMPARE(ctl->clipEffects(id)[1].value(QStringLiteral("invert")), 1.0);
        panel->findChild<QDoubleSpinBox *>(QStringLiteral("effectParam_width"))->setValue(30);
        QCOMPARE(ctl->clipEffects(id)[1].value(QStringLiteral("width")), 30.0);
        ctl->undoStack()->undo();
        QCOMPARE(ctl->clipEffects(id)[1].value(QStringLiteral("width")), 60.0);
        // Saved and read back.
        const Timeline back = Timeline::fromJson(ctl->timeline().toJson());
        QVERIFY(back.clip(id)->effects == ctl->clipEffects(id));
    }
};

QTEST_MAIN(TestPhase8c)
#include "tst_phase8c.moc"
