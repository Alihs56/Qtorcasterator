#ifndef SYNTAXHIGHLIGHTER_H
#define SYNTAXHIGHLIGHTER_H

#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QRegularExpression>
#include <QStringList>
#include <QFileInfo>

class SyntaxHighlighter : public QSyntaxHighlighter {
public:
    enum Language {
        L_Cpp, L_Python, L_Js, L_Java, L_CSharp, L_Rust, L_Go,
        L_Sql, L_Html, L_Css, L_Json, L_Xml, L_Yaml, L_Bash, L_Ruby, L_Php, L_Unknown
    };

    explicit SyntaxHighlighter(QTextDocument *parent)
        : QSyntaxHighlighter(parent), m_language(L_Unknown) {}

    QTextCharFormat makeFormat(const QColor &color, bool bold = false, bool italic = false) const {
        QTextCharFormat fmt;
        fmt.setForeground(color);
        if (bold) fmt.setFontWeight(QFont::Bold);
        if (italic) fmt.setFontItalic(true);
        return fmt;
    }

    void addKeywords(const QStringList &words, const QTextCharFormat &fmt) {
        for (const QString &w : words) {
            Rule r;
            r.pattern = QRegularExpression(QString("\\b%1\\b").arg(QRegularExpression::escape(w)));
            r.format = fmt;
            m_rules.append(r);
        }
    }

    void setLanguage(Language lang) {
        m_language = lang;
        m_rules.clear();
        setupRules();
        rehighlight();
    }

    // ===== از نسخه قدیمی: detectLanguage کامل‌تر =====
    static Language detectLanguage(const QString &filePath) {
        QString ext = QFileInfo(filePath).suffix().toLower();
        
        // C/C++
        if (ext == "cpp" || ext == "h" || ext == "hpp" || ext == "c" || ext == "cc" ||
            ext == "cxx" || ext == "hh" || ext == "hxx" || ext == "cuh")
            return L_Cpp;
        
        // Python
        if (ext == "py")
            return L_Python;
        
        // JavaScript / TypeScript
        if (ext == "js" || ext == "jsx" || ext == "mjs" || ext == "cjs")
            return L_Js;
        if (ext == "ts" || ext == "tsx")
            return L_Js;
        
        // Java / Kotlin / Scala
        if (ext == "java" || ext == "kt" || ext == "scala")
            return L_Java;
        
        // C#
        if (ext == "cs")
            return L_CSharp;
        
        // Rust
        if (ext == "rs")
            return L_Rust;
        
        // Go
        if (ext == "go")
            return L_Go;
        
        // Ruby
        if (ext == "rb")
            return L_Ruby;
        
        // PHP
        if (ext == "php")
            return L_Php;
        
        // HTML / Vue / Svelte
        if (ext == "html" || ext == "htm" || ext == "xhtml" || ext == "vue" || ext == "svelte")
            return L_Html;
        
        // CSS / SCSS / Less / Sass
        if (ext == "css" || ext == "scss" || ext == "less" || ext == "sass")
            return L_Css;
        
        // JSON
        if (ext == "json")
            return L_Json;
        
        // XML / Qt / Apple
        if (ext == "xml" || ext == "svg" || ext == "plist" ||
            ext == "ui" || ext == "qrc" || ext == "pro" || ext == "pri")
            return L_Xml;
        
        // YAML
        if (ext == "yaml" || ext == "yml")
            return L_Yaml;
        
        // SQL
        if (ext == "sql")
            return L_Sql;
        
        // Shell
        if (ext == "sh" || ext == "bash" || ext == "zsh" || ext == "fish")
            return L_Bash;
        
        // Special files (Makefile, CMakeLists.txt, Dockerfile)
        QString fn = QFileInfo(filePath).fileName();
        if (fn == "Makefile" || fn == "CMakeLists.txt" || fn == "Dockerfile")
            return L_Bash;
        
        // Config / text files
        if (ext == "user" || ext == "conf" || ext == "cfg" || ext == "ini" ||
            ext == "env" || ext == "gitignore" || ext == "editorconfig" ||
            ext == "toml" || ext == "md" || ext == "txt")
            return L_Unknown;
        
        return L_Unknown;
    }

protected:
    void highlightBlock(const QString &text) override {
        for (const Rule &r : m_rules) {
            QRegularExpressionMatchIterator it = r.pattern.globalMatch(text);
            while (it.hasNext()) {
                QRegularExpressionMatch match = it.next();
                setFormat(match.capturedStart(), match.capturedLength(), r.format);
            }
        }

        if (m_commentStart.pattern().isEmpty())
            return;

        int startIndex = 0;
        if (previousBlockState() != 1)
            startIndex = text.indexOf(m_commentStart);
        else if (!text.isEmpty())
            startIndex = 0;

        while (startIndex >= 0) {
            int endIndex = text.indexOf(m_commentEnd, startIndex);
            int commentLength;
            if (endIndex == -1) {
                setCurrentBlockState(1);
                commentLength = text.length() - startIndex;
            } else {
                QRegularExpressionMatch endMatch = m_commentEnd.match(text, endIndex);
                commentLength = endIndex - startIndex + (endMatch.hasMatch() ? endMatch.capturedLength() : 2);
            }
            setFormat(startIndex, commentLength, m_multiLineCommentFormat);
            startIndex = text.indexOf(m_commentStart, startIndex + commentLength);
        }
    }

private:
    struct Rule {
        QRegularExpression pattern;
        QTextCharFormat format;
    };
    QList<Rule> m_rules;
    Language m_language;
    QRegularExpression m_commentStart;
    QRegularExpression m_commentEnd;
    QTextCharFormat m_multiLineCommentFormat;

