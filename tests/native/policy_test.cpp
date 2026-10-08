#include "policy.h"
#include <QtTest>
using namespace csa;
static Observation observation(qint64 now, QString status = "running", QString revision = "r") {
    Observation result; result.healthy = true; result.capturedAt = now; result.revision = revision;
    Thread thread; thread.id = "a"; thread.title = "Chat A"; thread.turnId = "turn-a";
    thread.status = status; thread.turnStatus = status == "running" ? "inProgress" : status;
    thread.workPending = status == "running"; result.threads.append(thread); return result;
}
class PolicyTest : public QObject {
    Q_OBJECT
private slots:
    void oneShotAndAllChats() {
        Policy policy({60,60,"en"}); qint64 now = 1000000;
        auto current = observation(now); auto second = current.threads[0]; second.id = "b";
        current.threads.append(second); policy.arm(current, now);
        current.threads[0].status = "completed"; current.threads[0].workPending = false;
        QVERIFY(!policy.evaluate(current, now)); QCOMPARE(policy.view(now).phase, QString("waiting"));
        current = observation(now, "completed", "done"); current.threads.append(second);
        current.threads[1].status = "completed"; current.threads[1].turnStatus = "completed"; current.threads[1].workPending = false;
        QVERIFY(!policy.evaluate(current, now)); QCOMPARE(policy.view(now).phase, QString("settling"));
        now += 60000; current.capturedAt = now; QVERIFY(!policy.evaluate(current, now));
        QCOMPARE(policy.view(now).phase, QString("countdown"));
        now += 60000; current.capturedAt = now; QVERIFY(policy.evaluate(current, now));
        policy.consume(); QVERIFY(!policy.view(now).armed); QVERIFY(!policy.evaluate(current, now));
    }
    void failureWithPendingWork_data() {
        QTest::addColumn<QString>("terminal");
        QTest::newRow("failure-and-goal") << "failed";
        QTest::newRow("interruption-and-queue") << "interrupted";
    }
    void failureWithPendingWork() {
        QFETCH(QString, terminal); Policy policy; qint64 now = 1000000;
        policy.arm(observation(now), now);
        auto current = observation(now, "blocked"); current.threads[0].workPending = true;
        current.threads[0].turnStatus = terminal;
        QVERIFY(!policy.evaluate(current, now)); QVERIFY(!policy.view(now).armed);
        current = observation(now, "completed"); now += 500000; current.capturedAt = now;
        QVERIFY(!policy.evaluate(current, now));
    }
    void unknownAndMissingAndStale() {
        Policy policy; const qint64 now = 1000000;
        auto current = observation(now); policy.arm(current, now);
        current.threads.clear(); QVERIFY(!policy.evaluate(current, now)); QVERIFY(!policy.view(now).blockers.isEmpty());
        current = observation(now, "completed"); current.healthy = false;
        QVERIFY(!policy.evaluate(current, now)); QCOMPARE(policy.view(now).phase, QString("waiting"));
        current.healthy = true; QVERIFY(!policy.evaluate(current, now + 10001));
    }
    void supersededFailureStillExpiresBatch() {
        Policy policy; const qint64 now=1000000;
        auto current=observation(now); current.threads[0].terminalOrdinal=3;
        policy.arm(current,now);
        current=observation(now,"completed"); current.threads[0].turnId="fast-follow-up";
        current.threads[0].terminalOrdinal=5;
        QVERIFY(!policy.evaluate(current,now)); QVERIFY(!policy.view(now).armed);
        policy.arm(observation(now),now); policy.cancel();
        current=observation(now); current.threads[0].terminalOrdinal=5; policy.arm(current,now);
        current.threads[0].status=current.threads[0].turnStatus="completed"; current.threads[0].workPending=false;
        policy.evaluate(current,now); QCOMPARE(policy.view(now).phase,QString("settling"));
    }
    void newTurnAndRevisionReset() {
        Policy policy({60,60,"en"}); qint64 now = 1000000;
        policy.arm(observation(now), now); auto current = observation(now, "completed"); policy.evaluate(current, now);
        now += 30000; current.capturedAt = now; current.revision = "saved-again"; policy.evaluate(current, now);
        QCOMPARE(policy.view(now).remainingSeconds, 60);
        current.threads[0].turnId = "turn-b"; current.threads[0].turnStatus = "failed";
        current.threads[0].status = "failed"; policy.evaluate(current, now); QVERIFY(!policy.view(now).armed);
    }
    void cancelSettingsAndRestart() {
        Policy policy; const qint64 now = 1000000; policy.arm(observation(now), now);
        policy.cancel(); QVERIFY(!policy.view(now).armed);
        policy.arm(observation(now), now); policy.configure({60,60,"ar"}); QVERIFY(!policy.view(now).armed);
        QVERIFY(!Policy().view(now).armed);
        QVERIFY_EXCEPTION_THROWN(validateSettings({0,60,"en"}), std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(policy.arm(observation(now,"completed"),now), std::runtime_error);
    }
};
QTEST_GUILESS_MAIN(PolicyTest)
#include "policy_test.moc"
