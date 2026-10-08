#include "main_window.h"
#include "controller.h"
#include "window_style.h"
#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QScrollBar>
#include <QSet>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QTableWidget>
#include <QTabWidget>
#include <QVBoxLayout>
#include <algorithm>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace csa {
namespace {
QLabel *label(QWidget *parent, const char *name, bool wrap = true) {
    auto *widget = new QLabel(parent);
    widget->setObjectName(QString::fromLatin1(name));
    widget->setWordWrap(wrap);
    widget->setTextFormat(Qt::PlainText);
    return widget;
}
QTableWidget *table(QWidget *parent, const char *name, int columns) {
    auto *widget = new QTableWidget(0, columns, parent);
    widget->setObjectName(QString::fromLatin1(name));
    widget->setEditTriggers(QAbstractItemView::NoEditTriggers);
    widget->setSelectionBehavior(QAbstractItemView::SelectRows);
    widget->setSelectionMode(QAbstractItemView::SingleSelection);
    widget->setAlternatingRowColors(true);
    widget->setShowGrid(false);
    widget->verticalHeader()->hide();
    widget->verticalHeader()->setDefaultSectionSize(34);
    widget->horizontalHeader()->setStretchLastSection(true);
    return widget;
}
QString localTime(qint64 milliseconds) {
    return milliseconds > 0 ? QDateTime::fromMSecsSinceEpoch(milliseconds).toLocalTime().toString("MMM d, HH:mm:ss") : QStringLiteral("—");
}
} // namespace

MainWindow::MainWindow(Controller &controller, QWidget *parent)
    : QMainWindow(parent), controller_(controller) {
    setObjectName("mainWindow");
    setWindowTitle("Codex Shutdown Automation");
    setWindowIcon(utilityIcon());
    resize(1100, 740);
    setMinimumSize(800, 600);
    applyWindowStyle(this);
    buildUi();
    buildTray();
    connect(&controller_, &Controller::changed, this, &MainWindow::refresh);
    refresh();
}

QString MainWindow::text(const char *english, const char *arabic) const {
    return QString::fromUtf8(arabic_ ? arabic : english);
}

