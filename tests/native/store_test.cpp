#include "store.h"
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QtTest>
#include <stdexcept>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

using namespace csa;

namespace {
constexpr qint64 nowUtcMs = 1791460800000LL;
constexpr qint64 retentionMs = 72 * 60 * 60 * 1000LL;

QByteArray decisionLine(qint64 recordedAt, const QString &type) {
    const QString timestamp = QDateTime::fromMSecsSinceEpoch(recordedAt, QTimeZone::UTC).toString(Qt::ISODateWithMs);
    return QJsonDocument(QJsonObject{{"timestamp", timestamp}, {"type", type}}).toJson(QJsonDocument::Compact) + '\n';
}
bool writeFile(const QString &path, const QByteArray &contents) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size() && file.flush();
}
QByteArray readFile(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error("Cannot read test fixture.");
    return file.readAll();
}
#ifdef Q_OS_WIN
class JournalLock final {
public:
    explicit JournalLock(const QString &path)
        : handle_(CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)) {}
    ~JournalLock() { if (valid()) CloseHandle(handle_); }
    bool valid() const { return handle_ != INVALID_HANDLE_VALUE; }
private:
    HANDLE handle_;
};
#endif
} // namespace

class StoreTest final : public QObject {
    Q_OBJECT
private slots:
    void physicallyExpiresBoundaryAndPreservesRecentOrder() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        Store store(temporary.path());
        store.saveSettings({90, 180, "ar"});
        const QString settingsPath = temporary.filePath("settings.json");
        const QByteArray savedSettings = readFile(settingsPath);
        const QString journalPath = temporary.filePath("decisions.jsonl");
        const QByteArray first = decisionLine(nowUtcMs - 3600000, "first");
        const QByteArray oldestRetained = decisionLine(nowUtcMs - retentionMs + 1, "oldest-retained");
        const QByteArray current = decisionLine(nowUtcMs, "current");
        QVERIFY(writeFile(journalPath, decisionLine(nowUtcMs - retentionMs - 1, "expired") + first
            + decisionLine(nowUtcMs - retentionMs, "boundary") + oldestRetained + current));

        store.pruneHistory(nowUtcMs);

        QCOMPARE(readFile(journalPath), first + oldestRetained + current);
        QCOMPARE(readFile(settingsPath), savedSettings);
        QCOMPARE(store.settings().language, QString("ar"));
    }
    void discardsMalformedUndatedUnzonedAndFutureRecords() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        Store store(temporary.path());
        const QString journalPath = temporary.filePath("decisions.jsonl");
        const QByteArray retained = decisionLine(nowUtcMs - 1000, "retained");
        const QByteArray zeroOffset = "{\"timestamp\":\"2026-10-08T11:59:59.000+00:00\",\"type\":\"zero-offset\"}\n";
        const QByteArray invalid = "not-json\n[]\n{}\n{\"timestamp\":17}\n{\"timestamp\":\"invalid\"}\n"
            "{\"timestamp\":\"2026-10-08T11:59:59.000\"}\n";
        QVERIFY(writeFile(journalPath, invalid + retained + decisionLine(nowUtcMs + 1, "future") + zeroOffset));

        store.pruneHistory(nowUtcMs);

        QCOMPARE(readFile(journalPath), retained + zeroOffset);
    }
    void comparesExplicitOffsetsAsUtcInstants() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        Store store(temporary.path());
        const QString journalPath = temporary.filePath("decisions.jsonl");
        const QByteArray expired = "{\"timestamp\":\"2026-10-05T15:00:00.000+03:00\",\"type\":\"boundary\"}\n";
        const QByteArray retained = "{\"timestamp\":\"2026-10-08T15:00:00.000+03:00\",\"type\":\"current\"}\n";
        const QByteArray future = "{\"timestamp\":\"2026-10-08T15:00:00.001+03:00\",\"type\":\"future\"}\n";
        QVERIFY(writeFile(journalPath, expired + retained + future));

        store.pruneHistory(nowUtcMs);

        QCOMPARE(readFile(journalPath), retained);
    }
    void removesAllExpiredRecordsFromDisk() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        Store store(temporary.path());
        const QString journalPath = temporary.filePath("decisions.jsonl");
        QVERIFY(writeFile(journalPath, decisionLine(nowUtcMs - retentionMs, "boundary")));

        store.pruneHistory(nowUtcMs);

        QVERIFY(QFileInfo::exists(journalPath));
        QCOMPARE(readFile(journalPath), QByteArray());
    }
    void missingJournalIsNotCreated() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        Store store(temporary.path());

        store.pruneHistory(nowUtcMs);

        QVERIFY(!QFileInfo::exists(temporary.filePath("decisions.jsonl")));
    }
    void recentJournalIsNotRewritten() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        Store store(temporary.path());
        const QString journalPath = temporary.filePath("decisions.jsonl");
        const QByteArray original = decisionLine(nowUtcMs - 1000, "retained");
        QVERIFY(writeFile(journalPath, original));
        QFile journal(journalPath);
        QVERIFY(journal.open(QIODevice::ReadWrite));
        QVERIFY(journal.setFileTime(QDateTime::fromMSecsSinceEpoch(nowUtcMs - 3600000, QTimeZone::UTC), QFileDevice::FileModificationTime));
        journal.close();
        const QDateTime originalModified = QFileInfo(journalPath).lastModified();

        store.pruneHistory(nowUtcMs);

        QCOMPARE(readFile(journalPath), original);
        QCOMPARE(QFileInfo(journalPath).lastModified(), originalModified);
    }
    void atomicReplacementFailurePreservesOriginalJournal() {
#ifdef Q_OS_WIN
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        Store store(temporary.path());
        const QString journalPath = temporary.filePath("decisions.jsonl");
        const QByteArray original = decisionLine(nowUtcMs - retentionMs, "expired")
            + decisionLine(nowUtcMs - 1000, "retained");
        QVERIFY(writeFile(journalPath, original));
        // Denying delete sharing deterministically blocks replacement without changing permissions.
        JournalLock lock(journalPath);
        QVERIFY(lock.valid());

        QVERIFY_EXCEPTION_THROWN(store.pruneHistory(nowUtcMs), std::runtime_error);

        QCOMPARE(readFile(journalPath), original);
#else
        QSKIP("Windows deny-delete sharing is needed to exercise atomic replacement failure.");
#endif
    }
};

QTEST_GUILESS_MAIN(StoreTest)
#include "store_test.moc"
