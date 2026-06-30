#ifndef LANGUAGEDETECTOR_H
#define LANGUAGEDETECTOR_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QMap>
#include <QSet>

class LanguageDetector : public QObject
{
    Q_OBJECT
public:
    enum Language {
        Cpp = 0,
        Python,
        JavaScript,
        TypeScript,
        Java,
        CSharp,
        Rust,
        Go,
        Unknown
    };
    Q_ENUM(Language)

    explicit LanguageDetector(QObject *parent = nullptr);
    ~LanguageDetector() override = default;

    Language detect(const QString &filePath);
    Language detectByExtension(const QString &ext) const;
    Language detectByContent(const QString &content) const;
    QString languageName(Language lang) const;
    QStringList extensionsFor(Language lang) const;
    QString treeSitterGrammar(Language lang) const;
    QString lspServer(Language lang) const;
    QString linterCommand(Language lang) const;
    QString promptSuffix(Language lang) const;

signals:
    void detectionCompleted(const QString &filePath, Language language);

private:
    QMap<QString, Language> m_extensionMap;
    QMap<Language, QString> m_languageNames;
    QMap<Language, QStringList> m_extensions;
    QMap<Language, QString> m_treeSitterGrammars;
    QMap<Language, QString> m_lspServers;
    QMap<Language, QString> m_linterCommands;
    QMap<Language, QString> m_promptSuffixes;
    QSet<QString> m_genericExtensions;

    void registerLanguages();
};

#endif // LANGUAGEDETECTOR_H
