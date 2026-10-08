#include "main_window.h"
#include "controller.h"
#include "simulation.h"
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QtTest>

using namespace csa;

namespace {
class FixtureMonitor final : public Monitor {
public:
    Observation value;
    Observation scan(qint64 now) override {
        value.capturedAt = now;
        return value;
    }
};
bool capture(QWidget &window, const QString &name) {
    const QString directory = qEnvironmentVariable("CSA_UI_CAPTURE_DIR");
    if (directory.isEmpty()) return true;
    if (!QDir().mkpath(directory)) return false;
    QApplication::processEvents();
    return window.grab().save(QDir(directory).filePath(name + ".png"));
}
} // namespace

class UiTest final : public QObject {
    Q_OBJECT
private slots:
    void controlsUseOneTimePermissionAndExposeBlockers();
};

void UiTest::controlsUseOneTimePermissionAndExposeBlockers() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    FixtureMonitor monitor;
    monitor.value.healthy = true;
    monitor.value.revision = "running";
    Thread thread;
    thread.id = "synthetic-chat";
    thread.title = "Synthetic verification task";
    thread.status = "running";
    thread.turnStatus = "inProgress";
    thread.turnId = "synthetic-turn-1";
    thread.workPending = true;
    thread.lastActivityAt = now;
    monitor.value.threads.append(thread);
    Store store(temporary.path());
    RecordingShutdown shutdown;
    Controller controller(monitor, store, shutdown, true, [&now] { return now; });
    MainWindow window(controller);
    window.show();
    QTest::qWait(20);
    auto *enable = window.findChild<QPushButton *>("enableButton");
    auto *cancel = window.findChild<QPushButton *>("cancelButton");
    auto *save = window.findChild<QPushButton *>("saveSettingsButton");
    auto *state = window.findChild<QLabel *>("stateName");
    auto *blockers = window.findChild<QListWidget *>("blockerList");
    auto *chats = window.findChild<QTableWidget *>("chatsTable");
    auto *history = window.findChild<QTableWidget *>("historyTable");
    auto *settle = window.findChild<QSpinBox *>("settleSpin");
    auto *countdown = window.findChild<QSpinBox *>("countdownSpin");
    auto *language = window.findChild<QComboBox *>("languageCombo");
    QVERIFY(enable && cancel && save && state && blockers && chats && history && settle && countdown && language);
    QCOMPARE(state->text(), QString("OFF"));
    QVERIFY(enable->isEnabled());
    QVERIFY(!cancel->isEnabled());
    QCOMPARE(chats->rowCount(), 1);
    QCOMPARE(chats->item(0, 0)->text(), thread.title);
    QVERIFY(window.findChild<QLabel *>("simulationBanner")->isVisible());
    QCOMPARE(settle->minimum(), 60);
    QCOMPARE(countdown->maximum(), 3600);
    QVERIFY(capture(window, "native-off"));

    monitor.value.blockers = {{"unfinished-goal", "A synthetic goal still needs work."}};
    controller.tick();
    QVERIFY(blockers->isVisible());
    QCOMPARE(blockers->count(), 1);
    QVERIFY(blockers->item(0)->text().contains("synthetic goal"));
    QTest::mouseClick(enable, Qt::LeftButton);
    QVERIFY(controller.view().policy.armed);
    QCOMPARE(controller.view().policy.phase, QString("waiting"));
    QVERIFY(cancel->isEnabled());
    QCOMPARE(shutdown.calls, 0);

    monitor.value.blockers.clear();
    monitor.value.threads[0].status = "completed";
    monitor.value.threads[0].turnStatus = "completed";
    monitor.value.threads[0].workPending = false;
    monitor.value.revision = "completed";
    controller.tick();
    QCOMPARE(controller.view().policy.phase, QString("settling"));
    now += store.settings().settleSeconds * 1000;
    controller.tick();
    QCOMPARE(controller.view().policy.phase, QString("countdown"));
    QVERIFY(state->text().contains("60 seconds"));
    QVERIFY(capture(window, "native-countdown"));
    QTest::mouseClick(cancel, Qt::LeftButton);
    QCOMPARE(state->text(), QString("OFF"));
    QVERIFY(!controller.view().policy.armed);
    QVERIFY(!enable->isEnabled());
    QCOMPARE(shutdown.calls, 0);

    monitor.value.threads[0].status = "running";
    monitor.value.threads[0].turnStatus = "inProgress";
    monitor.value.threads[0].turnId = "synthetic-turn-2";
    monitor.value.threads[0].workPending = true;
    monitor.value.revision = "running-again";
    controller.tick();
    QTest::mouseClick(enable, Qt::LeftButton);
    QVERIFY(controller.view().policy.armed);
    settle->setValue(61);
    QVERIFY(save->isEnabled());
    QTest::mouseClick(save, Qt::LeftButton);
    QCOMPARE(store.settings().settleSeconds, 61);
    QVERIFY(!controller.view().policy.armed);
    QTest::mouseClick(enable, Qt::LeftButton);
    QVERIFY(controller.view().policy.armed);
    language->setCurrentIndex(1);
    QCOMPARE(window.layoutDirection(), Qt::RightToLeft);
    QCOMPARE(state->text(), QString::fromUtf8("غير مفعّل"));
    QCOMPARE(store.settings().language, QString("ar"));
    QVERIFY(!controller.view().policy.armed);
    QVERIFY(history->rowCount() >= 5);
    QCOMPARE(history->item(0, 2)->text(), store.history().first().value("message").toString());
    QVERIFY(capture(window, "native-arabic"));
    window.findChild<QTabWidget *>("viewTabs")->setCurrentIndex(1);
    QVERIFY(history->isVisible());
    QVERIFY(capture(window, "native-history"));
    QCOMPARE(shutdown.calls, 0);

    monitor.value.healthy = false;
    monitor.value.blockers = {{"read-error", "Synthetic read error."}};
    controller.tick();
    QVERIFY(!enable->isEnabled());
    QVERIFY(blockers->count() > 0);
    QCOMPARE(shutdown.calls, 0);
}

QTEST_MAIN(UiTest)
#include "ui_test.moc"
