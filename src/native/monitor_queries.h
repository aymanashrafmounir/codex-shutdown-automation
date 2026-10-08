#pragma once
#include <QMap>
#include <QStringList>
#include <QVector>

namespace csa::monitorQueries {
inline const QStringList files = {"state_5.sqlite", "thread_history_1.sqlite", "goals_1.sqlite", "queue_1.sqlite"};
inline const QVector<QMap<QString, QStringList>> schemas = {
    {{"threads", {"id", "title", "rollout_path", "history_mode", "updated_at_ms"}}},
    {{"thread_turns", {"thread_id", "turn_id", "status", "rollout_ordinal", "started_at", "completed_at",
                      "rollout_end_ordinal", "rollout_end_byte_offset"}},
     {"thread_history_projection_state", {"thread_id", "next_rollout_byte_offset", "next_rollout_ordinal"}},
     {"thread_items", {"thread_id", "turn_id", "item_id", "rollout_ordinal", "updated_at_ordinal", "item_type", "item_json"}}},
    {{"thread_goals", {"thread_id", "status", "updated_at_ms"}}},
    {{"queued_items", {"id", "thread_id", "updated_at_ms"}}},
};
inline const QString latestTurns = R"sql(
    SELECT thread_id, turn_id, status, rollout_ordinal, started_at, completed_at,
           rollout_end_ordinal, rollout_end_byte_offset FROM (
        SELECT *, ROW_NUMBER() OVER (PARTITION BY thread_id ORDER BY rollout_ordinal DESC) AS rank
        FROM thread_turns
    ) WHERE rank = 1 ORDER BY thread_id
)sql";
inline const QString terminalOrdinals = R"sql(
    SELECT thread_id, MAX(rollout_ordinal) AS terminal_ordinal,
        SUM(CASE WHEN typeof(rollout_ordinal) != 'integer' OR rollout_ordinal < 0 THEN 1 ELSE 0 END) AS invalid_ordinals
    FROM thread_turns WHERE status IN ('failed', 'interrupted') GROUP BY thread_id
)sql";
inline const QString relevantItems = R"sql(
    WITH latest AS (
        SELECT thread_id, turn_id, ROW_NUMBER() OVER (
            PARTITION BY thread_id ORDER BY rollout_ordinal DESC
        ) AS rank FROM thread_turns
    ), processes AS (
        SELECT i.thread_id, i.item_type,
            json_extract(i.item_json, '$.status') AS status,
            json_extract(i.item_json, '$.processId') AS process_id,
            json_extract(i.item_json, '$.exitCode') AS exit_code,
            json_type(i.item_json, '$.exitCode') AS exit_type,
            ROW_NUMBER() OVER (
                PARTITION BY i.thread_id, json_extract(i.item_json, '$.processId')
                ORDER BY MAX(i.rollout_ordinal, i.updated_at_ordinal) DESC,
                    i.rollout_ordinal DESC, i.item_id DESC
            ) AS rank
        FROM thread_items i WHERE i.item_type = 'commandExecution'
            AND json_extract(i.item_json, '$.processId') IS NOT NULL
    )
    SELECT i.thread_id, i.item_type,
        json_extract(i.item_json, '$.status') AS status,
        json_extract(i.item_json, '$.processId') AS process_id,
        json_extract(i.item_json, '$.exitCode') AS exit_code,
        json_type(i.item_json, '$.exitCode') AS exit_type
    FROM thread_items i JOIN latest l ON i.thread_id = l.thread_id AND i.turn_id = l.turn_id
    WHERE l.rank = 1 AND i.item_type IN (
        'commandExecution', 'mcpToolCall', 'dynamicToolCall', 'fileChange', 'collabAgentToolCall', 'collabToolCall'
    ) AND NOT (i.item_type = 'commandExecution' AND json_extract(i.item_json, '$.processId') IS NOT NULL)
    UNION ALL
    SELECT thread_id, item_type, status, process_id, exit_code, exit_type FROM processes WHERE rank = 1
)sql";
} // namespace csa::monitorQueries
