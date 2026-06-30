#ifndef CODEEDITOR_H
#define CODEEDITOR_H

#include <QPlainTextEdit>
#include <QTextEdit>
#include <QPainter>
#include <QTextBlock>
#include <QFocusEvent>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QShortcut>
#include <QTextCursor>
#include <QSet>
#include <QApplication>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QScrollBar>
#include <QSplitter>
#include <QFileInfo>
#include <QFileDialog>
#include <QDir>
#include "SyntaxHighlighter.h"

class CodeEditor;

class LineNumberArea : public QWidget {
    Q_OBJECT
public:
    explicit LineNumberArea(CodeEditor *editor);
    QSize sizeHint() const override;
protected:
    void paintEvent(QPaintEvent *event) override;
private:
    CodeEditor *codeEditor;
};

class ScrollIndicator : public QWidget {
    Q_OBJECT
public:
    explicit ScrollIndicator(CodeEditor *editor);
    void updateMatches(const QSet<int> &lines);
    void clearMatches();
protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
private:
    CodeEditor *ed;
    QSet<int> matchLines;
};

class CodeEditor : public QPlainTextEdit {
    Q_OBJECT
    signals:
    void focused(CodeEditor *editor);
    void matchCountChanged(int count);
public:
    explicit CodeEditor(QWidget *parent = nullptr) : QPlainTextEdit(parent), isDark(true) {
        lineNumberArea = new LineNumberArea(this);
        highlighter = new SyntaxHighlighter(document());
        highlighter->setLanguage(SyntaxHighlighter::L_Cpp);

        filePathLabel = new QLabel(this);
        filePathLabel->setText("No file open");
        filePathLabel->setFixedHeight(20);

        QFont f("Consolas", 13);
        f.setStyleHint(QFont::Monospace);
        setFont(f);
        setTabStopDistance(fontMetrics().horizontalAdvance(' ') * 4);
        setLineWrapMode(QPlainTextEdit::NoWrap);
        applyEditorStyle();

        connect(this, &CodeEditor::blockCountChanged, this, &CodeEditor::updateLineNumberAreaWidth);
        connect(this, &CodeEditor::updateRequest, this, &CodeEditor::updateLineNumberArea);
        connect(this, &CodeEditor::cursorPositionChanged, this, &CodeEditor::highlightCurrentLine);

        updateLineNumberAreaWidth(0);
        highlightCurrentLine();

        indicator = new ScrollIndicator(this);
        indicator->setObjectName("scrollIndicator");
        setupFindBar();

        connect(this, &CodeEditor::blockCountChanged, this, [this]() { indicator->update(); });

        connect(this, &CodeEditor::textChanged, this, [this]() {
            if (!m_modified) {
                m_modified = true;
                updateFilePathLabel();
                emit modifiedChanged(true);
            }
        });
    }

    void setTheme(bool dark) {
        isDark = dark;
        applyEditorStyle();
        applyFindBarStyle();
        applyFilePathLabelStyle();
        lineNumberArea->update();
        indicator->update();
        highlightCurrentLine();
    }

    int lineNumberAreaWidth() const {
        int d = 1, m = qMax(1, blockCount());
        while (m >= 10) { m /= 10; ++d; }
        return 12 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * d;
    }

    void setCode(const QString &c) {
        bool wasBlocked = blockSignals(true);
        setPlainText(c);
        blockSignals(wasBlocked);
        m_modified = false;
        updateFilePathLabel();
    }
    QString code() const { return toPlainText(); }

    void setFilePath(const QString &path) {
        m_filePath = path;
        if (path.isEmpty())
            filePathLabel->setText("No file open");
        else {
            QFileInfo fi(path);
            filePathLabel->setText(fi.fileName() + "  —  " + fi.absolutePath());
        }
    }

    QString filePath() const { return m_filePath; }

    int currentMatchCount() const { return matchCount->text().toInt(); }

    void setExternalMatchCount(int total) {
        int local = matchCount->text().toInt();
        if (local > 0 && total > local)
            matchCount->setText(QString("%1/%2").arg(local).arg(total));
        else
            matchCount->setText(QString("%1").arg(local));
    }

    void toggleFind() {
        bool vis = !findBar->isVisible();
        findBar->setVisible(vis);
        if (vis) {
            findInput->setFocus();
            findInput->selectAll();
        }
    }

