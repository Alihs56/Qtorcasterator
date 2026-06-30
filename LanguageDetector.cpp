#include "LanguageDetector.h"
#include <QFileInfo>
#include <QDebug>

LanguageDetector::LanguageDetector(QObject *parent)
    : QObject(parent)
{
    registerLanguages();
}

void LanguageDetector::registerLanguages()
{
    m_extensionMap.insert("cpp", Cpp);
    m_extensionMap.insert("cc", Cpp);
    m_extensionMap.insert("cxx", Cpp);
    m_extensionMap.insert("h", Cpp);
    m_extensionMap.insert("hpp", Cpp);
    m_extensionMap.insert("hxx", Cpp);
    m_extensionMap.insert("c", Cpp);

    m_extensionMap.insert("py", Python);
    m_extensionMap.insert("pyw", Python);

    m_extensionMap.insert("js", JavaScript);
    m_extensionMap.insert("mjs", JavaScript);
    m_extensionMap.insert("cjs", JavaScript);

    m_extensionMap.insert("ts", TypeScript);
    m_extensionMap.insert("tsx", TypeScript);

    m_extensionMap.insert("java", Java);

    m_extensionMap.insert("cs", CSharp);

    m_extensionMap.insert("rs", Rust);

    m_extensionMap.insert("go", Go);

    m_languageNames.insert(Cpp, "C++");
    m_languageNames.insert(Python, "Python");
    m_languageNames.insert(JavaScript, "JavaScript");
    m_languageNames.insert(TypeScript, "TypeScript");
    m_languageNames.insert(Java, "Java");
    m_languageNames.insert(CSharp, "C#");
    m_languageNames.insert(Rust, "Rust");
    m_languageNames.insert(Go, "Go");
    m_languageNames.insert(Unknown, "Unknown");

    m_extensions.insert(Cpp, {"cpp", "cc", "cxx", "c", "h", "hpp", "hxx"});
    m_extensions.insert(Python, {"py", "pyw"});
    m_extensions.insert(JavaScript, {"js", "mjs", "cjs"});
    m_extensions.insert(TypeScript, {"ts", "tsx"});
    m_extensions.insert(Java, {"java"});
    m_extensions.insert(CSharp, {"cs"});
    m_extensions.insert(Rust, {"rs"});
    m_extensions.insert(Go, {"go"});

    m_treeSitterGrammars.insert(Cpp, "cpp");
    m_treeSitterGrammars.insert(Python, "python");
    m_treeSitterGrammars.insert(JavaScript, "javascript");
    m_treeSitterGrammars.insert(TypeScript, "typescript");
    m_treeSitterGrammars.insert(Java, "java");
    m_treeSitterGrammars.insert(CSharp, "csharp");
    m_treeSitterGrammars.insert(Rust, "rust");
    m_treeSitterGrammars.insert(Go, "go");

    m_lspServers.insert(Cpp, "clangd");
    m_lspServers.insert(Python, "pyright");
    m_lspServers.insert(JavaScript, "ts-ls");
    m_lspServers.insert(TypeScript, "ts-ls");
    m_lspServers.insert(Java, "jdtls");
    m_lspServers.insert(CSharp, "omnisharp");
    m_lspServers.insert(Rust, "rust-analyzer");
    m_lspServers.insert(Go, "gopls");

    m_linterCommands.insert(Cpp, "clang-tidy");
    m_linterCommands.insert(Python, "flake8");
    m_linterCommands.insert(JavaScript, "eslint");
    m_linterCommands.insert(TypeScript, "eslint");
    m_linterCommands.insert(Java, "checkstyle");
    m_linterCommands.insert(CSharp, "dotnet-format");
    m_linterCommands.insert(Rust, "clippy");
    m_linterCommands.insert(Go, "golint");

    m_promptSuffixes.insert(Cpp, "Use modern C++17/Qt6 style. Prefer smart pointers, range-for, auto, "
                              "and Qt conventions (PascalCase for classes, camelCase for methods, "
                              "m_ prefix for members). Use Q_PROPERTY for exposed properties.");
    m_promptSuffixes.insert(Python, "Follow PEP 8. Use type hints, dataclasses where appropriate, "
                              "f-strings, and pathlib for filesystem operations.");
    m_promptSuffixes.insert(JavaScript, "Use ES6+ syntax, async/await, arrow functions, "
                               "destructuring, and modular design.");
    m_promptSuffixes.insert(TypeScript, "Use strict TypeScript 5.x. Enable strict null checks, "
                               "discriminated unions, generic constraints, and readonly modifiers.");
    m_promptSuffixes.insert(Java, "Use Java 17+ features, records where appropriate, "
                               "var for local type inference, and Optional for null safety.");
    m_promptSuffixes.insert(CSharp, "Use C# 10+ features, records, file-scoped namespaces, "
                               "and nullable reference types.");
    m_promptSuffixes.insert(Rust, "Use idiomatic Rust: ownership/borrowing, Result/Option, "
                              "match expressions, clippy-compliant style.");
    m_promptSuffixes.insert(Go, "Use idiomatic Go: error returns, interfaces, goroutines, "
                             "defer/panic/recover, and gofmt-compliant formatting.");
}

LanguageDetector::Language LanguageDetector::detect(const QString &filePath)
{
    QFileInfo info(filePath);
    QString ext = info.suffix().toLower();

    if (!ext.isEmpty() && m_extensionMap.contains(ext))
        return m_extensionMap.value(ext);

    return Unknown;
}

LanguageDetector::Language LanguageDetector::detectByExtension(const QString &ext) const
{
    QString normalized = ext.toLower();
    if (normalized.startsWith("."))
        normalized = normalized.mid(1);
    if (m_extensionMap.contains(normalized))
        return m_extensionMap.value(normalized);
    return Unknown;
}

LanguageDetector::Language LanguageDetector::detectByContent(const QString &content) const
{
    Q_UNUSED(content);
    return Unknown;
}

QString LanguageDetector::languageName(Language lang) const
{
    return m_languageNames.value(lang, "Unknown");
}

QStringList LanguageDetector::extensionsFor(Language lang) const
{
    return m_extensions.value(lang, {});
}

QString LanguageDetector::treeSitterGrammar(Language lang) const
{
    return m_treeSitterGrammars.value(lang, "");
}

QString LanguageDetector::lspServer(Language lang) const
{
    return m_lspServers.value(lang, "");
}

QString LanguageDetector::linterCommand(Language lang) const
{
    return m_linterCommands.value(lang, "");
}

QString LanguageDetector::promptSuffix(Language lang) const
{
    return m_promptSuffixes.value(lang, "");
}
