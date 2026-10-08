#include "window_style.h"
#include <QFont>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QWidget>

namespace csa {
QIcon utilityIcon() {
    QPixmap image(64, 64);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#182b27"));
    painter.drawRoundedRect(QRectF(2, 2, 60, 60), 12, 12);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor("#f5f4ed"), 4, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(QPointF(32, 14), QPointF(32, 33));
    painter.drawArc(QRectF(16, 19, 32, 32), 135 * 16, 270 * 16);
    return QIcon(image);
}

void applyWindowStyle(QWidget *window) {
    QPalette palette = window->palette();
    palette.setColor(QPalette::Window, QColor("#f5f4ed"));
    palette.setColor(QPalette::Base, QColor("#fffef9"));
    palette.setColor(QPalette::AlternateBase, QColor("#edf0e8"));
    palette.setColor(QPalette::WindowText, QColor("#182b27"));
    palette.setColor(QPalette::Text, QColor("#182b27"));
    palette.setColor(QPalette::ButtonText, QColor("#182b27"));
    palette.setColor(QPalette::Highlight, QColor("#206246"));
    palette.setColor(QPalette::HighlightedText, QColor("#fffef9"));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor("#748277"));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#748277"));
    window->setPalette(palette);
    window->setFont(QFont("Segoe UI", 10));
    window->setStyleSheet(QStringLiteral(R"(
        QLabel#productTitle { font-size: 20px; font-weight: 600; }
        QLabel#stateName { font-size: 32px; font-weight: 600; }
        QLabel#stateDescription { font-size: 14px; }
        QLabel#stateDetail, QLabel#monitorStatus, QLabel#footerNote { color: #52645d; }
        QFrame#statePanel { background: #fffef9; border: 1px solid #d8dfd6; border-radius: 10px; }
        QFrame#statePanel[phase="waiting"], QFrame#statePanel[phase="settling"] { background: #e6efe8; }
        QFrame#statePanel[phase="countdown"] { background: #f5e9d6; }
        QLabel#simulationBanner { background: #f5e9d6; color: #713e0a; padding: 10px; border-radius: 6px; }
        QLabel#errorMessage { color: #a4362f; padding: 4px; }
        QPushButton { min-height: 26px; padding: 5px 12px; }
        QPushButton#enableButton { color: #fffef9; background: #206246; border: 1px solid #206246; border-radius: 6px; }
        QPushButton#enableButton:hover { background: #164c35; }
        QPushButton#enableButton:disabled { background: #d8dfd6; color: #52645d; border-color: #d8dfd6; }
        QPushButton:focus, QSpinBox:focus, QComboBox:focus { border: 2px solid #39735a; }
        QTableWidget { border: 1px solid #d8dfd6; gridline-color: #d8dfd6; }
        QHeaderView::section { background: #edf0e8; color: #52645d; border: 0; border-bottom: 1px solid #d8dfd6; padding: 8px; }
        QListWidget#blockerList { color: #713e0a; background: #f5e9d6; border: 1px solid #ddcbaa; }
        QGroupBox { font-weight: 600; border: 1px solid #d8dfd6; border-radius: 6px; margin-top: 12px; padding-top: 12px; }
        QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; padding: 0 6px; }
        QTabWidget::pane { border: 1px solid #d8dfd6; }
    )"));
}
} // namespace csa