    void toggleReplace() {
        bool vis = !replaceBar->isVisible();
        replaceBar->setVisible(vis);
        if (vis) {
            replaceInput->setFocus();
            replaceInput->selectAll();
        }
    }

    bool hasFind() const { return findBar->isVisible(); }
    bool hasReplace() const { return replaceBar->isVisible(); }

    bool saveFile() {
        if (m_filePath.isEmpty()) return saveAsFile();
        QFile f(m_filePath);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
        f.write(toPlainText().toUtf8());
        f.close();
        m_modified = false;
        updateFilePathLabel();
        emit modifiedChanged(false);
        return true;
    }

    bool saveAsFile() {
        QString path = QFileDialog::getSaveFileName(this, "Save File", m_filePath.isEmpty() ? QDir::homePath() : m_filePath,
                                                     "All Files (*)");
        if (path.isEmpty()) return false;
        m_filePath = path;
        bool ok = saveFile();
        if (ok) {
            QFileInfo fi(path);
            filePathLabel->setText(fi.fileName() + "  —  " + fi.absolutePath());
        }
        return ok;
    }

    bool isModified() const { return m_modified; }
    void markClean() { m_modified = false; updateFilePathLabel(); }

signals:
    void modifiedChanged(bool modified);

protected:
    void resizeEvent(QResizeEvent *e) override {
        QPlainTextEdit::resizeEvent(e);
        auto cr = contentsRect();
        int topLabelH = 22;
        lineNumberArea->setGeometry(QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
        filePathLabel->setGeometry(QRect(cr.left() + 8, cr.top() + 1, cr.width() - 32, topLabelH - 2));
        int fbw = qMin(400, cr.width());
        findBar->setGeometry(QRect(cr.right() - fbw - 8, cr.top() + topLabelH + 2, fbw, 28));
        replaceBar->setGeometry(QRect(cr.right() - fbw - 8, cr.top() + topLabelH + 32, fbw, 28));
        indicator->setGeometry(QRect(cr.right() - 16, cr.top(), 16, cr.height()));
    }

    void focusInEvent(QFocusEvent *e) override {
        QPlainTextEdit::focusInEvent(e);
        emit focused(this);
    }

private:
    void setupFindBar() {
        findBar = new QWidget(this);
        findBar->setVisible(false);
        findBar->setGeometry(0, 0, 400, 28);

        auto *fbLayout = new QHBoxLayout(findBar);
        fbLayout->setContentsMargins(4, 2, 4, 2);
        fbLayout->setSpacing(2);

        findInput = new QLineEdit();
        findInput->setPlaceholderText("Find...");
        findInput->setFixedHeight(22);

        matchCount = new QLabel();
        matchCount->setFixedWidth(50);
        matchCount->setAlignment(Qt::AlignCenter);

        prevBtn = new QPushButton("▲");
        prevBtn->setFixedSize(24, 24);
        prevBtn->setToolTip("Previous match");
        nextBtn = new QPushButton("▼");
        nextBtn->setFixedSize(24, 24);
        nextBtn->setToolTip("Next match");

        auto *replaceBtn = new QPushButton("R");
        replaceBtn->setFixedSize(24, 24);
        replaceBtn->setToolTip("Toggle replace");

        closeBtn = new QPushButton("✕");
        closeBtn->setFixedSize(24, 24);
        closeBtn->setToolTip("Close find");

        replaceBar = new QWidget(this);
        replaceBar->setVisible(false);
        replaceBar->setGeometry(0, 0, 400, 28);

        auto *rbLayout = new QHBoxLayout(replaceBar);
        rbLayout->setContentsMargins(4, 2, 4, 2);
        rbLayout->setSpacing(2);

        replaceInput = new QLineEdit();
        replaceInput->setPlaceholderText("Replace...");
        replaceInput->setFixedHeight(22);

        auto *repOneBtn = new QPushButton("Replace");
        repOneBtn->setFixedHeight(24);

        auto *repAllBtn = new QPushButton("All");
        repAllBtn->setFixedHeight(24);

        rbLayout->addWidget(replaceInput, 1);
        rbLayout->addWidget(repOneBtn);
        rbLayout->addWidget(repAllBtn);

        fbLayout->addWidget(findInput, 1);
        fbLayout->addWidget(matchCount);
        fbLayout->addWidget(prevBtn);
        fbLayout->addWidget(nextBtn);
        fbLayout->addWidget(replaceBtn);
        fbLayout->addWidget(closeBtn);

        applyFindBarStyle();

        connect(findInput, &QLineEdit::textChanged, this, [this]() { doFind(false); });
        connect(findInput, &QLineEdit::returnPressed, this, [this]() { doFind(false); });
        connect(nextBtn, &QPushButton::clicked, this, [this]() { doFind(false); });
        connect(prevBtn, &QPushButton::clicked, this, [this]() { doFind(true); });
        connect(replaceBtn, &QPushButton::clicked, this, [this]() { toggleReplace(); });
        connect(closeBtn, &QPushButton::clicked, this, [this]() { findBar->hide(); replaceBar->hide(); setFocus(); });
        connect(repOneBtn, &QPushButton::clicked, this, [this]() { doReplace(); });
        connect(repAllBtn, &QPushButton::clicked, this, [this]() { doReplaceAll(); });
        connect(replaceInput, &QLineEdit::returnPressed, this, [this]() { doReplace(); });

        auto *escShortcut = new QShortcut(QKeySequence("Escape"), this);
        connect(escShortcut, &QShortcut::activated, this, [this]() {
            if (findBar->isVisible()) { findBar->hide(); replaceBar->hide(); setFocus(); }
        });
    }

    void doReplace() {
        QString findText = findInput->text();
        QString replaceText = replaceInput->text();
        if (findText.isEmpty()) return;

        QTextCursor c = textCursor();
        if (c.hasSelection() && c.selectedText() == findText) {
            c.insertText(replaceText);
            find(findText);
        } else {
            if (!find(findText))
                moveCursor(QTextCursor::Start);
        }
        highlightAllMatches(findText);
    }

    void doReplaceAll() {
        QString findText = findInput->text();
        QString replaceText = replaceInput->text();
        if (findText.isEmpty()) return;

        moveCursor(QTextCursor::Start);
        int count = 0;
        while (find(findText)) {
            textCursor().insertText(replaceText);
            ++count;
        }
        matchCount->setText(QString("%1").arg(count));
        highlightAllMatches(findText);
    }

    void doFind(bool backward) {
        QString text = findInput->text();
        if (text.isEmpty()) {
            clearFindHighlights();
            matchCount->setText("");
            indicator->clearMatches();
            return;
        }

        QTextDocument::FindFlags flags;
        if (backward) flags |= QTextDocument::FindBackward;

        if (backward)
            find(text, flags);
        else {
            if (textCursor().hasSelection() &&
                textCursor().selectedText() == text)
                moveCursor(QTextCursor::NextWord);
            find(text, flags);
        }

        highlightAllMatches(text);
    }

    void highlightAllMatches(const QString &text) {
        QList<QTextEdit::ExtraSelection> es;

        QTextEdit::ExtraSelection curLine;
        if (!isReadOnly()) {
            curLine.format.setBackground(QColor(isDark ? "#262640" : "#e8e8e8"));
            curLine.format.setProperty(QTextFormat::FullWidthSelection, true);
            curLine.cursor = textCursor();
            curLine.cursor.clearSelection();
            es.append(curLine);
        }

        QSet<int> matchLines;
        int count = 0;
        if (!text.isEmpty()) {
            QTextCursor c(document());
            while (!c.isNull() && !c.atEnd()) {
                c = document()->find(text, c);
                if (!c.isNull()) {
                    matchLines.insert(c.blockNumber());
                    QTextEdit::ExtraSelection s;
                    s.format.setBackground(QColor("#6c63ff"));
                    s.format.setForeground(QColor("#ffffff"));
                    s.cursor = c;
                    es.append(s);
                    ++count;
                }
            }
        }
        setExtraSelections(es);
        matchCount->setText(QString("%1").arg(count));
        indicator->updateMatches(matchLines);
        emit matchCountChanged(count);
    }

    void clearFindHighlights() {
        highlightCurrentLine();
    }

    void applyFindBarStyle() {
        QString bg = isDark ? "#262640" : "#f3f3f3";
        QString fg = isDark ? "#d0d0e4" : "#1e1e1e";
        QString border = isDark ? "#383858" : "#d4d4d4";

        findBar->setStyleSheet(QString(
            "background-color: %1; border: 1px solid %2; border-radius: 3px;").arg(bg, border));
        replaceBar->setStyleSheet(QString(
            "background-color: %1; border: 1px solid %2; border-radius: 3px;").arg(bg, border));

        findInput->setStyleSheet(QString(
            "QLineEdit { background-color: %1; color: %2; border: 1px solid %3; "
            "border-radius: 3px; padding: 2px 6px; font-size: 12px; }"
        ).arg(isDark ? "#1a1a2e" : "#ffffff", fg, border));

        replaceInput->setStyleSheet(QString(
            "QLineEdit { background-color: %1; color: %2; border: 1px solid %3; "
            "border-radius: 3px; padding: 2px 6px; font-size: 12px; }"
        ).arg(isDark ? "#1a1a2e" : "#ffffff", fg, border));

        matchCount->setStyleSheet(QString("color: %1; font-size: 11px;").arg(
            isDark ? "#7a7a9e" : "#999999"));

        QString btnBg = isDark ? "#1a1a2e" : "#ffffff";
        QString btnHover = isDark ? "#3a3a5a" : "#e8e8e8";
        QString btnStyle = QString(
            "QPushButton { background-color: %1; color: %2; border: 1px solid %3; "
            "font-size: 14px; border-radius: 3px; padding: 0px; }"
            "QPushButton:hover { background-color: %4; }"
        ).arg(btnBg, fg, border, btnHover);

        prevBtn->setStyleSheet(btnStyle);
        nextBtn->setStyleSheet(btnStyle);
        closeBtn->setStyleSheet(btnStyle);

        QString actBtnStyle = QString(
            "QPushButton { background-color: %1; color: %2; border: 1px solid %3; "
            "font-size: 12px; border-radius: 3px; padding: 0px 6px; }"
            "QPushButton:hover { background-color: %4; }"
        ).arg(btnBg, fg, border, btnHover);
        for (auto *btn : replaceBar->findChildren<QPushButton *>())
            btn->setStyleSheet(actBtnStyle);
    }

    void applyEditorStyle() {
        setStyleSheet(isDark
            ? "QPlainTextEdit { background-color: #1a1a2e; color: #dcdcee; "
              "border: none; padding: 4px; selection-background-color: #6c63ff; }"
            : "QPlainTextEdit { background-color: #ffffff; color: #1e1e1e; "
              "border: none; padding: 4px; selection-background-color: #6c63ff; }");
        applyFilePathLabelStyle();
    }

    void applyFilePathLabelStyle() {
        filePathLabel->setStyleSheet(isDark
            ? "QLabel { background-color: #16162a; color: #8a8aaa; "
              "border: none; font-size: 11px; font-family: Consolas; padding: 0 4px; }"
            : "QLabel { background-color: #e8e8e8; color: #666666; "
              "border: none; font-size: 11px; font-family: Consolas; padding: 0 4px; }");
    }

private slots:
    void updateLineNumberAreaWidth(int) {
        setViewportMargins(lineNumberAreaWidth(), 22, 16, 0);
    }

    void updateLineNumberArea(const QRect &r, int dy) {
        if (dy) lineNumberArea->scroll(0, dy);
        else    lineNumberArea->update(0, r.y(), lineNumberArea->width(), r.height());
        if (r.contains(viewport()->rect())) updateLineNumberAreaWidth(0);
    }

    void highlightCurrentLine() {
        QList<QTextEdit::ExtraSelection> es;
        if (!isReadOnly()) {
            QTextEdit::ExtraSelection s;
            s.format.setBackground(QColor(isDark ? "#262640" : "#e8e8e8"));
            s.format.setProperty(QTextFormat::FullWidthSelection, true);
            s.cursor = textCursor();
            s.cursor.clearSelection();
            es.append(s);
        }
        setExtraSelections(es);
    }

    void paintLineNumbers(QPaintEvent *e) {
        QPainter p(lineNumberArea);
        p.fillRect(e->rect(), QColor(isDark ? "#1a1a2e" : "#f3f3f3"));
        auto block = firstVisibleBlock();
        int num = block.blockNumber();
        int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
        int bot = top + qRound(blockBoundingRect(block).height());
        while (block.isValid() && top <= e->rect().bottom()) {
            if (block.isVisible() && bot >= e->rect().top()) {
                p.setPen(QColor(isDark ? "#7a7a9e" : "#999999"));
                p.drawText(0, top, lineNumberArea->width() - 8, fontMetrics().height(),
                           Qt::AlignRight, QString::number(num + 1));
            }
            block = block.next();
            top = bot;
            bot = top + qRound(blockBoundingRect(block).height());
            ++num;
        }
    }

private:
    LineNumberArea *lineNumberArea;
    SyntaxHighlighter *highlighter;
    bool isDark;
    QString m_filePath;
    bool m_modified = false;
    QLabel *filePathLabel;
    QWidget *findBar = nullptr;
    QLineEdit *findInput = nullptr;
    QLabel *matchCount = nullptr;
    QPushButton *prevBtn = nullptr;
    QPushButton *nextBtn = nullptr;
    QPushButton *closeBtn = nullptr;
    QWidget *replaceBar = nullptr;
    QLineEdit *replaceInput = nullptr;
    ScrollIndicator *indicator = nullptr;

    void updateFilePathLabel() {
        if (m_filePath.isEmpty()) {
            filePathLabel->setText("No file open");
        } else {
            QFileInfo fi(m_filePath);
            QString suffix = m_modified ? "  ●" : "";
            filePathLabel->setText(fi.fileName() + suffix + "  —  " + fi.absolutePath());
        }
    }

    friend class LineNumberArea;
    friend class ScrollIndicator;
};

inline LineNumberArea::LineNumberArea(CodeEditor *e)
    : QWidget(e), codeEditor(e) {}

inline QSize LineNumberArea::sizeHint() const {
    return QSize(codeEditor->lineNumberAreaWidth(), 0);
}

inline void LineNumberArea::paintEvent(QPaintEvent *e) {
    codeEditor->paintLineNumbers(e);
}

inline ScrollIndicator::ScrollIndicator(CodeEditor *editor)
    : QWidget(editor), ed(editor)
{
    setFixedWidth(24);
}

inline void ScrollIndicator::updateMatches(const QSet<int> &lines) {
    matchLines = lines;
    update();
}

inline void ScrollIndicator::clearMatches() {
    matchLines.clear();
    update();
}

inline void ScrollIndicator::paintEvent(QPaintEvent *) {
    QPainter p(this);
    if (!ed) return;
    int total = ed->blockCount();
    if (total == 0) return;

    int w = width();
    int h = rect().height();

    p.fillRect(rect(), QColor(ed->isDark ? "#1e1e38" : "#e8e8e8"));
    p.setPen(QColor(ed->isDark ? "#3a3a5a" : "#d0d0d0"));
    p.drawLine(0, 0, 0, h);

    if (!matchLines.isEmpty()) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#6c63ff"));
        for (int line : matchLines) {
            int y = line * h / total;
            int markerH = qMax(8, h / qMin(total, 500));
            p.drawRect(3, y, w - 6, markerH);
        }
    }
}

