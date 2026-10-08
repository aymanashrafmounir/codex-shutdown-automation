#include "controller.h"
#include "simulation.h"
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>
#include <stdexcept>
using namespace csa;
class FakeMonitor final : public Monitor {
public:
    QString status = "running", turnStatus = "inProgress", revision = "running";
    bool healthy = true, mutateFinal = false;
    int scans = 0;
    Observation scan(qint64 now) override {
        ++scans;
        if (mutateFinal && scans >= 6) { status = "running"; revision = "new-work"; }
        Observation result; result.healthy = healthy; result.capturedAt = now; result.revision = revision;
        Thread thread; thread.id = "a"; thread.title = "Test chat"; thread.turnId = "turn";
        thread.status = status; thread.turnStatus = turnStatus; thread.workPending = status == "running";
        result.threads.append(thread); return result;
    }
    void complete() { status = turnStatus = "completed"; revision = "complete"; }
};
class InspectShutdown final : public Shutdown {
public:
    QString directory; int calls = 0; bool saved = false, fail = false;
    void request() override {
        ++calls; QFile file(directory + "/decisions.jsonl"); file.open(QIODevice::ReadOnly);
        saved = QFile::exists(directory + "/settings.json") && file.readAll().contains("shutdown-requested");
        if (fail) throw std::runtime_error("Synthetic shutdown failure.");
    }
};
class CachedMonitor final : public Monitor {
public:
    FakeMonitor cached, fresh;
    int verifications=0;
    Observation scan(qint64 now) override { return cached.scan(now); }
    Observation verify(qint64 now) override { ++verifications; return fresh.scan(now); }
};
class ControllerTest : public QObject {
    Q_OBJECT
private slots:
    void savesBeforeRecordingShutdown() {
        QTemporaryDir temporary; Store store(temporary.path()); FakeMonitor monitor; InspectShutdown shutdown;
        shutdown.directory = temporary.path(); qint64 now = 1000000;
        Controller controller(monitor, store, shutdown, true, [&] {return now;});
        QVERIFY(controller.configure({60,60,"en"})); QVERIFY(controller.arm()); monitor.complete(); controller.tick();
        now += 60000; controller.tick(); now += 60000; controller.tick();
        QCOMPARE(shutdown.calls, 1); QVERIFY(shutdown.saved); QVERIFY(!controller.view().policy.armed);
        now += 60000; controller.tick(); QCOMPARE(shutdown.calls, 1);
    }
    void finalRescanSeesNewWork() {
        QTemporaryDir temporary; Store store(temporary.path()); FakeMonitor monitor; InspectShutdown shutdown;
        qint64 now = 1000000; Controller controller(monitor,store,shutdown,true,[&]{return now;});
        controller.configure({60,60,"en"}); QVERIFY(controller.arm()); monitor.complete(); controller.tick();
        now += 60000; controller.tick(); monitor.mutateFinal = true;
        now += 60000; controller.tick(); QCOMPARE(shutdown.calls,0); QCOMPARE(controller.view().policy.phase,QString("waiting"));
    }
    void persistenceFailureBlocksShutdown() {
        QTemporaryDir temporary; Store store(temporary.path()); FakeMonitor monitor; RecordingShutdown shutdown;
        qint64 now = 1000000; Controller controller(monitor,store,shutdown,true,[&]{return now;});
        controller.configure({60,60,"en"}); QVERIFY(controller.arm()); monitor.complete(); controller.tick();
        now += 60000; controller.tick();
        QFile::remove(temporary.path()+"/decisions.jsonl"); QDir().mkdir(temporary.path()+"/decisions.jsonl");
        now += 60000; controller.tick(); QCOMPARE(shutdown.calls,0); QVERIFY(!controller.view().policy.armed);
        QVERIFY(!controller.view().error.isEmpty());
    }
    void uncachedFinalGateBlocksNewWork() {
        QTemporaryDir temporary; Store store(temporary.path()); CachedMonitor monitor; RecordingShutdown shutdown;
        qint64 now=1000000; Controller controller(monitor,store,shutdown,true,[&]{return now;});
        controller.configure({60,60,"en"}); QVERIFY(controller.arm()); monitor.cached.complete(); controller.tick();
        now+=60000; controller.tick(); now+=60000; controller.tick();
        QCOMPARE(monitor.verifications,1); QCOMPARE(shutdown.calls,0);
        QCOMPARE(controller.view().policy.phase,QString("waiting"));
    }
    void commandFailureConsumesAuthorization() {
        QTemporaryDir temporary; Store store(temporary.path()); FakeMonitor monitor; InspectShutdown shutdown;
        shutdown.directory=temporary.path(); shutdown.fail=true; qint64 now=1000000;
        Controller controller(monitor,store,shutdown,true,[&]{return now;});
        controller.configure({60,60,"en"}); controller.arm(); monitor.complete(); controller.tick();
        now+=60000; controller.tick(); now+=60000; controller.tick();
        QCOMPARE(shutdown.calls,1); QVERIFY(!controller.view().policy.armed); QVERIFY(!controller.view().error.isEmpty());
        const auto history = store.history();
        QVERIFY(std::any_of(history.begin(),history.end(),[](const QJsonObject &row){return row["type"]=="shutdown-failed";}));
        now+=2000; controller.tick(); QVERIFY(!controller.view().error.isEmpty()); QCOMPARE(shutdown.calls,1);
    }
    void restartDoesNotRestoreEnable() {
        QTemporaryDir temporary; Store store(temporary.path()); FakeMonitor monitor; RecordingShutdown shutdown;
        qint64 now=1000000;
        {Controller first(monitor,store,shutdown,true,[&]{return now;}); first.configure({60,90,"ar"}); QVERIFY(first.arm());}
        Controller second(monitor,store,shutdown,true,[&]{return now;});
        QVERIFY(!second.view().policy.armed); QCOMPARE(second.view().settings.language,QString("ar")); QCOMPARE(shutdown.calls,0);
    }
    void corruptSettingsDoNotAuthorize() {
        QTemporaryDir temporary; Store store(temporary.path()); QFile file(temporary.path()+"/settings.json");
        QVERIFY(file.open(QIODevice::WriteOnly)); file.write("{\"armed\":true}"); file.close();
        QVERIFY_EXCEPTION_THROWN(store.settings(), std::runtime_error);
    }
    void idleMonitorPhysicallyExpiresHistory() {
        QTemporaryDir temporary; Store store(temporary.path()); FakeMonitor monitor; RecordingShutdown shutdown;
        qint64 now = 1791460000000;
        Controller controller(monitor, store, shutdown, true, [&] { return now; });
        store.append({{"timestamp", QDateTime::fromMSecsSinceEpoch(now - 72LL * 3600000 + 60000, Qt::UTC)
            .toString(Qt::ISODateWithMs)}, {"type", "expiry-probe"}});
        now += 60000;
        controller.tick();
        QFile journal(temporary.path() + "/decisions.jsonl");
        QVERIFY(journal.open(QIODevice::ReadOnly));
        QVERIFY(!journal.readAll().contains("expiry-probe"));
        QVERIFY(!controller.view().policy.armed); QCOMPARE(shutdown.calls, 0);
    }
};
QTEST_GUILESS_MAIN(ControllerTest)
#include "controller_test.moc"
