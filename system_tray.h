#ifndef SYSTEM_TRAY_H
#define SYSTEM_TRAY_H

#include <QObject>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>
#include <QIcon>

class SystemTray : public QObject {
    Q_OBJECT
public:
    explicit SystemTray(QObject *parent = nullptr);

    void show();
    void hide();
    bool isVisible() const;
    void showNotification(const QString &title, const QString &message,
                          QSystemTrayIcon::MessageIcon icon = QSystemTrayIcon::Information,
                          int durationMs = 5000);

    void setModelCount(int running, int total);
    void setVRAMStatus(const QString &text);

signals:
    void showWindowRequested();
    void hideWindowRequested();
    void quitRequested();
    void toggleAlwaysOnModels(bool on);
    void openSettingsRequested();

private:
    void createMenu();

    QSystemTrayIcon *m_trayIcon;
    QMenu *m_menu;
    QAction *m_showAction;
    QAction *m_hideAction;
    QAction *m_settingsAction;
    QAction *m_modelsAction;
    QAction *m_quitAction;
};

#endif