void MainWindow::buildUi() {
    auto *central = new QWidget(this);
    setCentralWidget(central);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(24, 20, 24, 16);
    layout->setSpacing(12);
    auto *header = new QHBoxLayout;
    title_ = label(central, "productTitle", false);
    title_->setText("Codex Shutdown Automation");
    monitorStatus_ = label(central, "monitorStatus", false);
    language_ = new QComboBox(central);
    language_->setObjectName("languageCombo");
    language_->addItem("English", "en");
    language_->addItem(QString::fromUtf8("العربية"), "ar");
    header->addWidget(title_);
    header->addStretch();
    header->addWidget(monitorStatus_);
    header->addWidget(language_);
    layout->addLayout(header);
    simulation_ = label(central, "simulationBanner");
    layout->addWidget(simulation_);
    statePanel_ = new QFrame(central);
    statePanel_->setObjectName("statePanel");
    auto *stateLayout = new QHBoxLayout(statePanel_);
    stateLayout->setContentsMargins(20, 16, 20, 16);
    auto *stateCopy = new QVBoxLayout;
    stateName_ = label(statePanel_, "stateName", false);
    description_ = label(statePanel_, "stateDescription");
    detail_ = label(statePanel_, "stateDetail");
    stateCopy->addWidget(stateName_);
    stateCopy->addWidget(description_);
    stateCopy->addWidget(detail_);
    stateLayout->addLayout(stateCopy, 1);
    auto *actions = new QVBoxLayout;
    enable_ = new QPushButton(statePanel_);
    enable_->setObjectName("enableButton");
    cancel_ = new QPushButton(statePanel_);
    cancel_->setObjectName("cancelButton");
    actions->addWidget(enable_);
    actions->addWidget(cancel_);
    stateLayout->addLayout(actions);
    layout->addWidget(statePanel_);
    error_ = label(central, "errorMessage");
    layout->addWidget(error_);
    tabs_ = new QTabWidget(central);
    tabs_->setObjectName("viewTabs");
    auto *overview = new QWidget(tabs_);
    auto *overviewLayout = new QHBoxLayout(overview);
    auto *work = new QVBoxLayout;
    blockers_ = new QListWidget(overview);
    blockers_->setObjectName("blockerList");
    blockers_->setMaximumHeight(100);
    work->addWidget(blockers_);
    threads_ = table(overview, "chatsTable", 4);
    threads_->setColumnWidth(0, 250);
    threads_->setColumnWidth(1, 120);
    threads_->setColumnWidth(2, 135);
    work->addWidget(threads_, 1);
    overviewLayout->addLayout(work, 1);
    timing_ = new QGroupBox(overview);
    timing_->setFixedWidth(242);
    auto *settingsLayout = new QVBoxLayout(timing_);
    settleLabel_ = label(timing_, "settleLabel");
    countdownLabel_ = label(timing_, "countdownLabel");
    settle_ = new QSpinBox(timing_);
    countdown_ = new QSpinBox(timing_);
    settle_->setObjectName("settleSpin");
    countdown_->setObjectName("countdownSpin");
    settle_->setRange(60, 3600);
    countdown_->setRange(60, 3600);
    settleLabel_->setBuddy(settle_);
    countdownLabel_->setBuddy(countdown_);
    save_ = new QPushButton(timing_);
    save_->setObjectName("saveSettingsButton");
    settingsNote_ = label(timing_, "settingsNote");
    startup_ = label(timing_, "startupStatus");
    const QList<QWidget *> settingsWidgets = {settleLabel_, settle_, countdownLabel_, countdown_, save_, settingsNote_, startup_};
    for (QWidget *widget : settingsWidgets) settingsLayout->addWidget(widget);
    settingsLayout->addStretch();
    overviewLayout->addWidget(timing_);
    tabs_->addTab(overview, QString());
    history_ = table(tabs_, "historyTable", 3);
    history_->setColumnWidth(0, 170);
    history_->setColumnWidth(1, 170);
    tabs_->addTab(history_, QString());
    layout->addWidget(tabs_, 1);
    auto *footerLayout = new QHBoxLayout;
    footer_ = label(central, "footerNote");
    quit_ = new QPushButton(central);
    quit_->setObjectName("quitButton");
    footerLayout->addWidget(footer_, 1);
    footerLayout->addWidget(quit_);
    layout->addLayout(footerLayout);
    connect(enable_, &QPushButton::clicked, &controller_, &Controller::arm);
    connect(cancel_, &QPushButton::clicked, &controller_, &Controller::cancel);
    connect(save_, &QPushButton::clicked, this, &MainWindow::saveSettings);
    connect(quit_, &QPushButton::clicked, this, &MainWindow::quitMonitor);
    auto edited = [this] { dirty_ = true; savedFeedback_ = false; refresh(); };
    connect(settle_, &QSpinBox::valueChanged, this, edited);
    connect(countdown_, &QSpinBox::valueChanged, this, edited);
    connect(language_, &QComboBox::currentIndexChanged, this, [this] {
        Settings settings = controller_.view().settings;
        settings.language = language_->currentData().toString();
        if (controller_.configure(settings)) { savedFeedback_ = true; refresh(); }
    });
}

void MainWindow::buildTray() {
    tray_ = new QSystemTrayIcon(utilityIcon(), this);
    tray_->setObjectName("monitorTray");
    auto *menu = new QMenu(this);
    showAction_ = menu->addAction(QString());
    cancelAction_ = menu->addAction(QString());
    menu->addSeparator();
    quitAction_ = menu->addAction(QString());
    connect(showAction_, &QAction::triggered, this, &MainWindow::showWindow);
    connect(cancelAction_, &QAction::triggered, &controller_, &Controller::cancel);
    connect(quitAction_, &QAction::triggered, this, &MainWindow::quitMonitor);
    connect(tray_, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) showWindow();
    });
    tray_->setContextMenu(menu);
    if (QSystemTrayIcon::isSystemTrayAvailable()) tray_->show();
}

