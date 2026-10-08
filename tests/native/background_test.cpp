#include "background_monitor.h"
#include <QElapsedTimer>
#include <QSemaphore>
#include <QTest>
#include <QThread>
#include <atomic>
#include <memory>
#include <stdexcept>

using namespace csa;
namespace {
struct RecordingState {
    std::atomic<int> calls{0}, active{0}, maxActive{0};
    std::atomic<bool> holdFirst{false}, pending{false}, fail{false};
    std::atomic<bool> destroyed{false}, destroyedDuringScan{false};
    QSemaphore entered, release;
};
class RecordingMonitor final : public Monitor {
public:
    explicit RecordingMonitor(std::shared_ptr<RecordingState> state) : state_(std::move(state)) {}
    ~RecordingMonitor() override {
        state_->destroyedDuringScan = state_->active.load() != 0;
        state_->destroyed = true;
    }
    Observation scan(qint64 now) override {
        const int call = ++state_->calls;
        const int active = ++state_->active;
        int previous = state_->maxActive.load();
        while (previous < active && !state_->maxActive.compare_exchange_weak(previous, active)) {}
        state_->entered.release();
        if (call == 1 && state_->holdFirst.load()) state_->release.tryAcquire(1, 5000);
        Observation result;
        result.capturedAt = now;
        result.healthy = true;
        result.revision = QString::number(call);
        Thread thread;
        thread.id = "work";
        thread.status = state_->pending.load() ? "running" : "completed";
        thread.turnStatus = state_->pending.load() ? "inProgress" : "completed";
        thread.workPending = state_->pending.load();
        result.threads.append(thread);
        --state_->active;
        if (state_->fail.load()) throw std::runtime_error("Recording monitor failure");
        return result;
    }
private:
    std::shared_ptr<RecordingState> state_;
};
std::unique_ptr<Monitor> source(const std::shared_ptr<RecordingState> &state) {
    return std::make_unique<RecordingMonitor>(state);
}
} // namespace

class BackgroundTest : public QObject {
    Q_OBJECT
private slots:
    void firstReadIsNonblocking() {
        auto state = std::make_shared<RecordingState>(); state->holdFirst = true;
        BackgroundMonitor monitor(source(state)); QVERIFY(state->entered.tryAcquire(1, 2000));
        QElapsedTimer timer; timer.start(); const auto result = monitor.scan(1234);
        QVERIFY(timer.elapsed() < 250); QVERIFY(!result.healthy);
        QCOMPARE(result.blockers.first().code, "MONITOR_INITIALIZING"); QCOMPARE(result.capturedAt, qint64(0));
        state->release.release();
    }
    void eventuallyPublishesRealObservation() {
        auto state = std::make_shared<RecordingState>(); state->holdFirst = true;
        BackgroundMonitor monitor(source(state)); QVERIFY(state->entered.tryAcquire(1, 2000));
        QVERIFY(!monitor.scan(0).healthy); state->release.release();
        QTRY_VERIFY_WITH_TIMEOUT(monitor.scan(0).healthy, 2000);
        const auto result = monitor.scan(0); QCOMPARE(result.threads.first().status, "completed");
        QVERIFY(result.capturedAt > 0); QVERIFY(state->calls.load() >= 1);
    }
    void cacheNeverRefreshesCapturedAt() {
        auto state = std::make_shared<RecordingState>(); BackgroundMonitor monitor(source(state));
        QTRY_VERIFY_WITH_TIMEOUT(monitor.scan(0).healthy, 2000);
        const auto before = monitor.scan(100);
        const auto later = monitor.scan(before.capturedAt + 100000);
        QCOMPARE(later.capturedAt, before.capturedAt); QCOMPARE(later.revision, before.revision);
    }
    void injectedClockKeepsCacheInTheFreshnessDomain() {
        auto state = std::make_shared<RecordingState>(); std::atomic<qint64> steady{1000};
        BackgroundMonitor monitor(source(state), [&steady] { return steady.load(); });
        QTRY_VERIFY_WITH_TIMEOUT(monitor.scan(0).healthy, 2000);
        const auto original = monitor.scan(0); QCOMPARE(original.capturedAt, qint64(1000));
        const qint64 simulatedWall = QDateTime::currentMSecsSinceEpoch() + 86400000;
        steady = 1100; const auto cached = monitor.scan(simulatedWall);
        QCOMPARE(cached.capturedAt, original.capturedAt); QVERIFY(cached.healthy);
        QCOMPARE(steady.load() - cached.capturedAt, qint64(100));
        const auto verified = monitor.verify(steady.load()); QCOMPARE(verified.capturedAt, qint64(1100));
    }
    void finalVerificationReadsNewWork() {
        auto state = std::make_shared<RecordingState>(); BackgroundMonitor monitor(source(state));
        QTRY_VERIFY_WITH_TIMEOUT(monitor.scan(0).healthy, 2000);
        const auto cached = monitor.scan(0); QVERIFY(!cached.threads.first().workPending);
        const int calls = state->calls.load(); state->pending = true;
        const auto verified = monitor.verify(9876);
        QVERIFY(verified.healthy); QCOMPARE(verified.capturedAt, qint64(9876));
        QVERIFY(verified.threads.first().workPending); QCOMPARE(verified.threads.first().status, "running");
        QVERIFY(state->calls.load() > calls); QCOMPARE(monitor.scan(0).capturedAt, cached.capturedAt);
    }
    void scansAndVerificationAreSerialized() {
        auto state = std::make_shared<RecordingState>(); state->holdFirst = true;
        BackgroundMonitor monitor(source(state)); QVERIFY(state->entered.tryAcquire(1, 2000));
        std::atomic<bool> finished{false}; Observation verified;
        auto verifier = QThread::create([&] { verified = monitor.verify(7654); finished = true; });
        verifier->start(); QTest::qWait(40); QVERIFY(!finished.load());
        state->release.release(); QVERIFY(verifier->wait(2000)); delete verifier;
        QVERIFY(finished.load()); QVERIFY(verified.healthy); QCOMPARE(state->maxActive.load(), 1);
        QCOMPARE(verified.capturedAt, qint64(7654));
    }
    void exceptionsKeepWorkerAndVerificationUnhealthy() {
        auto state = std::make_shared<RecordingState>(); state->fail = true;
        BackgroundMonitor monitor(source(state));
        QTRY_COMPARE_WITH_TIMEOUT(monitor.scan(0).blockers.first().code, QString("MONITOR_READ_FAILED"), 2000);
        QVERIFY(!monitor.scan(0).healthy);
        const auto result = monitor.verify(4567); QVERIFY(!result.healthy);
        QCOMPARE(result.capturedAt, qint64(4567)); QCOMPARE(result.blockers.first().code, "MONITOR_READ_FAILED");
    }
    void destructionJoinsBeforeDestroyingSource() {
        auto state = std::make_shared<RecordingState>(); state->holdFirst = true;
        auto monitor = std::make_unique<BackgroundMonitor>(source(state)); QVERIFY(state->entered.tryAcquire(1, 2000));
        auto release = QThread::create([state] { QThread::msleep(100); state->release.release(); }); release->start();
        QElapsedTimer timer; timer.start(); monitor.reset(); QVERIFY(release->wait(2000)); delete release;
        QVERIFY(state->destroyed.load()); QVERIFY(!state->destroyedDuringScan.load());
        QCOMPARE(state->active.load(), 0); QVERIFY(timer.elapsed() < 1500);
    }
};
QTEST_GUILESS_MAIN(BackgroundTest)
#include "background_test.moc"