inline void ScrollIndicator::mousePressEvent(QMouseEvent *event) {
    int total = ed->blockCount();
    if (total == 0) return;
    int line = event->pos().y() * total / rect().height();
    line = qBound(0, line, total - 1);
    auto cursor = ed->textCursor();
    cursor.movePosition(QTextCursor::Start);
    cursor.movePosition(QTextCursor::Down, QTextCursor::MoveAnchor, line);
    ed->setTextCursor(cursor);
    ed->ensureCursorVisible();
    ed->setFocus();
}

inline void ScrollIndicator::wheelEvent(QWheelEvent *event) {
    QApplication::sendEvent(ed->verticalScrollBar(), event);
}

// ===== ChatEditor با قابلیت‌های کامل از نسخه قدیمی =====

class ChatEditor;

class ChatScrollIndicator : public QWidget {
    Q_OBJECT
public:
    explicit ChatScrollIndicator(ChatEditor *editor);
    void updateMatches(const QSet<int> &lines);
    void clearMatches();
protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
private:
    ChatEditor *ed;
    QSet<int> matchLines;
};

class ChatEditor : public QTextEdit {
    Q_OBJECT
public:
    explicit ChatEditor(QWidget *parent = nullptr);
    void setTheme(bool dark);
    void toggleFind();
    void toggleReplace();
    bool hasFind() const { return findBar->isVisible(); }
    bool hasReplace() const { return replaceBar->isVisible(); }
protected:
    void resizeEvent(QResizeEvent *e) override;
private:
    void setupFindBar();
    void doFind(bool backward);
    void doReplace();
    void doReplaceAll();
    void highlightAllMatches(const QString &text);
    void clearFindHighlights();
    void applyFindBarStyle();
    void applyEditorStyle();
    bool isDark;
    QWidget *findBar = nullptr;
    QLineEdit *findInput = nullptr;
    QLabel *matchCount = nullptr;
    QPushButton *prevBtn = nullptr;
    QPushButton *nextBtn = nullptr;
    QPushButton *closeBtn = nullptr;
    QWidget *replaceBar = nullptr;
    QLineEdit *replaceInput = nullptr;
    ChatScrollIndicator *indicator = nullptr;
    friend class ChatScrollIndicator;
};

