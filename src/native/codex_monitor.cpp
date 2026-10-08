#include "codex_monitor.h"
#include "monitor_queries.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QUuid>
#include <QVariantMap>
#include <algorithm>
#include <array>
#include <memory>
#include <utility>

namespace csa {
namespace {
using Row = QVariantMap;
using Rows = QVector<Row>;
struct Failure { QString code, message; };

class Store {
public:
    explicit Store(const QString &path) : name(QUuid::createUuid().toString()),
        db(QSqlDatabase::addDatabase("QSQLITE", name)) {
        db.setDatabaseName(path);
        db.setConnectOptions("QSQLITE_OPEN_READONLY");
    }
    ~Store() {
        db.close();
        db = QSqlDatabase();
        QSqlDatabase::removeDatabase(name);
    }
    QString name;
    QSqlDatabase db;
    qint64 version = 0;
};

Rows read(const QSqlDatabase &db, const QString &sql) {
    QSqlQuery query(db);
    if (!query.exec(sql))
        throw Failure{"MONITOR_READ_FAILED", "Codex stores could not be read safely. Shutdown is blocked."};
    Rows rows;
    const auto record = query.record();
    while (query.next()) {
        Row row;
        for (int i = 0; i < record.count(); ++i) row.insert(record.fieldName(i), query.value(i));
        rows.append(row);
    }
    if (query.lastError().isValid())
        throw Failure{"MONITOR_READ_FAILED", "Codex stores could not be read safely. Shutdown is blocked."};
    return rows;
}

bool integer(const QVariant &value) {
    const auto type = value.metaType().id();
    return !value.isNull() && (type == QMetaType::Int || type == QMetaType::LongLong ||
        type == QMetaType::UInt || type == QMetaType::ULongLong);
}

qint64 version(const QSqlDatabase &db) {
    const auto rows = read(db, "PRAGMA data_version");
    if (rows.size() != 1 || !integer(rows.first().value("data_version")))
        throw Failure{"MONITOR_READ_FAILED", "Codex store version could not be read safely."};
    return rows.first().value("data_version").toLongLong();
}

void validate(const Store &store, int index) {
    const auto &schema = monitorQueries::schemas[index];
    for (auto table = schema.cbegin(); table != schema.cend(); ++table) {
        QSet<QString> columns;
        for (const auto &row : read(store.db, "PRAGMA table_info(\"" + table.key() + "\")"))
            columns.insert(row.value("name").toString());
        for (const auto &column : table.value()) {
            if (!columns.contains(column))
                throw Failure{"STORE_INCOMPATIBLE", monitorQueries::files[index] + " has an unsupported " + table.key() + " schema."};
        }
    }
}

QMap<QString, Row> keyed(const Rows &rows, const QString &key) {
    QMap<QString, Row> result;
    for (const auto &row : rows) result.insert(row.value(key).toString(), row);
    return result;
}

void pending(Observation &observation, const QMap<QString, int> &indices, const QString &id, const QString &reason) {
    if (!indices.contains(id)) return;
    auto &thread = observation.threads[indices.value(id)];
    thread.status = "blocked";
    thread.workPending = true;
    thread.reason = reason;
    // turnStatus is independent: pending work must never mask a failed/interrupted turn.
}

void readPending(const std::array<std::unique_ptr<Store>, 4> &stores, Observation &observation,
                 const QMap<QString, int> &indices) {
    const QSet<QString> itemStatuses = {"inProgress", "completed", "failed", "declined", "interrupted"};
    for (const auto &item : read(stores[1]->db, monitorQueries::relevantItems)) {
        const auto status = item.value("status").toString();
        if (!itemStatuses.contains(status)) {
            observation.blockers.append(Blocker{"UNKNOWN_ITEM_STATUS", "A Codex tool item has an unsupported status."});
        } else if (item.value("item_type").toString() == "commandExecution" &&
                   !item.value("exit_code").isNull() &&
                   (item.value("exit_type").toString() != "integer" || !integer(item.value("exit_code")))) {
            pending(observation, indices, item.value("thread_id").toString(), "The command exit state is invalid.");
            observation.blockers.append(Blocker{"UNKNOWN_ITEM_STATE", "A Codex command has an invalid exit state."});
        } else if (status == "inProgress" || (item.value("item_type").toString() == "commandExecution" &&
                   !item.value("process_id").isNull() && item.value("exit_code").isNull())) {
            pending(observation, indices, item.value("thread_id").toString(), "A tool or command session is unresolved.");
            observation.blockers.append(Blocker{"PENDING_TOOL", "A Codex tool or command session is unresolved."});
        }
    }
    const QSet<QString> goalStatuses = {"active", "paused", "blocked", "usage_limited", "budget_limited", "complete"};
    for (const auto &goal : read(stores[2]->db, "SELECT thread_id, status, updated_at_ms FROM thread_goals ORDER BY thread_id")) {
        auto status = goal.value("status").toString();
        if (!goalStatuses.contains(status)) {
            observation.blockers.append(Blocker{"UNKNOWN_GOAL_STATUS", "A Codex goal has an unsupported status."});
        } else if (status != "complete") {
            status.replace('_', ' ');
            const auto id = goal.value("thread_id").toString();
            pending(observation, indices, id, "The goal is " + status + ".");
            if (indices.contains(id)) observation.threads[indices.value(id)].lastActivityAt =
                qMax(observation.threads[indices.value(id)].lastActivityAt, goal.value("updated_at_ms").toLongLong());
            observation.blockers.append(Blocker{"UNFINISHED_GOAL", "A Codex goal is " + status + "."});
        }
    }
    const auto queued = read(stores[3]->db, "SELECT id, thread_id, updated_at_ms FROM queued_items ORDER BY id");
    for (const auto &item : queued) {
        const auto id = item.value("thread_id").toString();
        pending(observation, indices, id, "User work is queued.");
        if (indices.contains(id)) observation.threads[indices.value(id)].lastActivityAt =
            qMax(observation.threads[indices.value(id)].lastActivityAt, item.value("updated_at_ms").toLongLong());
    }
    if (!queued.isEmpty()) observation.blockers.append(Blocker{"PENDING_QUEUE", QString::number(queued.size()) + " Codex item(s) are waiting to run."});
}

void readThreads(const std::array<std::unique_ptr<Store>, 4> &stores, Observation &observation) {
    const auto metadata = keyed(read(stores[0]->db,
        "SELECT id, title, rollout_path, history_mode, updated_at_ms FROM threads ORDER BY id"), "id");
    const auto projection = keyed(read(stores[1]->db,
        "SELECT thread_id, next_rollout_byte_offset, next_rollout_ordinal FROM thread_history_projection_state"), "thread_id");
    const auto latest = keyed(read(stores[1]->db, monitorQueries::latestTurns), "thread_id");
    const auto terminals = keyed(read(stores[1]->db, monitorQueries::terminalOrdinals), "thread_id");
    for (auto turn = latest.cbegin(); turn != latest.cend(); ++turn) {
        if (!metadata.contains(turn.key()))
            throw Failure{"UNKNOWN_THREAD", "A Codex turn has no matching thread metadata. Shutdown is blocked."};
    }
    struct FileStamp { QString path; qint64 size, modified; };
    QVector<FileStamp> files;
    QMap<QString, int> indices;
    for (auto meta = metadata.cbegin(); meta != metadata.cend(); ++meta) {
        const Row turn = latest.value(meta.key());
        Thread thread;
        thread.id = meta.key();
        thread.title = meta.value().value("title").toString();
        if (thread.title.isEmpty()) thread.title = "Chat " + thread.id.left(8);
        thread.turnId = turn.value("turn_id").toString();
        thread.turnStatus = latest.contains(thread.id) ? turn.value("status").toString() : "unknown";
        if (terminals.contains(thread.id)) {
            const auto terminal = terminals.value(thread.id);
            if (!integer(terminal.value("terminal_ordinal")) || terminal.value("terminal_ordinal").toLongLong() < 0 ||
                terminal.value("invalid_ordinals").toLongLong() != 0)
                throw Failure{"UNKNOWN_TERMINAL_STATE", "A Codex terminal turn has invalid event metadata."};
            thread.terminalOrdinal = terminal.value("terminal_ordinal").toLongLong();
        }
        thread.status = "unknown";
        thread.workPending = thread.turnStatus == "inProgress";
        thread.lastActivityAt = std::max({meta.value().value("updated_at_ms").toLongLong(),
            turn.value("started_at").toLongLong() * 1000, turn.value("completed_at").toLongLong() * 1000});
        indices.insert(thread.id, observation.threads.size());
        observation.threads.append(thread);
        auto &result = observation.threads.last();
        if (!latest.contains(thread.id)) {
            result.reason = "Thread metadata has no projected turn state.";
            observation.blockers.append(Blocker{"UNKNOWN_THREAD_STATE", "A Codex thread has no projected turn state. Shutdown is blocked."});
            continue;
        }
        if (meta.value().value("history_mode").toString() != "paginated")
            throw Failure{"STORE_INCOMPATIBLE", "A Codex thread uses an unsupported history format. Shutdown is blocked."};
        if (thread.turnStatus == "completed" && (
            !integer(turn.value("completed_at")) || turn.value("completed_at").toLongLong() <= 0 ||
            !integer(turn.value("rollout_end_ordinal")) || turn.value("rollout_end_ordinal").toLongLong() < turn.value("rollout_ordinal").toLongLong() ||
            !integer(turn.value("rollout_end_byte_offset")) || turn.value("rollout_end_byte_offset").toLongLong() <= 0)) {
            result.reason = "The completed turn has incomplete completion metadata.";
            observation.blockers.append(Blocker{"UNKNOWN_COMPLETION_STATE", "A Codex completed turn has incomplete completion metadata."});
            continue;
        }
        const QFileInfo info(meta.value().value("rollout_path").toString());
        const Row projected = projection.value(thread.id);
        if (!info.isFile() || !projection.contains(thread.id) || projected.value("next_rollout_byte_offset").toLongLong() != info.size() ||
            !integer(projected.value("next_rollout_ordinal")) || projected.value("next_rollout_ordinal").toLongLong() <= turn.value("rollout_ordinal").toLongLong() ||
            (!turn.value("rollout_end_ordinal").isNull() && projected.value("next_rollout_ordinal").toLongLong() < turn.value("rollout_end_ordinal").toLongLong()) ||
            (!turn.value("rollout_end_byte_offset").isNull() && projected.value("next_rollout_byte_offset").toLongLong() < turn.value("rollout_end_byte_offset").toLongLong()))
            throw Failure{"PROJECTION_LAG", "Codex history has not caught up with its session files. Shutdown is blocked."};
        files.append(FileStamp{info.filePath(), info.size(), info.lastModified().toMSecsSinceEpoch()});
        if (thread.turnStatus == "inProgress") {
            result.status = "running"; result.reason = "Running or waiting for input or approval.";
        } else if (thread.turnStatus == "completed") result.status = "completed";
        else if (thread.turnStatus == "failed") { result.status = "failed"; result.reason = "The latest turn failed."; }
        else if (thread.turnStatus == "interrupted") { result.status = "blocked"; result.reason = "The latest turn was interrupted."; }
        else observation.blockers.append(Blocker{"UNKNOWN_TURN_STATUS", "A Codex turn has an unsupported status."});
    }
    readPending(stores, observation, indices);
    for (const auto &file : files) {
        const QFileInfo after(file.path);
        if (after.size() != file.size || after.lastModified().toMSecsSinceEpoch() != file.modified)
            throw Failure{"STORE_CHANGED", "Codex session files changed during this scan. Waiting for a consistent snapshot."};
    }
}

QString revision(const Observation &observation) {
    QJsonArray threads, blockers;
    for (const auto &thread : observation.threads) threads.append(QJsonObject{
        {"id", thread.id}, {"title", thread.title}, {"status", thread.status}, {"turnStatus", thread.turnStatus},
        {"turnId", thread.turnId}, {"terminalOrdinal", thread.terminalOrdinal},
        {"lastActivityAt", thread.lastActivityAt}, {"workPending", thread.workPending}, {"reason", thread.reason}});
    for (const auto &blocker : observation.blockers) blockers.append(QJsonObject{{"code", blocker.code}, {"message", blocker.message}});
    const auto bytes = QJsonDocument(QJsonObject{{"healthy", observation.healthy}, {"threads", threads}, {"blockers", blockers}}).toJson(QJsonDocument::Compact);
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
} // namespace

CodexMonitor::CodexMonitor(QString codexHome) : codexHome_(std::move(codexHome)) {}

Observation CodexMonitor::scan(qint64 now) {
    Observation observation;
    observation.capturedAt = now;
    observation.source = "Codex local SQLite stores (conservative)";
    try {
        std::array<std::unique_ptr<Store>, 4> stores;
        for (int i = 0; i < 4; ++i) {
            const auto path = QDir(codexHome_).filePath(monitorQueries::files[i]);
            if (!QFileInfo::exists(path)) throw Failure{"STORE_MISSING", monitorQueries::files[i] + " is missing. Monitoring is unavailable."};
            stores[i] = std::make_unique<Store>(path);
            if (!stores[i]->db.open()) throw Failure{"MONITOR_READ_FAILED", "Codex stores could not be read safely. Shutdown is blocked."};
            validate(*stores[i], i);
            stores[i]->version = version(stores[i]->db);
        }
        readThreads(stores, observation);
        for (const auto &store : stores) {
            if (version(store->db) != store->version)
                throw Failure{"STORE_CHANGED", "Codex state changed during this scan. Waiting for a consistent snapshot."};
        }
        observation.healthy = std::none_of(observation.blockers.cbegin(), observation.blockers.cend(),
            [](const Blocker &blocker) { return blocker.code.startsWith("UNKNOWN_"); });
    } catch (const Failure &failure) {
        observation.blockers.append(Blocker{failure.code, failure.message});
    } catch (...) {
        observation.blockers.append(Blocker{"MONITOR_READ_FAILED", "Codex stores could not be read safely. Shutdown is blocked."});
    }
    observation.revision = revision(observation);
    return observation;
}
} // namespace csa
