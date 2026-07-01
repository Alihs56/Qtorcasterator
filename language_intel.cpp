#include "language_intel.h"

LanguageIntelliService::LanguageIntelliService()
{
    registerDefaults();
}

void LanguageIntelliService::registerDefaults()
{
    LanguageInfo cpp;
    cpp.id = "cpp";
    cpp.displayName = "C++";
    cpp.extensions = {"h", "hpp", "cpp", "cc", "cxx"};
    cpp.treeSitterGrammar = "tree-sitter-cpp";
    cpp.lspServer = "clangd";
    cpp.lspCommand = "clangd --background-index";
    cpp.linterCommand = "clang-tidy";
    cpp.promptSuffix = "C++ code, follow RAII, use Qt parent-child ownership";
    m_registry["cpp"] = cpp;

    LanguageInfo python;
    python.id = "python";
    python.displayName = "Python";
    python.extensions = {"py"};
    python.treeSitterGrammar = "tree-sitter-python";
    python.lspServer = "pylsp";
    python.lspCommand = "pylsp";
    python.linterCommand = "flake8";
    python.promptSuffix = "Python code, follow PEP 8, use type hints";
    m_registry["python"] = python;

    LanguageInfo js;
    js.id = "javascript";
    js.displayName = "JavaScript";
    js.extensions = {"js", "mjs"};
    js.treeSitterGrammar = "tree-sitter-javascript";
    js.lspServer = "typescript-language-server";
    js.lspCommand = "typescript-language-server --stdio";
    js.linterCommand = "eslint";
    js.promptSuffix = "JavaScript code, use const/let, modern syntax";
    m_registry["javascript"] = js;

    LanguageInfo ts;
    ts.id = "typescript";
    ts.displayName = "TypeScript";
    ts.extensions = {"ts", "tsx"};
    ts.treeSitterGrammar = "tree-sitter-typescript";
    ts.lspServer = "typescript-language-server";
    ts.lspCommand = "typescript-language-server --stdio";
    ts.linterCommand = "eslint";
    ts.promptSuffix = "TypeScript code, use strict types, modern syntax";
    m_registry["typescript"] = ts;

    LanguageInfo rust;
    rust.id = "rust";
    rust.displayName = "Rust";
    rust.extensions = {"rs"};
    rust.treeSitterGrammar = "tree-sitter-rust";
    rust.lspServer = "rust-analyzer";
    rust.lspCommand = "rust-analyzer";
    rust.linterCommand = "clippy-driver";
    rust.promptSuffix = "Rust code, follow ownership rules, no unsafe blocks";
    m_registry["rust"] = rust;

    LanguageInfo go;
    go.id = "go";
    go.displayName = "Go";
    go.extensions = {"go"};
    go.treeSitterGrammar = "tree-sitter-go";
    go.lspServer = "gopls";
    go.lspCommand = "gopls";
    go.linterCommand = "golint";
    go.promptSuffix = "Go code, follow conventions, proper error handling";
    m_registry["go"] = go;

    LanguageInfo java;
    java.id = "java";
    java.displayName = "Java";
    java.extensions = {"java"};
    java.treeSitterGrammar = "tree-sitter-java";
    java.lspServer = "eclipse.jdt.ls";
    java.lspCommand = "java -jar eclipse.jdt.ls";
    java.linterCommand = "checkstyle";
    java.promptSuffix = "Java code, follow conventions, proper OOP patterns";
    m_registry["java"] = java;

    LanguageInfo csharp;
    csharp.id = "csharp";
    csharp.displayName = "C#";
    csharp.extensions = {"cs"};
    csharp.treeSitterGrammar = "tree-sitter-c-sharp";
    csharp.lspServer = "omnisharp";
    csharp.lspCommand = "omnisharp";
    csharp.linterCommand = "StyleCopAnalyzers";
    csharp.promptSuffix = "C# code, follow .NET conventions, proper patterns";
    m_registry["csharp"] = csharp;
}

LanguageIntelliService::LanguageInfo LanguageIntelliService::detect(const QString &filePath) const
{
    int dotPos = filePath.lastIndexOf('.');
    if (dotPos <= 0) return {};
    QString ext = filePath.mid(dotPos + 1).toLower();
    return detectByExtension(ext);
}

LanguageIntelliService::LanguageInfo LanguageIntelliService::detectByExtension(const QString &ext) const
{
    QString lowerExt = ext.toLower();

    // C/C++
    if (lowerExt == "h" || lowerExt == "hpp" || lowerExt == "cpp" || 
        lowerExt == "cc" || lowerExt == "cxx" || lowerExt == "c") {
        return m_registry.value("cpp");
    }
    // Python
    if (lowerExt == "py") {
        return m_registry.value("python");
    }
    // JavaScript
    if (lowerExt == "js" || lowerExt == "mjs") {
        return m_registry.value("javascript");
    }
    // TypeScript
    if (lowerExt == "ts" || lowerExt == "tsx") {
        return m_registry.value("typescript");
    }
    // Rust
    if (lowerExt == "rs") {
        return m_registry.value("rust");
    }
    // Go
    if (lowerExt == "go") {
        return m_registry.value("go");
    }
    // Java
    if (lowerExt == "java") {
        return m_registry.value("java");
    }
    // C#
    if (lowerExt == "cs") {
        return m_registry.value("csharp");
    }
    return {};
}

QString LanguageIntelliService::promptContext(const QString &filePath) const
{
    LanguageInfo info = detect(filePath);
    if (info.id.isEmpty()) return "Generic code";
    return QString("Write %1: %2").arg(info.displayName, info.promptSuffix);
}