inline ChatScrollIndicator::ChatScrollIndicator(ChatEditor *editor)
    : QWidget(editor), ed(editor)
{
    setFixedWidth(16);
}

inline void ChatScrollIndicator::updateMatches(const QSet<int> &lines) {
    matchLines = lines;
    update();
}

inline void ChatScrollIndicator::clearMatches() {
    matchLines.clear();
    update();
}

inline void ChatScrollIndicator::paintEvent(QPaintEvent *) {
    QPainter p(this);
    if (!ed) return;
    int total = ed->document()->blockCount();
    if (total == 0) return;
    int w = width(), h = rect().height();
    p.fillRect(rect(), QColor(ed->isDark ? "#1e1e38" : "#e8e8e8"));
    p.setPen(QColor(ed->isDark ? "#3a3a5a" : "#d0d0d0"));
    p.drawLine(0, 0, 0, h);
    if (!matchLines.isEmpty()) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#6c63ff"));
        for (int line : matchLines) {
            int y = line * h / total;
            int markerH = qMax(5, h / qMin(total, 500));
            p.drawRect(2, y, w - 4, markerH);
        }
    }
}

inline void ChatScrollIndicator::mousePressEvent(QMouseEvent *event) {
    int total = ed->document()->blockCount();
    if (total == 0) return;
    int line = event->pos().y() * total / rect().height();
    line = qBound(0, line, total - 1);
    auto cursor = ed->textCursor();
    cursor.movePosition(QTextCursor::Start);
    cursor.movePosition(QTextCursor::Down, QTextCursor::MoveAnchor, line);
    ed->setTextCursor(cursor);
    ed->ensureCursorVisible();
    ed->setFocus();
}

