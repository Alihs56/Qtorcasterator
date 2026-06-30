#include "system_tray.h"
#include "logger.h"
#include <QApplication>
#include <QIcon>
#include <QPainter>   // از نسخه قدیمی
#include <QPixmap>    // از نسخه قدیمی

SystemTray::SystemTray(QObject *parent) : QObject(parent) {
    m_trayIcon = new QSystemTrayIcon(this);

    // ===== از نسخه قدیمی: آیکون با fallback =====
    QIcon icon = QIcon::fromTheme("computer", QIcon(":/icons/app.png"));
    if (icon.isNull()) {
        // Create a simple fallback icon
        QPixmap pixmap(64, 64);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setBrush(QColor("#6c63ff"));
        painter.setPen(Qt::NoPen);
        painter.drawRoundedRect(8, 8, 48, 48, 8, 8);
        painter.setPen(Qt::white);
        painter.setFont(QFont("Arial", 20, QFont::Bold));
        painter.drawText(pixmap.rect(), Qt::AlignCenter, "AI");
        icon = QIcon(pixmap);
    }
    m_trayIcon->setIcon(icon);
    m_trayIcon->setToolTip("Qt AI Orchestrator");

    createMenu();
    m_trayIcon->setContextMenu(m_menu);

    connect(m_trayIcon, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::DoubleClick ||
                    reason == QSystemTrayIcon::Trigger) {
                    emit showWindowRequested();
                }
            });
}

void SystemTray::createMenu() {
    m_menu = new QMenu();

    QString menuStyle =
        "QMenu { padding: 4px 0; }"
        "QMenu::item { padding: 6px 24px; font-size: 13px; }"
        "QMenu::separator { height: 1px; margin: 4px 8px; }";
    m_menu->setStyleSheet(menuStyle);

    m_showAction = m_menu->addAction("Show Window");
    connect(m_showAction, &QAction::triggered, this, &SystemTray::showWindowRequested);

    m_hideAction = m_menu->addAction("Hide to Tray");
    connect(m_hideAction, &QAction::triggered, this, &SystemTray::hideWindowRequested);

    m_menu->addSeparator();

    m_settingsAction = m_menu->addAction("Settings...");
    connect(m_settingsAction, &QAction::triggered, this, &SystemTray::openSettingsRequested);

    m_modelsAction = m_menu->addAction("Restart Always-On Models");
    m_modelsAction->setEnabled(false);
    connect(m_modelsAction, &QAction::triggered, this, [this]() {
        emit toggleAlwaysOnModels(true);
    });

    m_menu->addSeparator();

    m_quitAction = m_menu->addAction("Quit");
    connect(m_quitAction, &QAction::triggered, this, &SystemTray::quitRequested);
}

void SystemTray::show() {
    m_trayIcon->show();
}

void SystemTray::hide() {
    m_trayIcon->hide();
}

bool SystemTray::isVisible() const {
    return m_trayIcon->isVisible();
}

// ===== از نسخه قدیمی: showNotification با بررسی visibility =====
void SystemTray::showNotification(const QString &title, const QString &message,
                                  QSystemTrayIcon::MessageIcon icon, int durationMs) {
    if (m_trayIcon->isVisible()) {
        m_trayIcon->showMessage(title, message, icon, durationMs);
    }
}

void SystemTray::setModelCount(int running, int total) {
    m_trayIcon->setToolTip(QString("Qt AI Orchestrator\nModels: %1/%2 running")
                               .arg(running).arg(total));
}

void SystemTray::setVRAMStatus(const QString &text) {
    Q_UNUSED(text)
    // Could be used to update tooltip with VRAM info
}