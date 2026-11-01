// Phase 8 (part 2) acceptance tests: clip effects (model, file, pixel work,
// engine, controller, Effects panel).
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
#include <QListWidget>
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

ClipEffect effect(EK kind, const QString &key = {}, double v = 0)
{
    ClipEffect e = ClipEffect::make(kind);
    if (!key.isEmpty())
        e.setValue(key, v);
    return e;
}

// 8x2 straight RGBA image: left half red, right half blue.
std::vector<uint8_t> halves(int w = 8, int h = 2)
{
    std::vector<uint8_t> img(size_t(w) * h * 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            uint8_t *p = img.data() + (size_t(y) * w + x) * 4;
            p[0] = x < w / 2 ? 200 : 0;
            p[1] = 0;
            p[2] = x < w / 2 ? 0 : 200;
            p[3] = 255;
        }
    return img;
}

} // namespace

class TestPhase8b : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    QString m_halves;

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
        QCoreApplication::setApplicationName(QStringLiteral("tst_phase8b"));
        QStandardPaths::setTestModeEnabled(true);
        QSettings().clear();
        QSettings().setValue(QStringLiteral("proxy/auto"), false);
        QDir(MediaCache::root()).removeRecursively();
        QVERIFY(m_dir.isValid());
        m_halves = m_dir.filePath(QStringLiteral("halves.png"));
        QImage img(160, 90, QImage::Format_RGB32);
        img.fill(QColor(255, 0, 0));
        for (int y = 0; y < 90; ++y)
            for (int x = 80; x < 160; ++x)
                img.setPixelColor(x, y, QColor(0, 0, 255));
        QVERIFY(img.save(m_halves));
    }

    void model()
    {
        ClipEffect e = ClipEffect::make(EK::Saturation);
        QCOMPARE(e.value(QStringLiteral("saturation")), 100.0);
        QVERIFY(e.isNeutral());
        QVERIFY(e.setValue(QStringLiteral("saturation"), 999));
        QCOMPARE(e.value(QStringLiteral("saturation")), 300.0);
        QVERIFY(!e.setValue(QStringLiteral("saturation"), 300)); // unchanged
        QVERIFY(!e.setValue(QStringLiteral("nothing"), 1));
        QVERIFY(!e.isNeutral());
        e.enabled = false;
        QVERIFY(e.isNeutral());
        QVERIFY(ClipEffect::make(EK::Mirror).value(QStringLiteral("horizontal")) == 1);

        Timeline t = Timeline::makeDefault();
        TimelineClip c = clipOf(QUuid::createUuid(), 0, 100);
        c.effects << effect(EK::Blur, QStringLiteral("radius"), 12.5) << e << effect(EK::Crop, QStringLiteral("top"), 10);
        t.tracks[0].clips << c;
        // Saved and read (switched-off effects too, in order).
        const Timeline back = Timeline::fromJson(t.toJson());
        QCOMPARE(back.clip(c.id)->effects.size(), 3);
        QVERIFY(back.clip(c.id)->effects == c.effects);
        QVERIFY(!back.clip(c.id)->effects[1].enabled);
        // A split keeps the effects on both parts.
        const QList<QUuid> made = t.splitAt(40, {c.id});
        QVERIFY(t.clip(made.first())->effects == c.effects);
        // The engine gets only what changes the picture.
        const ClipEffects sent = effectsFromEngineString(effectsToEngineString(c.effects));
        QCOMPARE(sent.size(), 2);
        QCOMPARE(sent[0].kind, EK::Blur);
        QCOMPARE(sent[1].kind, EK::Crop);
        QVERIFY(effectsToEngineString({ClipEffect::make(EK::BrightnessContrast)}).isEmpty());
        QVERIFY(!effectsActive({e}));
        // An unknown (newer) kind is skipped, not fatal.
        QJsonArray a = effectsToJson(c.effects);
        a.prepend(QJsonObject{{QStringLiteral("kind"), QStringLiteral("hologram")}});
        QCOMPARE(effectsFromJson(a).size(), 3);
    }

    void pixels()
    {
        const int w = 8, h = 2;
        const QRectF all(0, 0, w, h);
        auto px = [&](const std::vector<uint8_t> &img, int x, int c) { return int(img[size_t(x) * 4 + c]); };

        auto img = halves();
        applyEffects(img.data(), w, h, {effect(EK::BrightnessContrast, QStringLiteral("brightness"), 50)}, all);
        QVERIFY(px(img, 0, 0) > 230 && px(img, 0, 1) > 50);

        img = halves();
        applyEffects(img.data(), w, h, {effect(EK::BrightnessContrast, QStringLiteral("contrast"), -100)}, all);
        QVERIFY(px(img, 0, 0) < 200 && px(img, 0, 1) > 60); // towards mid grey

        img = halves();
        applyEffects(img.data(), w, h, {effect(EK::Saturation, QStringLiteral("saturation"), 0)}, all);
        QCOMPARE(px(img, 0, 0), px(img, 0, 1));
        QCOMPARE(px(img, 0, 1), px(img, 0, 2));

        img = halves();
        applyEffects(img.data(), w, h, {ClipEffect::make(EK::Mirror)}, all);
        QVERIFY(px(img, 0, 2) == 200 && px(img, 7, 0) == 200);

        img = halves();
        ClipEffect flip = ClipEffect::make(EK::Mirror);
        flip.setValue(QStringLiteral("horizontal"), 0);
        flip.setValue(QStringLiteral("vertical"), 1);
        applyEffects(img.data(), w, h, {flip}, all);
        QVERIFY(px(img, 0, 0) == 200); // rows swap, columns stay

        img = halves();
        applyEffects(img.data(), w, h, {effect(EK::Crop, QStringLiteral("left"), 25)}, all);
        QCOMPARE(px(img, 0, 3), 0);
        QCOMPARE(px(img, 1, 3), 0);
        QCOMPARE(px(img, 2, 3), 255);
        // Crop counts in the picture, not the letterbox bars.
        img = halves();
        applyEffects(img.data(), w, h, {effect(EK::Crop, QStringLiteral("left"), 50)}, QRectF(2, 0, 4, 2));
        QCOMPARE(px(img, 0, 3), 255); // bar untouched
        QCOMPARE(px(img, 3, 3), 0);
        QCOMPARE(px(img, 4, 3), 255);

        // Blur softens the edge, sharpen makes it harder.
        std::vector<uint8_t> big = halves(64, 64);
        applyEffects(big.data(), 64, 64, {effect(EK::Blur, QStringLiteral("radius"), 100)}, QRectF(0, 0, 64, 64));
        const int edgeR = big[(32 * 64 + 31) * 4], edgeB = big[(32 * 64 + 31) * 4 + 2];
        QVERIFY2(edgeR < 200 && edgeB > 0, qPrintable(QStringLiteral("%1 %2").arg(edgeR).arg(edgeB)));
        QCOMPARE(int(big[(32 * 64 + 31) * 4 + 3]), 255);
        std::vector<uint8_t> soft(size_t(64) * 4);
        for (int x = 0; x < 64; ++x) {
            const uint8_t v = uint8_t(std::clamp(100 + (x - 32) * 4, 60, 140));
            soft[x * 4] = soft[x * 4 + 1] = soft[x * 4 + 2] = v;
            soft[x * 4 + 3] = 255;
        }
        auto sharp = soft;
        applyEffects(sharp.data(), 64, 1, {effect(EK::Sharpen, QStringLiteral("amount"), 300)}, QRectF(0, 0, 64, 1));
        QVERIFY(sharp[23 * 4] < soft[23 * 4]); // darker below the ramp's start
        QVERIFY(sharp[42 * 4] > soft[42 * 4]); // brighter beyond its end
    }

    void engine()
    {
        ProjectController ctl;
        if (!ctl.canPlay())
            QSKIP("video engine not available");
        const QUuid m = import(ctl, m_halves);
        Project p = ctl.project();
        p.timeline = Timeline::makeDefault();
        TimelineClip c = clipOf(m, 0, 50);
        p.timeline.tracks[0].clips << c;
        QImage plain = render(p, 10);
        QVERIFY(at(plain, 0.1).red() > 200 && at(plain, 0.9).blue() > 200);

        auto with = [&](const ClipEffects &e) {
            p.timeline.tracks[0].clips[0].effects = e;
            return render(p, 10);
        };
        QImage img = with({ClipEffect::make(EK::Mirror)});
        QVERIFY2(at(img, 0.1).blue() > 200 && at(img, 0.9).red() > 200, qPrintable(at(img, 0.1).name()));

        img = with({effect(EK::Saturation, QStringLiteral("saturation"), 0)});
        const QColor g = at(img, 0.1);
        QVERIFY2(std::abs(g.red() - g.green()) < 12 && std::abs(g.green() - g.blue()) < 12, qPrintable(g.name()));

        img = with({effect(EK::Crop, QStringLiteral("left"), 50)});
        QVERIFY(at(img, 0.1).red() < 30); // black behind
        QVERIFY(at(img, 0.9).blue() > 200);

        // Order matters: crop then mirror leaves the right side empty.
        img = with({effect(EK::Crop, QStringLiteral("left"), 50), ClipEffect::make(EK::Mirror)});
        QVERIFY(at(img, 0.1).blue() > 200);
        QVERIFY(at(img, 0.9).blue() < 30);

        // Effects come before the motion: shrink the picture, the crop still cuts its own left half.
        p.timeline.tracks[0].clips[0].motion.base.scale = 50;
        img = with({effect(EK::Crop, QStringLiteral("left"), 50)});
        QVERIFY(at(img, 0.4).red() < 30);
        QVERIFY(at(img, 0.6).blue() > 200);
        QVERIFY(at(img, 0.9).blue() < 30);

        // Switched off: no change.
        ClipEffect off = ClipEffect::make(EK::Mirror);
        off.enabled = false;
        p.timeline.tracks[0].clips[0].motion = {};
        img = with({off});
        QVERIFY(at(img, 0.1).red() > 200);
    }

    void controllerAndPanel()
    {
        MainWindow w;
        w.resize(1366, 768);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        ProjectController *ctl = w.controller();
        const QUuid m = import(*ctl, m_halves);
        ctl->placeMedia({m}, 0, 0);
        const QUuid id = ctl->timeline().tracks[0].clips[0].id;
        auto *view = w.findChild<TimelineView *>(QStringLiteral("timelineView"));
        auto *panel = w.findChild<EffectsPanel *>(QStringLiteral("effectsPanel"));
        QVERIFY(panel);
        auto *tabs = w.findChild<QTabWidget *>(QStringLiteral("rightTabs"));
        tabs->setCurrentWidget(panel);
        QVERIFY(w.findChild<QWidget *>(QStringLiteral("effectsEmpty"))->isVisibleTo(panel));

        view->setSelection({id});
        QCOMPARE(panel->clip(), id);
        auto *kind = panel->findChild<QComboBox *>(QStringLiteral("effectAddKind"));
        auto *add = panel->findChild<QToolButton *>(QStringLiteral("effectAdd"));
        auto *list = panel->findChild<QListWidget *>(QStringLiteral("effectList"));
        QCOMPARE(kind->count(), ClipEffect::kinds().size());
        kind->setCurrentIndex(kind->findData(int(EK::Saturation)));
        const int steps = ctl->undoStack()->count();
        add->click();
        QCOMPARE(ctl->clipEffects(id).size(), 1);
        QCOMPARE(list->count(), 1);
        QCOMPARE(panel->currentEffect(), 0);

        auto *sat = panel->findChild<QDoubleSpinBox *>(QStringLiteral("effectParam_saturation"));
        QVERIFY(sat);
        QTRY_VERIFY(sat->isVisibleTo(panel)); // new rows are shown on the next event loop pass
        QCOMPARE(sat->value(), 100.0);
        sat->setValue(50);
        sat->setValue(20);
        QCOMPARE(ctl->clipEffects(id)[0].value(QStringLiteral("saturation")), 20.0);
        QCOMPARE(ctl->undoStack()->count(), steps + 2); // add + one merged change

        kind->setCurrentIndex(kind->findData(int(EK::Mirror)));
        add->click();
        QCOMPARE(panel->currentEffect(), 1);
        auto *vertical = panel->findChild<QCheckBox *>(QStringLiteral("effectParam_vertical"));
        QVERIFY(vertical && !vertical->isChecked());
        vertical->setChecked(true);
        QCOMPARE(ctl->clipEffects(id)[1].value(QStringLiteral("vertical")), 1.0);
        panel->findChild<QToolButton *>(QStringLiteral("effectUp"))->click();
        QCOMPARE(ctl->clipEffects(id)[0].kind, EK::Mirror);
        QCOMPARE(panel->currentEffect(), 0);

        list->item(0)->setCheckState(Qt::Unchecked);
        QVERIFY(!ctl->clipEffects(id)[0].enabled);
        list->setCurrentRow(1);
        QCOMPARE(panel->findChild<QDoubleSpinBox *>(QStringLiteral("effectParam_saturation"))->value(), 20.0);
        panel->findChild<QToolButton *>(QStringLiteral("effectRemove"))->click();
        QCOMPARE(ctl->clipEffects(id).size(), 1);
        ctl->undoStack()->undo();
        QCOMPARE(ctl->clipEffects(id).size(), 2);
        QCOMPARE(list->count(), 2);

        // Audio clips have no effects.
        QVERIFY(ctl->addClipEffect(QUuid::createUuid(), EK::Blur) < 0);
    }
};

QTEST_MAIN(TestPhase8b)
#include "tst_phase8b.moc"