inline void ChatScrollIndicator::wheelEvent(QWheelEvent *event) {
    QApplication::sendEvent(ed->verticalScrollBar(), event);
}

inline ChatEditor::ChatEditor(QWidget *parent)
    : QTextEdit(parent), isDark(true)
{
    setReadOnly(true);
    QFont f("Consolas", 13);
    f.setStyleHint(QFont::Monospace);
    setFont(f);
    applyEditorStyle();
    indicator = new ChatScrollIndicator(this);
    setupFindBar();
    connect(this, &ChatEditor::textChanged, this, [this]() { indicator->update(); });
}

inline void ChatEditor::setTheme(bool dark) {
    isDark = dark;
    applyEditorStyle();
    applyFindBarStyle();
    indicator->update();
}

inline void ChatEditor::toggleFind() {
    bool vis = !findBar->isVisible();
    findBar->setVisible(vis);
    if (vis) { findInput->setFocus(); findInput->selectAll(); }
}

inline void ChatEditor::toggleReplace() {
    bool vis = !replaceBar->isVisible();
    replaceBar->setVisible(vis);
    if (vis) { replaceInput->setFocus(); replaceInput->selectAll(); }
}

inline void ChatEditor::resizeEvent(QResizeEvent *e) {
    QTextEdit::resizeEvent(e);
    auto cr = contentsRect();
    int fbw = qMin(400, cr.width());
    findBar->setGeometry(QRect(cr.right() - fbw - 8, cr.top() + 4, fbw, 28));
    replaceBar->setGeometry(QRect(cr.right() - fbw - 8, cr.top() + 34, fbw, 28));
    indicator->setGeometry(QRect(cr.right() - 16, cr.top(), 16, cr.height()));
}

