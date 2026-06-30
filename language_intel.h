#ifndef LANGUAGE_INTEL_H
#define LANGUAGE_INTEL_H

#include <QString>
#include <QStringList>
#include <QMap>

class LanguageIntelliService
{
public:
    struct LanguageInfo
    {
        QString id;
        QString displayName;
        QStringList extensions;
        QString treeSitterGrammar;
        QString lspServer;
        QString lspCommand;
        QString linterCommand;
        QString promptSuffix;
    };

    explicit LanguageIntelliService();

    LanguageInfo detect(const QString &filePath) const;
    LanguageInfo detectByExtension(const QString &ext) const;
    QString promptContext(const QString &filePath) const;

private:
    QMap<QString, LanguageInfo> m_registry;
    void registerDefaults();
};

#endif
