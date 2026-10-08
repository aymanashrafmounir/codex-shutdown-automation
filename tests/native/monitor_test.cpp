#include "codex_monitor.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

using namespace csa;
namespace {
constexpr qint64 now = 2000000;
const QStringList files = {"state_5.sqlite", "thread_history_1.sqlite", "goals_1.sqlite", "queue_1.sqlite"};
const QByteArray session = "{\"type\":\"fixture\"}\n";
class Fixture {
public:
    QTemporaryDir directory;
    bool ready = true;
    Fixture() {
        QDir(directory.path()).mkdir("sessions");
        exec(0, "CREATE TABLE threads (id TEXT PRIMARY KEY,title TEXT,rollout_path TEXT,history_mode TEXT,updated_at_ms INTEGER)");
        exec(1, "CREATE TABLE thread_turns (thread_id TEXT,turn_id TEXT,status TEXT,rollout_ordinal INTEGER,started_at INTEGER,"
                "completed_at INTEGER,rollout_end_ordinal INTEGER,rollout_end_byte_offset INTEGER,PRIMARY KEY(thread_id,turn_id))");
        exec(1, "CREATE TABLE thread_history_projection_state (thread_id TEXT PRIMARY KEY,next_rollout_byte_offset INTEGER,next_rollout_ordinal INTEGER)");
        exec(1, "CREATE TABLE thread_items (thread_id TEXT,turn_id TEXT,item_id TEXT,rollout_ordinal INTEGER,updated_at_ordinal INTEGER,"
                "item_type TEXT,item_json TEXT,PRIMARY KEY(thread_id,turn_id,item_id))");
        exec(2, "CREATE TABLE thread_goals (thread_id TEXT,status TEXT,updated_at_ms INTEGER)");
        exec(3, "CREATE TABLE queued_items (id TEXT,thread_id TEXT,updated_at_ms INTEGER)");
    }
    QString path(int index) const { return QDir(directory.path()).filePath(files[index]); }
    QString rollout(const QString &id) const { return QDir(directory.path()).filePath("sessions/" + id + ".jsonl"); }
    void exec(int index, const QString &sql, const QVariantList &values = {}) {
        const QString name = QUuid::createUuid().toString();
        bool ok = false;
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", name);
            db.setDatabaseName(path(index));
            if (db.open()) {
                QSqlQuery query(db);
                if (query.prepare(sql)) {
                    for (const auto &value : values) query.addBindValue(value);
                    ok = query.exec();
                }
            }
            db.close();
        }
        QSqlDatabase::removeDatabase(name);
        ready = ready && ok;
    }
    void turn(const QString &id, int ordinal, const QString &status) {
        const bool complete = status == "completed";
        exec(1, "INSERT INTO thread_turns VALUES (?,?,?,?,?,?,?,?)", {id,id + "-" + QString::number(ordinal),status,ordinal,1000,
            complete ? QVariant(2000) : QVariant(), complete ? QVariant(ordinal + 1) : QVariant(),
            complete ? QVariant(session.size()) : QVariant()});
        exec(1, "INSERT INTO thread_history_projection_state VALUES (?,?,?) ON CONFLICT(thread_id) DO UPDATE SET "
                "next_rollout_ordinal=MAX(next_rollout_ordinal,excluded.next_rollout_ordinal)", {id,session.size(),ordinal + 2});
    }
    void thread(const QString &id, const QString &status = "completed", int ordinal = 1) {
        QFile file(rollout(id));
        ready = ready && file.open(QIODevice::WriteOnly);
        if (file.isOpen()) { ready = ready && file.write(session) == session.size(); file.close(); }
        exec(0, "INSERT INTO threads VALUES (?,?,?,?,?)", {id,"Fixture " + id,rollout(id),"paginated",now});
        turn(id, ordinal, status);
    }
    void item(const QString &id, int turn, const QString &itemId, int ordinal, const QString &type, const QJsonObject &value) {
        exec(1, "INSERT INTO thread_items VALUES (?,?,?,?,?,?,?)", {id,id + "-" + QString::number(turn),itemId,ordinal,0,type,
            QString::fromUtf8(QJsonDocument(value).toJson(QJsonDocument::Compact))});
    }
    Observation scan() { return CodexMonitor(directory.path()).scan(now); }
    QVector<QByteArray> fingerprints() {
        QVector<QByteArray> result;
        for (int i = 0; i < 4; ++i) {
            QFile file(path(i)); file.open(QIODevice::ReadOnly);
            result.append(QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256));
        }
        return result;
    }
};
QJsonObject command(QJsonValue exit) { return {{"status","completed"},{"processId","42"},{"exitCode",exit}}; }
QString code(const Observation &observation) { return observation.blockers.isEmpty() ? QString() : observation.blockers.first().code; }
} // namespace