QString MainWindow::statusText(const QString &status) const {
    if (status == "running") return text("Running", "قيد التنفيذ");
    if (status == "completed") return text("Completed", "مكتملة");
    if (status == "failed") return text("Failed", "فشلت");
    if (status == "interrupted") return text("Interrupted", "توقفت");
    if (status == "blocked") return text("Needs attention", "تحتاج انتباهك");
    if (status == "unknown") return text("Unknown", "غير معروفة");
    return status;
}

QString MainWindow::decisionText(const QString &type) const {
    if (type == "started") return text("Monitor started", "بدأ المراقب");
    if (type == "armed") return text("Enabled once", "تفعيل لمرة واحدة");
    if (type == "cancelled") return text("Cancelled", "أُلغي الإذن");
    if (type == "settings-saved") return text("Settings saved", "حُفظت الإعدادات");
    if (type == "authorization-expired") return text("Permission expired", "انتهى الإذن");
    if (type == "shutdown-checkpoint") return text("Decision saved", "حُفظ القرار");
    if (type == "shutdown-requested") return text("Shutdown requested", "طُلب الإغلاق");
    if (type == "shutdown-deferred") return text("Shutdown deferred", "تأجّل الإغلاق");
    if (type == "shutdown-command-accepted") return text("Command accepted", "قُبل الأمر");
    return type;
}

