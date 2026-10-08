#include "store.h"
#include "policy.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#ifdef Q_OS_WIN
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif

namespace csa {
static void durableFlush(QFileDevice &file) {
    if (!file.flush()) throw std::runtime_error("Unable to flush saved state.");
#ifdef Q_OS_WIN
    if (!FlushFileBuffers(reinterpret_cast<HANDLE>(_get_osfhandle(file.handle()))))
        throw std::runtime_error("Windows could not persist saved state.");
#else
    if (::fsync(file.handle()) != 0) throw std::runtime_error("Unable to persist saved state.");
#endif
}
Store::Store(QString directory) : directory_(std::move(directory)) {
    if (!QDir().mkpath(directory_)) throw std::runtime_error("Cannot create the application state directory.");
}
Settings Store::settings() const {
    QFile file(directory_ + "/settings.json");
    if (!file.exists()) return {};
    if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error("Cannot read settings.");
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject())
        throw std::runtime_error("Saved settings are invalid. Shutdown stays OFF.");
    const auto json = document.object();
    if (json.keys().size() != 3 || !json.contains("settleSeconds") || !json.contains("countdownSeconds") || !json.contains("language"))
        throw std::runtime_error("Saved settings have unsupported fields.");
    for (const auto &key : {"settleSeconds", "countdownSeconds"}) {
        const double value = json.value(key).toDouble(-1);
        if (!json.value(key).isDouble() || !std::isfinite(value) || value < 60 || value > 3600 || value != std::floor(value))
            throw std::runtime_error("Saved timing is invalid.");
    }
    Settings result{json["settleSeconds"].toInt(), json["countdownSeconds"].toInt(), json["language"].toString()};
    validateSettings(result); return result;
}
void Store::saveSettings(const Settings &settings) {
    validateSettings(settings);
    const QJsonObject json{{"settleSeconds", settings.settleSeconds}, {"countdownSeconds", settings.countdownSeconds}, {"language", settings.language}};
    QSaveFile file(directory_ + "/settings.json");
    file.setDirectWriteFallback(false);
    const auto data = QJsonDocument(json).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size()) throw std::runtime_error("Cannot save settings.");
    durableFlush(file);
    if (!file.commit()) throw std::runtime_error("Cannot commit saved settings.");
}
void Store::append(const QJsonObject &decision) {
    QFile file(directory_ + "/decisions.jsonl");
    const auto data = QJsonDocument(decision).toJson(QJsonDocument::Compact) + '\n';
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append) || file.write(data) != data.size())
        throw std::runtime_error("Cannot save the shutdown decision. Shutdown is blocked.");
    durableFlush(file);
}
static QByteArray readHistoryLine(QFile &source) {
    const QByteArray line = source.readLine();
    if (source.error() != QFileDevice::NoError) throw std::runtime_error("Cannot read decision history for retention.");
    return line;
}
static bool recentDecision(const QByteArray &line, qint64 nowUtcMs) {
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(line, &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject()) return false;
    const auto timestamp = QDateTime::fromString(document.object().value("timestamp").toString(), Qt::ISODateWithMs);
    const bool explicitZone = timestamp.timeSpec() == Qt::UTC || timestamp.timeSpec() == Qt::OffsetFromUTC;
    if (!timestamp.isValid() || !explicitZone) return false;
    constexpr qint64 retentionMs = 72 * 60 * 60 * 1000LL;
    const qint64 recordedAt = timestamp.toUTC().toMSecsSinceEpoch();
    return recordedAt > nowUtcMs - retentionMs && recordedAt <= nowUtcMs;
}
void Store::pruneHistory(qint64 nowUtcMs) {
    QFile source(directory_ + "/decisions.jsonl");
    if (!source.exists()) return;
    if (!source.open(QIODevice::ReadOnly)) throw std::runtime_error("Cannot read decision history for retention.");
    bool removed = false;
    while (!source.atEnd()) {
        if (!recentDecision(readHistoryLine(source), nowUtcMs)) { removed = true; break; }
    }
    if (!removed) return;
    if (!source.seek(0)) throw std::runtime_error("Cannot reread decision history for retention.");
    QSaveFile replacement(source.fileName());
    replacement.setDirectWriteFallback(false);
    if (!replacement.open(QIODevice::WriteOnly)) throw std::runtime_error("Cannot replace decision history for retention.");
    while (!source.atEnd()) {
        QByteArray line = readHistoryLine(source);
        if (!recentDecision(line, nowUtcMs)) continue;
        if (!line.endsWith('\n')) line += '\n';
        if (replacement.write(line) != line.size()) throw std::runtime_error("Cannot write retained decision history.");
    }
    // Windows must release the source handle before the atomic replacement.
    source.close();
    durableFlush(replacement);
    if (!replacement.commit()) throw std::runtime_error("Cannot commit retained decision history.");
}
QVector<QJsonObject> Store::history() const {
    QFile file(directory_ + "/decisions.jsonl");
    if (!file.exists()) return {};
    if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error("Cannot read decision history.");
    // Only the last 128 KiB is needed to render recent decisions.
    if (file.size() > 131072) { file.seek(file.size() - 131072); file.readLine(); }
    QVector<QJsonObject> entries;
    while (!file.atEnd()) {
        const auto document = QJsonDocument::fromJson(file.readLine());
        if (document.isObject()) entries.append(document.object());
    }
    if (entries.size() > 100) entries = entries.mid(entries.size() - 100);
    std::reverse(entries.begin(), entries.end());
    return entries;
}
} // namespace csa