class MonitorTest : public QObject {
    Q_OBJECT
private slots:
    void latestStates() {
        Fixture f; f.thread("parent"); f.thread("subagent", "inProgress"); QVERIFY(f.ready);
        const auto s = f.scan(); QVERIFY(s.healthy); QCOMPARE(s.capturedAt, now); QCOMPARE(s.threads.size(), 2);
        QCOMPARE(s.threads[0].status, "completed"); QVERIFY(!s.threads[0].workPending);
        QCOMPARE(s.threads[1].status, "running"); QCOMPARE(s.threads[1].turnStatus, "inProgress");
        QCOMPARE(s.threads[1].turnId, "subagent-1"); QVERIFY(s.threads[1].workPending); QCOMPARE(s.revision, f.scan().revision);
    }
    void orphanSuperseded() {
        Fixture f; f.thread("chat", "inProgress"); f.turn("chat", 2, "completed"); QVERIFY(f.ready);
        const auto s = f.scan(); QVERIFY(s.healthy); QCOMPARE(s.threads[0].status, "completed");
        QCOMPARE(s.threads[0].turnId, "chat-2"); QVERIFY(!s.threads[0].workPending); QVERIFY(s.blockers.isEmpty());
    }
    void terminalCohort() {
        Fixture f; f.thread("failed", "failed"); f.thread("interrupted", "interrupted"); QVERIFY(f.ready);
        const auto s = f.scan(); QVERIFY(s.healthy); QCOMPARE(s.threads[0].status, "failed");
        QCOMPARE(s.threads[1].status, "blocked"); QVERIFY(!s.threads[0].workPending); QVERIFY(!s.threads[1].workPending);
        QVERIFY(s.blockers.isEmpty());
    }
    void pendingGoalsQueue() {
        Fixture f; f.thread("goal"); f.thread("queue");
        f.exec(2, "INSERT INTO thread_goals VALUES (?,?,?)", {"goal","active",now});
        f.exec(3, "INSERT INTO queued_items VALUES (?,?,?)", {"item","queue",now}); QVERIFY(f.ready);
        const auto s = f.scan(); QVERIFY(s.healthy); QCOMPARE(s.blockers.size(), 2);
        QCOMPARE(s.blockers[0].code, "UNFINISHED_GOAL"); QCOMPARE(s.blockers[1].code, "PENDING_QUEUE");
        for (const auto &thread : s.threads) { QCOMPARE(thread.status, "blocked"); QVERIFY(thread.workPending); }
    }
    void completedGoalHandledFailure() {
        Fixture f; f.thread("chat"); f.exec(2, "INSERT INTO thread_goals VALUES (?,?,?)", {"chat","complete",now});
        f.item("chat", 1, "tool", 1, "mcpToolCall", {{"status","failed"}}); QVERIFY(f.ready);
        const auto s = f.scan(); QVERIFY(s.healthy); QCOMPARE(s.threads[0].status, "completed"); QVERIFY(s.blockers.isEmpty());
    }
    void projectionLag() {
        Fixture f; f.thread("chat"); QFile file(f.rollout("chat")); QVERIFY(file.open(QIODevice::Append));
        file.write("new event\n"); file.close(); QVERIFY(f.ready);
        const auto s = f.scan(); QVERIFY(!s.healthy); QCOMPARE(s.threads[0].status, "unknown"); QCOMPARE(code(s), "PROJECTION_LAG");
    }
    void missingStore() {
        Fixture f; QVERIFY(f.ready); QVERIFY(QFile::remove(f.path(3))); const auto s = f.scan();
        QVERIFY(!s.healthy); QCOMPARE(code(s), "STORE_MISSING");
    }
    void missingSchema() {
        Fixture f; f.exec(3, "DROP TABLE queued_items"); f.exec(3, "CREATE TABLE queued_items(id TEXT)"); QVERIFY(f.ready);
        const auto s = f.scan(); QVERIFY(!s.healthy); QCOMPARE(code(s), "STORE_INCOMPATIBLE");
    }
    void unknownStatus() {
        Fixture f; f.thread("chat", "futureStatus"); QVERIFY(f.ready); const auto s = f.scan();
        QVERIFY(!s.healthy); QCOMPARE(s.threads[0].status, "unknown"); QCOMPARE(code(s), "UNKNOWN_TURN_STATUS");
    }
    void pendingCurrentCommand() {
        Fixture f; f.thread("chat"); f.item("chat", 1, "command", 1, "commandExecution", command(QJsonValue::Null)); QVERIFY(f.ready);
        const auto s = f.scan(); QVERIFY(s.healthy); QCOMPARE(s.threads[0].status, "blocked");
        QVERIFY(s.threads[0].workPending); QCOMPARE(code(s), "PENDING_TOOL");
    }
    void metadataWithoutTurn() {
        Fixture f; f.thread("chat"); f.exec(1, "DELETE FROM thread_turns"); QVERIFY(f.ready); const auto s = f.scan();
        QVERIFY(!s.healthy); QCOMPARE(s.threads[0].id, "chat"); QCOMPARE(s.threads[0].status, "unknown");
        QVERIFY(s.threads[0].turnId.isEmpty()); QCOMPARE(code(s), "UNKNOWN_THREAD_STATE");
    }
    void missingProjection() {
        Fixture f; f.thread("chat"); f.exec(1, "DELETE FROM thread_history_projection_state"); QVERIFY(f.ready);
        const auto s = f.scan(); QVERIFY(!s.healthy); QCOMPARE(code(s), "PROJECTION_LAG");
    }
    void oldCommandSurvives() {
        Fixture f; f.thread("chat", "completed", 10); f.turn("chat", 1, "inProgress");
        f.item("chat", 1, "start", 2, "commandExecution", command(QJsonValue::Null)); QVERIFY(f.ready);
        auto s = f.scan(); QVERIFY(s.healthy); QCOMPARE(s.threads[0].turnId, "chat-10");
        QVERIFY(s.threads[0].workPending); QCOMPARE(code(s), "PENDING_TOOL");
        f.item("chat", 10, "exit", 11, "commandExecution", command(0)); QVERIFY(f.ready);
        s = f.scan(); QVERIFY(s.healthy); QCOMPARE(s.threads[0].status, "completed"); QVERIFY(!s.threads[0].workPending);
        QVERIFY(s.blockers.isEmpty());
    }
    void originalRevisionResolves() {
        Fixture f; f.thread("chat", "completed", 10);
        f.item("chat", 1, "original", 2, "commandExecution", command(QJsonValue::Null));
        f.item("chat", 10, "poll", 12, "commandExecution", command(QJsonValue::Null));
        f.exec(1, "UPDATE thread_history_projection_state SET next_rollout_ordinal=14"); QVERIFY(f.ready);
        QVERIFY(f.scan().threads[0].workPending);
        f.exec(1, "UPDATE thread_items SET updated_at_ordinal=?,item_json=? WHERE item_id=?",
            {13,QString::fromUtf8(QJsonDocument(command(0)).toJson(QJsonDocument::Compact)),"original"}); QVERIFY(f.ready);
        const auto s = f.scan(); QVERIFY(s.healthy); QVERIFY(!s.threads[0].workPending); QVERIFY(s.blockers.isEmpty());
    }
    void otherThreadCannotResolve() {
        Fixture f; f.thread("first"); f.thread("second");
        f.item("first", 1, "pending", 1, "commandExecution", command(QJsonValue::Null));
        f.item("second", 1, "exit", 1, "commandExecution", command(0)); QVERIFY(f.ready);
        const auto s = f.scan(); QVERIFY(s.threads[0].workPending); QVERIFY(!s.threads[1].workPending); QCOMPARE(code(s), "PENDING_TOOL");
    }
    void malformedCompletion_data() {
        QTest::addColumn<QString>("column");
        for (const QString &column : {QString("completed_at"),QString("rollout_end_ordinal"),QString("rollout_end_byte_offset")})
            QTest::newRow(qPrintable(column)) << column;
    }
    void malformedCompletion() {
        QFETCH(QString, column); Fixture f; f.thread("chat");
        f.exec(1, "UPDATE thread_turns SET " + column + "=NULL"); QVERIFY(f.ready); const auto s = f.scan();
        QVERIFY(!s.healthy); QCOMPARE(s.threads[0].status, "unknown"); QCOMPARE(code(s), "UNKNOWN_COMPLETION_STATE");
    }
    void endOrdinalLag() {
        Fixture f; f.thread("chat"); f.exec(1, "UPDATE thread_turns SET rollout_end_ordinal=4"); QVERIFY(f.ready);
        const auto s = f.scan(); QVERIFY(!s.healthy); QCOMPARE(code(s), "PROJECTION_LAG");
    }
    void endBytesLag() {
        Fixture f; f.thread("chat"); f.exec(1, "UPDATE thread_turns SET rollout_end_byte_offset=100"); QVERIFY(f.ready);
        const auto s = f.scan(); QVERIFY(!s.healthy); QCOMPARE(code(s), "PROJECTION_LAG");
    }
    void missingProjectionColumn() {
        Fixture f; f.thread("chat"); f.exec(1, "ALTER TABLE thread_history_projection_state DROP COLUMN next_rollout_ordinal"); QVERIFY(f.ready);
        const auto s = f.scan(); QVERIFY(!s.healthy); QCOMPARE(code(s), "STORE_INCOMPATIBLE");
    }
    void storeBytesPreserved() {
        Fixture f; f.thread("chat"); f.exec(2, "ALTER TABLE thread_goals ADD COLUMN objective TEXT");
        f.exec(2, "INSERT INTO thread_goals VALUES (?,?,?,?)", {"chat","complete",now,"PRIVATE_OBJECTIVE"}); QVERIFY(f.ready);
        const auto before = f.fingerprints(); const auto s = f.scan(); QVERIFY(s.healthy); QCOMPARE(f.fingerprints(), before);
        QVERIFY(!s.threads[0].reason.contains("PRIVATE_OBJECTIVE")); QVERIFY(!s.source.contains("PRIVATE_OBJECTIVE"));
    }
    void failureGoalMarker() {
        Fixture f; f.thread("chat", "failed"); f.exec(2, "INSERT INTO thread_goals VALUES (?,?,?)", {"chat","active",now}); QVERIFY(f.ready);
        const auto failed = f.scan(); QVERIFY(failed.healthy); QCOMPARE(failed.threads[0].status, "blocked");
        QCOMPARE(failed.threads[0].turnStatus, "failed"); QVERIFY(failed.threads[0].workPending);
        f.exec(1, "UPDATE thread_turns SET status='interrupted'"); QVERIFY(f.ready);
        const auto interrupted = f.scan(); QCOMPARE(interrupted.threads[0].turnStatus, "interrupted");
        QVERIFY(failed.revision != interrupted.revision);
    }
    void interruptionQueueMarker() {
        Fixture f; f.thread("chat", "interrupted"); f.exec(3, "INSERT INTO queued_items VALUES (?,?,?)", {"item","chat",now}); QVERIFY(f.ready);
        const auto s = f.scan(); QVERIFY(s.healthy); QCOMPARE(s.threads[0].status, "blocked");
        QCOMPARE(s.threads[0].turnStatus, "interrupted"); QVERIFY(s.threads[0].workPending);
    }
    void terminalPendingTool_data() {
        QTest::addColumn<QString>("terminal"); QTest::newRow("failed") << QString("failed"); QTest::newRow("interrupted") << QString("interrupted");
    }
    void terminalPendingTool() {
        QFETCH(QString, terminal); Fixture f; f.thread("chat", terminal);
        f.item("chat", 1, "tool", 1, "commandExecution", command(QJsonValue::Null)); QVERIFY(f.ready);
        const auto s = f.scan(); QVERIFY(s.healthy); QCOMPARE(s.threads[0].status, "blocked");
        QCOMPARE(s.threads[0].turnStatus, terminal); QVERIFY(s.threads[0].workPending); QCOMPARE(code(s), "PENDING_TOOL");
    }
    void invalidProcessExit_data() {
        QTest::addColumn<QString>("exitJson");
        QTest::newRow("boolean") << QString("true");
        QTest::newRow("string") << QString("\"pending\"");
        QTest::newRow("fraction") << QString("0.5");
    }
    void invalidProcessExit() {
        QFETCH(QString, exitJson); Fixture f; f.thread("chat");
        const auto json = QJsonDocument::fromJson(("{\"status\":\"completed\",\"processId\":\"42\",\"exitCode\":" + exitJson + "}").toUtf8()).object();
        f.item("chat", 1, "command", 1, "commandExecution", json); QVERIFY(f.ready);
        const auto s = f.scan(); QVERIFY(!s.healthy); QCOMPARE(code(s), "UNKNOWN_ITEM_STATE");
        QVERIFY(s.threads[0].workPending); QCOMPARE(s.threads[0].turnStatus, "completed");
    }
    void supersededTerminalMarkerPersists() {
        Fixture f; f.thread("chat", "completed", 10); QVERIFY(f.ready);
        const auto clean = f.scan(); QCOMPARE(clean.threads[0].terminalOrdinal, qint64(-1));
        f.turn("chat", 1, "failed"); f.turn("chat", 3, "interrupted"); QVERIFY(f.ready);
        const auto terminal = f.scan(); QVERIFY(terminal.healthy);
        QCOMPARE(terminal.threads[0].turnStatus, "completed"); QCOMPARE(terminal.threads[0].turnId, "chat-10");
        QCOMPARE(terminal.threads[0].terminalOrdinal, qint64(3)); QVERIFY(!terminal.threads[0].workPending);
        QVERIFY(terminal.revision != clean.revision);
    }
    void corruptTerminalOrdinal_data() {
        QTest::addColumn<QString>("ordinalSql");
        QTest::newRow("negative") << QString("-2");
        QTest::newRow("text") << QString("'broken'");
        QTest::newRow("fraction") << QString("0.5");
        QTest::newRow("null") << QString("NULL");
    }
    void corruptTerminalOrdinal() {
        QFETCH(QString, ordinalSql); Fixture f; f.thread("chat", "completed", 10); f.turn("chat", 1, "failed");
        f.exec(1, "UPDATE thread_turns SET rollout_ordinal=" + ordinalSql + " WHERE status='failed'"); QVERIFY(f.ready);
        const auto result = f.scan(); QVERIFY(!result.healthy); QCOMPARE(code(result), "UNKNOWN_TERMINAL_STATE");
    }
    void activeToolsGoalsAndQueueKeepAllThreeChatsRunning() {
        Fixture f; f.thread("a", "inProgress"); f.thread("b", "inProgress"); f.thread("c", "inProgress");
        f.item("a", 1, "mcp", 1, "mcpToolCall", {{"status","inProgress"}});
        f.item("b", 1, "command", 1, "commandExecution", command(QJsonValue::Null));
        f.item("c", 1, "change", 1, "fileChange", {{"status","inProgress"}});
        f.exec(2, "INSERT INTO thread_goals VALUES (?,?,?)", {"a","active",now});
        f.exec(3, "INSERT INTO queued_items VALUES (?,?,?)", {"item","b",now}); QVERIFY(f.ready);
        const auto result = f.scan(); QVERIFY(result.healthy); QCOMPARE(result.threads.size(), 3);
        for (const auto &thread : result.threads) {
            QCOMPARE(thread.status, "running"); QCOMPARE(thread.turnStatus, "inProgress"); QVERIFY(thread.workPending);
        }
        QCOMPARE(result.blockers.size(), 5);
    }
    void earlyProjectionLagRetainsLaterActiveInventory() {
        Fixture f; f.thread("a", "inProgress"); f.thread("b", "inProgress"); f.thread("c", "inProgress");
        QFile file(f.rollout("a")); QVERIFY(file.open(QIODevice::Append)); file.write("unprojected event\n"); file.close();
        f.item("a", 1, "pending", 1, "mcpToolCall", {{"status","inProgress"}}); QVERIFY(f.ready);
        const auto result = f.scan(); QVERIFY(!result.healthy); QCOMPARE(result.threads.size(), 3);
        QCOMPARE(code(result), "PROJECTION_LAG"); QCOMPARE(result.threads[0].status, "unknown");
        QCOMPARE(result.threads[0].turnStatus, "inProgress"); QVERIFY(result.threads[0].workPending);
        QVERIFY(result.threads[0].reason.contains("projection"));
        QCOMPARE(result.threads[1].status, "running"); QCOMPARE(result.threads[2].status, "running");
    }
    void earlyHistoryMismatchRetainsLaterActiveInventory() {
        Fixture f; f.thread("a", "inProgress"); f.thread("b", "inProgress"); f.thread("c", "inProgress");
        f.exec(0, "UPDATE threads SET history_mode='future' WHERE id='a'"); QVERIFY(f.ready);
        const auto result = f.scan(); QVERIFY(!result.healthy); QCOMPARE(result.threads.size(), 3);
        QCOMPARE(code(result), "STORE_INCOMPATIBLE"); QCOMPARE(result.threads[0].status, "unknown");
        QCOMPARE(result.threads[1].status, "running"); QCOMPARE(result.threads[2].status, "running");
    }
};
QTEST_GUILESS_MAIN(MonitorTest)
#include "monitor_test.moc"