    void setupRules() {
        QTextCharFormat kw = makeFormat(QColor("#c678dd"), true);   // purple
        QTextCharFormat type = makeFormat(QColor("#e5c07b"));       // yellow
        QTextCharFormat str = makeFormat(QColor("#98c379"));        // green
        QTextCharFormat num = makeFormat(QColor("#d19a66"));        // orange
        QTextCharFormat comment = makeFormat(QColor("#5c6370"), false, true); // gray italic
        QTextCharFormat pp = makeFormat(QColor("#61afef"));         // blue
        QTextCharFormat fn = makeFormat(QColor("#61afef"));         // function name blue
        QTextCharFormat prop = makeFormat(QColor("#e06c75"));       // property red (از نسخه قدیمی)

        switch (m_language) {
        case L_Cpp:
            addKeywords({"auto","break","case","const","continue","default","do","else",
                "enum","extern","for","goto","if","inline","register","return","signed",
                "sizeof","static","struct","switch","typedef","union","unsigned","volatile",
                "while","class","namespace","template","typename","virtual","override",
                "public","private","protected","friend","using","throw","try","catch",
                "new","delete","nullptr","true","false","int","float","double","char",
                "void","bool","long","short","constexpr","static_assert","decltype",
                "noexcept","nullptr_t","include","define","ifdef","ifndef","endif",
                "pragma","once","export","import","module"}, kw);
            addKeywords({"std","string","vector","map","set","list","pair","unique_ptr",
                "shared_ptr","weak_ptr","make_unique","make_shared","cout","cin","cerr",
                "endl","size_t","int8_t","int16_t","int32_t","int64_t","uint8_t","uint16_t",
                "uint32_t","uint64_t","QString","QWidget","QMainWindow","QObject",
                "QPushButton","QLabel","QVBoxLayout","QHBoxLayout","QFile","QDir",
                "QProcess","QTimer","QThread","QDebug","QStringList","QMap","QList",
                "QVector","QJsonDocument","QJsonObject","QJsonArray","QNetworkAccessManager"}, type);
            break;
        case L_Python:
            addKeywords({"def","class","return","if","elif","else","for","while","break",
                "continue","import","from","as","pass","raise","try","except","finally",
                "with","yield","lambda","self","None","True","False","and","or","not",
                "in","is","del","global","nonlocal","assert","async","await","print"}, kw);
            break;
        case L_Js:
            addKeywords({"function","var","let","const","class","return","if","else",
                "for","while","do","switch","case","break","continue","new","this",
                "super","import","export","from","default","async","await","yield",
                "try","catch","finally","throw","typeof","instanceof","of","in",
                "true","false","null","undefined","NaN","Infinity","console","require",
                "module","process","setTimeout","setInterval","Promise","then","catch"}, kw);
            addKeywords({"number","string","boolean","object","Array","Map","Set",
                "Error","Date","RegExp","Math","JSON","Symbol","Buffer"}, type);
            break;
        case L_Java:
            addKeywords({"public","private","protected","static","final","class","interface",
                "extends","implements","abstract","return","if","else","for","while","do",
                "switch","case","break","continue","new","this","super","import","package",
                "try","catch","finally","throw","throws","void","int","long","float",
                "double","boolean","char","byte","short","true","false","null","enum",
                "synchronized","volatile","transient","native","strictfp","instanceof"}, kw);
            addKeywords({"String","Integer","Long","Float","Double","Boolean","Byte",
                "Short","Character","Object","Class","System","List","ArrayList",
                "Map","HashMap","Set","HashSet","Collection","Iterator"}, type);
            break;
        case L_CSharp:
            addKeywords({"public","private","protected","internal","static","class","struct",
                "interface","enum","abstract","virtual","override","sealed","readonly",
                "const","return","if","else","for","foreach","while","do","switch",
                "case","break","continue","new","this","base","using","namespace",
                "try","catch","finally","throw","void","int","long","float","double",
                "bool","char","string","byte","short","var","true","false","null",
                "async","await","yield","get","set","value"}, kw);
            addKeywords({"String","Int32","Int64","Single","Double","Boolean","Byte",
                "Object","List","Dictionary","IEnumerable","IQueryable","Task",
                "ActionResult","IActionResult"}, type);
            break;
        case L_Rust:
            addKeywords({"fn","let","mut","const","static","return","if","else","for",
                "while","loop","match","break","continue","struct","enum","trait",
                "impl","type","pub","use","mod","crate","self","super","where",
                "as","in","ref","move","async","await","unsafe","extern","true",
                "false","Some","None","Ok","Err","String","Vec","Box","Option",
                "Result","HashMap","Arc","Mutex","Rc"}, kw);
            break;
        case L_Go:
            addKeywords({"func","return","if","else","for","range","switch","case",
                "break","continue","defer","go","select","chan","struct","interface",
                "type","map","var","const","package","import","true","false","nil",
                "make","new","close","len","cap","append","copy","delete","panic",
                "recover","string","int","int8","int16","int32","int64","float32",
                "float64","bool","byte","error"}, kw);
            break;
        case L_Sql:
            addKeywords({"SELECT","FROM","WHERE","INSERT","INTO","VALUES","UPDATE",
                "SET","DELETE","CREATE","TABLE","DROP","ALTER","INDEX","JOIN",
                "LEFT","RIGHT","INNER","OUTER","ON","AND","OR","NOT","IN","LIKE",
                "BETWEEN","IS","NULL","AS","ORDER","BY","GROUP","HAVING","LIMIT",
                "OFFSET","UNION","ALL","DISTINCT","COUNT","SUM","AVG","MIN","MAX",
                "EXISTS","CASE","WHEN","THEN","ELSE","END","BEGIN","COMMIT",
                "ROLLBACK","GRANT","REVOKE","PRIMARY","KEY","FOREIGN","REFERENCES",
                "CASCADE","CONSTRAINT","UNIQUE","CHECK","DEFAULT","AUTO_INCREMENT"}, kw);
            break;
        default:
            addKeywords({"if","else","for","while","do","switch","case","break",
                "continue","return","true","false","null","import","from","class",
                "function","def","var","let","const","new","this","try","catch",
                "finally","throw","async","await","in","of","type","interface",
                "enum","extends","implements"}, kw);
            break;
        }

        // Numbers
        {
            Rule r;
            r.pattern = QRegularExpression("\\b[0-9]+(\\.[0-9]+)?([eE][+-]?[0-9]+)?\\b");
            r.format = num;
            m_rules.append(r);
        }

        // Hex numbers
        {
            Rule r;
            r.pattern = QRegularExpression("\\b0[xX][0-9a-fA-F]+\\b");
            r.format = num;
            m_rules.append(r);
        }

        // Double-quoted strings
        {
            Rule r;
            r.pattern = QRegularExpression(R"("[^"\\]*(\\.[^"\\]*)*")");
            r.format = str;
            m_rules.append(r);
        }