void MainWindow::refresh() {
    const ViewState view = controller_.view();
    arabic_ = view.settings.language == "ar";
    setLayoutDirection(arabic_ ? Qt::RightToLeft : Qt::LeftToRight);
    const QSignalBlocker languageBlock(language_), settleBlock(settle_), countdownBlock(countdown_);
    language_->setCurrentIndex(arabic_ ? 1 : 0);
    language_->setAccessibleName(text("Language", "اللغة"));
    language_->setToolTip(text("Changing language cancels current permission.", "تغيير اللغة يلغي الإذن الحالي."));
    if (!dirty_) { settle_->setValue(view.settings.settleSeconds); countdown_->setValue(view.settings.countdownSeconds); }
    settle_->setSuffix(text(" seconds", " ثانية"));
    countdown_->setSuffix(text(" seconds", " ثانية"));
    settle_->setAccessibleName(text("Idle confirmation seconds", "ثواني تأكيد السكون"));
    countdown_->setAccessibleName(text("Final countdown seconds", "ثواني العدّ التنازلي"));
    const QString phase = view.policy.phase;
    QString state = text("OFF", "غير مفعّل");
    QString description = text("Shutdown is off. Your computer will keep running.", "الإغلاق غير مفعّل. سيظل جهازك يعمل.");
    if (phase == "waiting") { state = text("Enabled · waiting", "مفعّل · في الانتظار"); description = text("Waiting for every monitored chat and pending work to finish.", "ننتظر اكتمال كل المحادثات والأعمال المعلّقة."); }
    if (phase == "settling") { state = text("Confirming idle", "تأكيد السكون"); description = text("All work finished. Confirming saved state stays quiet.", "اكتمل العمل. نتأكد من استمرار سكون الحالة المحفوظة."); }
    if (phase == "countdown") { state = text("Shutdown in %1 seconds", "الإغلاق خلال %1 ثانية").arg(view.policy.remainingSeconds); description = text("Cancel now to keep this computer running.", "ألغِ الآن لإبقاء جهازك يعمل."); }
    if (phase == "requesting") { state = text("Shutdown requested", "تم طلب الإغلاق"); description = text("One-time permission consumed. Normal Windows shutdown requested.", "انتهى إذن المرة الواحدة. طُلب إغلاق Windows بالطريقة العادية."); }
    stateName_->setText(state);
    description_->setText(description);
    detail_->setText(phase == "settling" ? text("%1 seconds remaining", "متبقٍ %1 ثانية").arg(view.policy.remainingSeconds) : view.policy.armed ? text("%1 chats in this batch", "%1 محادثات في هذه الدفعة").arg(view.policy.trackedCount) : text("Enable is temporary. Every restart begins OFF.", "التفعيل مؤقت. يبدأ كل تشغيل جديد دون تفعيل."));
    statePanel_->setProperty("phase", phase);
    statePanel_->style()->unpolish(statePanel_);
    statePanel_->style()->polish(statePanel_);
    const bool pending = std::any_of(view.monitor.threads.cbegin(), view.monitor.threads.cend(), [](const Thread &thread) { return thread.workPending || thread.status == "running"; });
    enable_->setText(text("&Enable for this batch", "فعّل لهذه الدفعة"));
    cancel_->setText(text("&Cancel shutdown", "إلغاء الإغلاق"));
    enable_->setEnabled(view.monitor.healthy && pending && phase == "disarmed");
    enable_->setToolTip(!view.monitor.healthy ? text("Resolve the monitor issue first.", "عالج مشكلة المراقب أولًا.") : !pending ? text("Start Codex work first.", "ابدأ العمل في Codex أولًا.") : text("One-time permission for all current and new local work.", "إذن لمرة واحدة لكل الأعمال المحلية الحالية والجديدة."));
    cancel_->setEnabled(view.policy.armed);
    save_->setEnabled(dirty_ && phase != "requesting");
    language_->setEnabled(phase != "requesting");
    quit_->setEnabled(phase != "requesting");
    monitorStatus_->setText(view.monitor.healthy ? text("Monitor available", "المراقب متاح") : text("Monitor unavailable", "المراقب غير متاح"));
    simulation_->setVisible(view.simulation);
    simulation_->setText(text("SIMULATION · This instance cannot shut down your computer.", "محاكاة · هذا التشغيل لا يمكنه إغلاق جهازك."));
    error_->setText(view.error);
    error_->setVisible(!view.error.isEmpty());
    const auto running = std::count_if(view.monitor.threads.cbegin(), view.monitor.threads.cend(),
        [](const Thread &thread) { return thread.turnStatus == "inProgress"; });
    tabs_->setTabText(0, text("Overview · %1 running", "نظرة عامة · %1 جارية").arg(running));
    tabs_->setTabText(1, text("Decision history · 3 days", "سجل القرارات · 3 أيام"));
    timing_->setTitle(text("Shutdown timing", "توقيت الإغلاق"));
    settleLabel_->setText(text("Idle confirmation", "تأكيد السكون"));
    countdownLabel_->setText(text("Final countdown", "العدّ التنازلي الأخير"));
    save_->setText(text("Save timing", "حفظ التوقيت"));
    const QString settingsHelp = text("Saved for next time. Saving timing or language cancels current permission.", "يُحفظ للمرة القادمة. حفظ التوقيت أو اللغة يلغي الإذن الحالي.");
    settingsNote_->setText(savedFeedback_ ? text("Settings saved. Shutdown is OFF.\n", "حُفظت الإعدادات. الإغلاق غير مفعّل.\n") + settingsHelp : settingsHelp);
    startup_->setText(view.startupEnabled ? text("Starts at Windows sign-in", "يعمل عند تسجيل الدخول") : text("Start at sign-in: not installed", "التشغيل عند الدخول: غير مثبّت"));
    footer_->setText(text("Closing this window keeps monitoring. Decisions are saved before shutdown; project files are not changed.", "إغلاق النافذة يُبقي المراقبة. تُحفظ القرارات قبل الإغلاق؛ لا تتغيّر ملفات المشاريع."));
    quit_->setText(text("Quit monitor", "إيقاف المراقب"));
    QSet<QString> seen;
    blockers_->clear();
    for (const auto &source : {view.monitor.blockers, view.policy.blockers}) for (const Blocker &blocker : source) {
        if (!seen.contains(blocker.message)) { seen.insert(blocker.message); blockers_->addItem(blocker.message); }
    }
    blockers_->setVisible(blockers_->count() > 0);
    blockers_->setAccessibleName(text("Reasons shutdown is waiting", "أسباب انتظار الإغلاق"));
    renderThreads(view);
    renderHistory(view);
    showAction_->setText(text("Show window", "عرض النافذة"));
    cancelAction_->setText(text("Cancel shutdown", "إلغاء الإغلاق"));
    quitAction_->setText(text("Quit monitor", "إيقاف المراقب"));
    cancelAction_->setEnabled(view.policy.armed);
    quitAction_->setEnabled(phase != "requesting");
    tray_->setToolTip("Codex Shutdown Automation — " + state);
}