inline void ChatEditor::setupFindBar() {
    findBar = new QWidget(this);
    findBar->setVisible(false);
    findBar->setGeometry(0, 0, 400, 28);
    auto *fbLayout = new QHBoxLayout(findBar);
    fbLayout->setContentsMargins(4, 2, 4, 2);
    fbLayout->setSpacing(2);

    findInput = new QLineEdit();
    findInput->setPlaceholderText("Find...");
    findInput->setFixedHeight(22);
    matchCount = new QLabel();
    matchCount->setFixedWidth(50);
    matchCount->setAlignment(Qt::AlignCenter);
    prevBtn = new QPushButton("▲");
    prevBtn->setFixedSize(24, 24);
    prevBtn->setToolTip("Previous match");
    nextBtn = new QPushButton("▼");
    nextBtn->setFixedSize(24, 24);
    nextBtn->setToolTip("Next match");
    auto *replaceBtn = new QPushButton("R");
    replaceBtn->setFixedSize(24, 24);
    replaceBtn->setToolTip("Toggle replace");
    closeBtn = new QPushButton("✕");
    closeBtn->setFixedSize(24, 24);
    closeBtn->setToolTip("Close find");

    replaceBar = new QWidget(this);
    replaceBar->setVisible(false);
    replaceBar->setGeometry(0, 0, 400, 28);
    auto *rbLayout = new QHBoxLayout(replaceBar);
    rbLayout->setContentsMargins(4, 2, 4, 2);
    rbLayout->setSpacing(2);
    replaceInput = new QLineEdit();
    replaceInput->setPlaceholderText("Replace...");
    replaceInput->setFixedHeight(22);
    auto *repOneBtn = new QPushButton("Replace");
    repOneBtn->setFixedHeight(24);
    auto *repAllBtn = new QPushButton("All");
    repAllBtn->setFixedHeight(24);
    rbLayout->addWidget(replaceInput, 1);
    rbLayout->addWidget(repOneBtn);
    rbLayout->addWidget(repAllBtn);

    fbLayout->addWidget(findInput, 1);
    fbLayout->addWidget(matchCount);
    fbLayout->addWidget(prevBtn);
    fbLayout->addWidget(nextBtn);
    fbLayout->addWidget(replaceBtn);
    fbLayout->addWidget(closeBtn);

    applyFindBarStyle();

    connect(findInput, &QLineEdit::textChanged, this, [this]() { doFind(false); });
    connect(findInput, &QLineEdit::returnPressed, this, [this]() { doFind(false); });
    connect(nextBtn, &QPushButton::clicked, this, [this]() { doFind(false); });
    connect(prevBtn, &QPushButton::clicked, this, [this]() { doFind(true); });
    connect(replaceBtn, &QPushButton::clicked, this, [this]() { toggleReplace(); });
    connect(closeBtn, &QPushButton::clicked, this, [this]() { findBar->hide(); replaceBar->hide(); setFocus(); });
    connect(repOneBtn, &QPushButton::clicked, this, [this]() { doReplace(); });
    connect(repAllBtn, &QPushButton::clicked, this, [this]() { doReplaceAll(); });
    connect(replaceInput, &QLineEdit::returnPressed, this, [this]() { doReplace(); });

    auto *escShortcut = new QShortcut(QKeySequence("Escape"), this);
    connect(escShortcut, &QShortcut::activated, this, [this]() {
        if (findBar->isVisible()) { findBar->hide(); replaceBar->hide(); setFocus(); }
    });
}

