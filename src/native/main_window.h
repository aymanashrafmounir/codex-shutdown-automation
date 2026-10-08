#pragma once
#include <QMainWindow>
#include <QString>

class QAction;
class QCloseEvent;
class QComboBox;
class QFrame;
class QGroupBox;
class QLabel;
class QListWidget;
class QPushButton;
class QSpinBox;
class QSystemTrayIcon;
class QTableWidget;
class QTabWidget;

namespace csa {
class Controller;
struct ViewState;

class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(Controller &controller, QWidget *parent = nullptr);
    void showWindow();
protected:
    void closeEvent(QCloseEvent *event) override;
private:
    void buildUi();
    void buildTray();
    void refresh();
    void renderThreads(const ViewState &view);
    void renderHistory(const ViewState &view);
    void saveSettings();
    void quitMonitor();
    QString text(const char *english, const char *arabic) const;
    QString statusText(const QString &status) const;
    QString decisionText(const QString &type) const;

    Controller &controller_;
    bool arabic_ = false, dirty_ = false, savedFeedback_ = false;
    QFrame *statePanel_ = nullptr;
    QLabel *title_ = nullptr, *monitorStatus_ = nullptr, *simulation_ = nullptr;
    QLabel *stateName_ = nullptr, *description_ = nullptr, *detail_ = nullptr;
    QLabel *error_ = nullptr, *footer_ = nullptr, *settingsNote_ = nullptr;
    QLabel *settleLabel_ = nullptr, *countdownLabel_ = nullptr, *startup_ = nullptr;
    QComboBox *language_ = nullptr;
    QPushButton *enable_ = nullptr, *cancel_ = nullptr, *save_ = nullptr, *quit_ = nullptr;
    QTabWidget *tabs_ = nullptr;
    QTableWidget *threads_ = nullptr, *history_ = nullptr;
    QListWidget *blockers_ = nullptr;
    QGroupBox *timing_ = nullptr;
    QSpinBox *settle_ = nullptr, *countdown_ = nullptr;
    QSystemTrayIcon *tray_ = nullptr;
    QAction *showAction_ = nullptr, *cancelAction_ = nullptr, *quitAction_ = nullptr;
};
} // namespace csa