        // Single-quoted strings
        {
            Rule r;
            r.pattern = QRegularExpression(R"('[^'\\]*(\\.[^'\\]*)*')");
            r.format = str;
            m_rules.append(r);
        }

        // Preprocessor directives (C/C++)
        if (m_language == L_Cpp) {
            Rule r;
            r.pattern = QRegularExpression("^\\s*#[a-zA-Z]+");
            r.format = pp;
            m_rules.append(r);
        }

        // Function calls
        {
            Rule r;
            r.pattern = QRegularExpression("\\b([a-zA-Z_][a-zA-Z0-9_]*)\\s*\\(");
            r.format = fn;
            m_rules.append(r);
        }

        // Single-line comments
        {
            Rule r;
            if (m_language == L_Python || m_language == L_Ruby || m_language == L_Bash ||
                m_language == L_Yaml || m_language == L_Unknown)
                r.pattern = QRegularExpression("#[^\n]*");
            else if (m_language == L_Html || m_language == L_Xml)
                r.pattern = QRegularExpression("<!--[^>]*-->");
            else
                r.pattern = QRegularExpression("//[^\n]*");
            r.format = comment;
            m_rules.append(r);
        }

        // Multi-line comments (C-style)
        if (m_language != L_Python && m_language != L_Html && m_language != L_Xml &&
            m_language != L_Bash && m_language != L_Yaml) {
            m_commentStart = QRegularExpression("/\\*");
            m_commentEnd = QRegularExpression("\\*/");
            m_multiLineCommentFormat = comment;
        }
    }
};

#endif