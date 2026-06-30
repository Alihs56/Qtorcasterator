#include "MainWindow.h"
#include "settings_manager.h"
#include "logger.h"
#include <QApplication>

int main(int argc, char *argv[]) {
    // ===== از نسخه قدیمی: تنظیم پلتفرم برای کامپوزیتورهای X11 =====
    qputenv("QT_QPA_PLATFORM", "xcb");

    QApplication app(argc, argv);
    app.setStyle("Fusion");

    QFont font = app.font();
    font.setPointSize(10);
    app.setFont(font);

    Logger::instance()->info("System", "=== Qt AI Agent Orchestrator ===");
    Logger::instance()->info("System", "Starting up...");

    SettingsManager settings;
    Logger::instance()->info("System",
                             "Models directory: " + settings.modelsDirectory());
    Logger::instance()->info("System",
                             "llama-server: " + settings.llamaServerPath());

    MainWindow mainWindow;
    mainWindow.setWindowTitle("Qt AI Agent Orchestrator [Llama.cpp Backend]");
    mainWindow.resize(1400, 900);
    mainWindow.show();
    
    // ===== از نسخه قدیمی: اطمینان از نمایش پنجره =====
    mainWindow.raise();
    mainWindow.activateWindow();

    Logger::instance()->info("System", "Window shown, entering event loop...");

    int result = app.exec();

    Logger::instance()->info("System", "Shutting down.");
    return result;
}