inline void ChatEditor::doFind(bool backward) {
    QString text = findInput->text();
    if (text.isEmpty()) {
        clearFindHighlights();
        matchCount->setText("");
        indicator->clearMatches();
        return;
    }
    QTextDocument::FindFlags flags;
    if (backward) flags |= QTextDocument::FindBackward;
    if (backward)
        find(text, flags);
    else {
        if (textCursor().hasSelection() && textCursor().selectedText() == text)
            moveCursor(QTextCursor::NextWord);
        find(text, flags);
    }
    highlightAllMatches(text);
}

inline void ChatEditor::doReplace() {
    QString findText = findInput->text();
    QString replaceText = replaceInput->text();
    if (findText.isEmpty()) return;
    QTextCursor c = textCursor();
    if (c.hasSelection() && c.selectedText() == findText) {
        c.insertText(replaceText);
        find(findText);
    } else {
        if (!find(findText))
            moveCursor(QTextCursor::Start);
    }
    highlightAllMatches(findText);
}

inline void ChatEditor::doReplaceAll() {
    QString findText = findInput->text();
    QString replaceText = replaceInput->text();
    if (findText.isEmpty()) return;
    moveCursor(QTextCursor::Start);
    int count = 0;
    while (find(findText)) {
        textCursor().insertText(replaceText);
        ++count;
    }
    matchCount->setText(QString("%1").arg(count));
    highlightAllMatches(findText);
}

