#ifndef SETTINGS_DIALOG_H
#define SETTINGS_DIALOG_H

#include <QDialog>
#include <QTabWidget>
#include <QLineEdit>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QMap>

class SettingsManager;
struct ModelConfigEntry;

struct ModelEditWidgets {
    QLineEdit *modelFile = nullptr;
    QPushButton *browseBtn = nullptr;
    QLineEdit *mmprojFile = nullptr;
    QPushButton *mmprojBrowseBtn = nullptr;
    QSpinBox *port = nullptr;
    QSpinBox *ctx = nullptr;
    QSpinBox *ngl = nullptr;
    QSpinBox *vram = nullptr;
    QSpinBox *batchSize = nullptr;
    QSpinBox *ubatchSize = nullptr;
    QDoubleSpinBox *temperature = nullptr;
    bool hasMMProj = false;        // از نسخه قدیمی
    QString schedule;              // از نسخه قدیمی
};

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(SettingsManager *settings, bool isDark = true, QWidget *parent = nullptr);

signals:
    void settingsApplied();

protected:
    void showEvent(QShowEvent *event) override;  // از نسخه قدیمی

private:
    void setupUI();
    void loadSettings();
    void saveSettings();
    void applyStyle();  // از نسخه قدیمی

    QWidget* createGeneralTab();    // از نسخه قدیمی
    QWidget* createPathsTab();      // از نسخه قدیمی
    QWidget* createModelsTab();     // از نسخه قدیمی
    QWidget* createModelGroup(const QString &key, const QString &displayName,
                              bool hasMMProj = false, const QString &schedule = "On-Demand");  // از نسخه قدیمی

    SettingsManager *m_settings;
    bool m_isDark;
    QMap<QString, ModelEditWidgets> m_modelWidgets;

    // General
    QComboBox *m_themeCombo = nullptr;
    QCheckBox *m_autoStartModels = nullptr;
    QCheckBox *m_minimizeToTray = nullptr;
    QCheckBox *m_notifications = nullptr;

    // Paths
    QLineEdit *m_llamaPath = nullptr;
    QLineEdit *m_modelsDir = nullptr;
    QSpinBox *m_vramLimit = nullptr;
};

#endif