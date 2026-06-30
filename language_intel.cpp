#include "language_intel.h"
#include "SyntaxHighlighter.h"
#include <QFileInfo>

LanguageIntelliService::LanguageIntelliService()
{
    registerDefaults();
}

void LanguageIntelliService::registerDefaults()
{
    m_registry["cpp"] = {
        "cpp", "C++ (Qt 6 / C++20)",
        {"cpp", "h", "hpp", "c", "cc", "cxx", "hh", "hxx", "cuh"},
        "cpp", "clangd", "clangd --background-index=false",
        "g++ -Wall -Wextra -fsyntax-only",
        "\n\nLANGUAGE RULES: You are editing C++20 code for Qt 6. "
        "Prefer modern signal/slot syntax (function pointers) over old string-based syntax. "
        "Use QPointer where ownership is shared. Honor RAII. "
        "Check Qt parent-child ownership. Do not use raw new without parent."
    };
    m_registry["python"] = {
        "python", "Python",
        {"py"},
        "python", "pyright", "pyright-langserver --stdio",
        "ruff check",
        "\n\nLANGUAGE RULES: You are editing Python code. "
        "Follow PEP 8. Type hints are preferred. "
        "No manual memory management — rely on GC."
    };
    m_registry["javascript"] = {
        "javascript", "JavaScript / TypeScript",
        {"js", "jsx", "mjs", "cjs", "ts", "tsx"},
        "javascript", "typescript-language-server", "typescript-language-server --stdio",
        "eslint",
        "\n\nLANGUAGE RULES: You are editing JavaScript/TypeScript. "
        "Use const/let, avoid var. Prefer async/await over promises. "
        "TypeScript: strict types, no implicit any."
    };
    m_registry["java"] = {
        "java", "Java / Kotlin / Scala",
        {"java", "kt", "scala"},
        "java", "jdtls", "jdtls",
        "javac",
        "\n\nLANGUAGE RULES: You are editing Java-family code. "
        "Use final where appropriate. Prefer interfaces over abstract classes. "
        "Kotlin: data classes, null safety."
    };
    m_registry["csharp"] = {
        "csharp", "C#",
        {"cs"},
        "c_sharp", "omnisharp", "OmniSharp",
        "dotnet build",
        "\n\nLANGUAGE RULES: You are editing C#. "
        "Use async/await, nullable reference types, record types where appropriate."
    };
    m_registry["rust"] = {
        "rust", "Rust",
        {"rs"},
        "rust", "rust-analyzer", "rust-analyzer",
        "cargo check",
        "\n\nLANGUAGE RULES: You are editing Rust. "
        "Prefer ownership and borrowing over clones. Use Result/Option properly. "
        "No unsafe blocks unless explicitly requested."
    };
    m_registry["go"] = {
        "go", "Go",
        {"go"},
        "go", "gopls", "gopls",
        "go vet",
        "\n\nLANGUAGE RULES: You are editing Go. "
        "Error handling with if err != nil. Defer for cleanup. "
        "Interfaces are implicit. Context propagation."
    };
    m_registry["ruby"] = {
        "ruby", "Ruby",
        {"rb"},
        "ruby", "solargraph", "solargraph stdio",
        "rubocop",
        "\n\nLANGUAGE RULES: You are editing Ruby. "
        "Idiomatic Ruby: blocks, symbols, duck typing. "
        "No manual memory management."
    };
    m_registry["php"] = {
        "php", "PHP",
        {"php"},
        "php", "php-language-server", "php-language-server",
        "php -l",
        "\n\nLANGUAGE RULES: You are editing PHP. "
        "Strict types where possible. Avoid raw SQL — use PDO/ORM. "
        "PSR-12 coding style."
    };
    m_registry["sql"] = {
        "sql", "SQL",
        {"sql"},
        "sql", "", "",
        "sqlfluff lint",
        "\n\nLANGUAGE RULES: You are editing SQL. "
        "Use parameterized queries to prevent injection. "
        "Prefer CTEs for readability. Explicit JOIN conditions."
    };
    m_registry["html"] = {
        "html", "HTML",
        {"html", "htm"},
        "html", "", "",
        "htmlhint",
        "\n\nLANGUAGE RULES: You are editing HTML. "
        "Semantic elements. Accessibility (aria-labels). "
        "Avoid inline styles/scripts."
    };
    m_registry["json"] = {
        "json", "JSON",
        {"json"},
        "json", "", "",
        "jsonlint",
        "\n\nLANGUAGE RULES: You are editing JSON. "
        "Valid JSON only. No comments. Consistent ordering if schema requires it."
    };
    m_registry["yaml"] = {
        "yaml", "YAML",
        {"yaml", "yml"},
        "yaml", "", "",
        "yamllint",
        "\n\nLANGUAGE RULES: You are editing YAML. "
        "Indentation sensitive. Use quotes for strings containing special chars."
    };
    m_registry["bash"] = {
        "bash", "Bash / Shell",
        {"sh", "bash", "zsh", "fish"},
        "bash", "", "",
        "shellcheck",
        "\n\nLANGUAGE RULES: You are editing Shell scripts. "
        "Always quote variables. Use set -euo pipefail. "
        "Prefer portable POSIX sh when possible."
    };
}

LanguageIntelliService::LanguageInfo LanguageIntelliService::detect(const QString &filePath) const
{
    if (filePath.isEmpty())
        return {"unknown", "Unknown", {}, "", "", "", "", ""};
    QString ext = QFileInfo(filePath).suffix().toLower();
    return detectByExtension(ext);
}

LanguageIntelliService::LanguageInfo LanguageIntelliService::detectByExtension(const QString &ext) const
{
    for (auto it = m_registry.constBegin(); it != m_registry.constEnd(); ++it) {
        if (it.value().extensions.contains(ext))
            return it.value();
    }
    return {"unknown", "Unknown", {}, "", "", "", "", ""};
}

QString LanguageIntelliService::promptContext(const QString &filePath) const
{
    if (filePath.isEmpty())
        return {};
    LanguageInfo info = detect(filePath);
    if (info.id == "unknown")
        return {};
    return info.promptSuffix;
}