inline void ChatEditor::highlightAllMatches(const QString &text) {
    QList<QTextEdit::ExtraSelection> es;
    QSet<int> matchLines;
    int count = 0;
    if (!text.isEmpty()) {
        QTextCursor c(document());
        while (!c.isNull() && !c.atEnd()) {
            c = document()->find(text, c);
            if (!c.isNull()) {
                matchLines.insert(c.blockNumber());
                QTextEdit::ExtraSelection s;
                s.format.setBackground(QColor("#6c63ff"));
                s.format.setForeground(QColor("#ffffff"));
                s.cursor = c;
                es.append(s);
                ++count;
            }
        }
    }
    setExtraSelections(es);
    matchCount->setText(QString("%1").arg(count));
    indicator->updateMatches(matchLines);
}

inline void ChatEditor::clearFindHighlights() {
    QList<QTextEdit::ExtraSelection> es;
    setExtraSelections(es);
}

inline void ChatEditor::applyFindBarStyle() {
    QString bg = isDark ? "#262640" : "#f3f3f3";
    QString fg = isDark ? "#d0d0e4" : "#1e1e1e";
    QString border = isDark ? "#383858" : "#d4d4d4";
    findBar->setStyleSheet(QString(
        "background-color: %1; border: 1px solid %2; border-radius: 3px;").arg(bg, border));
    replaceBar->setStyleSheet(QString(
        "background-color: %1; border: 1px solid %2; border-radius: 3px;").arg(bg, border));
    findInput->setStyleSheet(QString(
        "QLineEdit { background-color: %1; color: %2; border: 1px solid %3; "
        "border-radius: 3px; padding: 2px 6px; font-size: 12px; }"
    ).arg(isDark ? "#1a1a2e" : "#ffffff", fg, border));
    replaceInput->setStyleSheet(QString(
        "QLineEdit { background-color: %1; color: %2; border: 1px solid %3; "
        "border-radius: 3px; padding: 2px 6px; font-size: 12px; }"
    ).arg(isDark ? "#1a1a2e" : "#ffffff", fg, border));
    matchCount->setStyleSheet(QString("color: %1; font-size: 11px;").arg(
        isDark ? "#7a7a9e" : "#999999"));
    QString btnBg = isDark ? "#1a1a2e" : "#ffffff";
    QString btnHover = isDark ? "#3a3a5a" : "#e8e8e8";
    QString btnStyle = QString(
        "QPushButton { background-color: %1; color: %2; border: 1px solid %3; "
        "font-size: 14px; border-radius: 3px; padding: 0px; }"
        "QPushButton:hover { background-color: %4; }"
    ).arg(btnBg, fg, border, btnHover);
    prevBtn->setStyleSheet(btnStyle);
    nextBtn->setStyleSheet(btnStyle);
    closeBtn->setStyleSheet(btnStyle);
    QString actBtnStyle = QString(
        "QPushButton { background-color: %1; color: %2; border: 1px solid %3; "
        "font-size: 12px; border-radius: 3px; padding: 0px 6px; }"
        "QPushButton:hover { background-color: %4; }"
    ).arg(btnBg, fg, border, btnHover);
    for (auto *btn : replaceBar->findChildren<QPushButton *>())
        btn->setStyleSheet(actBtnStyle);
}

inline void ChatEditor::applyEditorStyle() {
    setStyleSheet(isDark
        ? "QTextEdit { background-color: #1a1a2e; color: #dcdcee; "
          "border: none; selection-background-color: #6c63ff; }"
        : "QTextEdit { background-color: #ffffff; color: #1e1e1e; "
          "border: none; selection-background-color: #6c63ff; }");
}

#endif