void MainWindow::renderThreads(const ViewState &view) {
    const int scroll = threads_->verticalScrollBar()->value();
    threads_->setHorizontalHeaderLabels({text("Chat", "المحادثة"), text("State", "الحالة"), text("Last activity", "آخر نشاط"), text("Details", "التفاصيل")});
    QVector<Thread> rows = view.monitor.threads;
    std::stable_sort(rows.begin(), rows.end(), [](const Thread &a, const Thread &b) {
        return (a.workPending || a.status == "running") > (b.workPending || b.status == "running");
    });
    threads_->setRowCount(rows.size());
    for (int row = 0; row < rows.size(); ++row) {
        const Thread &thread = rows[row];
        const QStringList values = {thread.title, statusText(thread.status), localTime(thread.lastActivityAt), thread.reason};
        for (int column = 0; column < values.size(); ++column) {
            auto *item = new QTableWidgetItem(values[column]);
            item->setToolTip(values[column]);
            threads_->setItem(row, column, item);
        }
    }
    threads_->verticalScrollBar()->setValue(scroll);
}

void MainWindow::renderHistory(const ViewState &view) {
    const int scroll = history_->verticalScrollBar()->value();
    history_->setHorizontalHeaderLabels({text("Local time", "الوقت المحلي"), text("Decision", "القرار"), text("Details", "التفاصيل")});
    history_->setRowCount(view.history.size());
    for (int row = 0; row < view.history.size(); ++row) {
        const QJsonObject &entry = view.history[row];
        const qint64 timestamp = QDateTime::fromString(entry.value("timestamp").toString(), Qt::ISODate).toMSecsSinceEpoch();
        const QStringList values = {localTime(timestamp), decisionText(entry.value("type").toString()), entry.value("message").toString()};
        for (int column = 0; column < values.size(); ++column) history_->setItem(row, column, new QTableWidgetItem(values[column]));
    }
    history_->verticalScrollBar()->setValue(scroll);
}

void MainWindow::saveSettings() {
    Settings settings = controller_.view().settings;
    settings.settleSeconds = settle_->value();
    settings.countdownSeconds = countdown_->value();
    if (controller_.configure(settings)) { dirty_ = false; savedFeedback_ = true; refresh(); }
}
void MainWindow::showWindow() {
    showNormal();
#ifdef Q_OS_WIN
    // STARTUPINFO can override Qt's first native show after a hidden tray launch.
    if (QApplication::platformName() == "windows")
        ::ShowWindow(reinterpret_cast<HWND>(winId()), SW_RESTORE);
#endif
    raise(); activateWindow();
}
void MainWindow::quitMonitor() { controller_.cancel(); tray_->hide(); QApplication::quit(); }
void MainWindow::closeEvent(QCloseEvent *event) {
    if (QSystemTrayIcon::isSystemTrayAvailable()) { hide(); event->ignore(); }
    else { event->ignore(); error_->setText(text("System tray unavailable. Use Quit monitor to stop safely.", "شريط النظام غير متاح. استخدم إيقاف المراقب للتوقف بأمان.")); error_->show(); }
}
} // namespace csa
