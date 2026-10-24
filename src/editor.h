#pragma once
#include <QtWidgets>
#include <QtPrintSupport>

namespace Mojian {
class LiveController;
class Services;
class Editor;
QString decodeText(const QByteArray &bytes, bool *ok = nullptr);
QString tableMarkdown(const QList<QStringList> &rows, const QStringList &align);
QString escapedCell(QString text);

class Highlighter : public QSyntaxHighlighter {
public:
    explicit Highlighter(QTextDocument *document);
    bool live = true;
    int activeBlock = 0;
    Editor *owner = nullptr;
    void projectionFormat();
protected:
    void highlightBlock(const QString &text) override;
};

class Editor : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit Editor(QWidget *parent = nullptr);
    QString sourceCache,referenceDefinitions;
    QString pendingPrefix,pendingSuffix,outsideBefore,outsideAfter;
    int outsidePosition=-1;
    void insertFormattedText(const QString &text);
    QString toPlainText() const { return sourceCache; }
    void updateGutter();
    QRectF blockRect(const QTextBlock &block) const { return blockBoundingGeometry(block).translated(contentOffset()); }
    Highlighter *highlighter;
    LiveController *liveController = nullptr;
    bool composing = false;
    QTextCursor searchScope;
    QString scopeSource;
    QWidget *gutter = nullptr;
    int gutterWidth() const;
    void paintGutter(QPaintEvent *event);
    void setLive(bool live);
    void wrap(const QString &before, const QString &after = {});
    void prefix(const QString &prefix);
    void heading(int level);
    void unindent();
    void insertRule();
    bool findText(const QString &text, bool backwards, bool sensitive, bool regex, bool whole);
    int replaceAll(const QString &needle, const QString &replacement, bool sensitive, bool regex, bool whole);
    static QRegularExpression expression(const QString &text, bool sensitive, bool regex, bool whole);
signals:
    void sourceChanged();
protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    bool event(QEvent *event) override;
    void insertFromMimeData(const QMimeData *source) override;
};

class Window : public QMainWindow {
    Q_OBJECT
public:
    explicit Window(const QString &path = {}, QWidget *parent = nullptr);
    Editor *editor;
    QTextBrowser *preview;
    QString filePath;
    Services *services = nullptr;
    QFileSystemWatcher watcher;
    QByteArray diskHash;
    QTimer recoveryTimer;
    QString recoveryKey;
    bool focusMode = false, typewriterMode = false;
    QString lineEnding = "\n";
    bool loadPath(const QString &path);
    bool savePath(const QString &path);
    void setMode(const QString &mode);
    QString mode() const { return currentMode; }
    void refreshPreview();
    void insertTable(const QList<QStringList> &rows, const QStringList &align);
    void showHelp();
    Editor *focusedEditor() const;
    static Window *openDocument(const QString &path);
    static QList<QPointer<Window>> windows;
protected:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *object,QEvent *event) override;
private:
    QSplitter *splitter;
    QDockWidget *outlineDock;
    QTreeWidget *outline;
    QDockWidget *folderDock;
    QTreeView *files;
    QFileSystemModel *fileModel;
    QWidget *findBar;
    QLineEdit *needle, *replacement;
    QCheckBox *caseCheck, *regexCheck, *wholeCheck, *selectionCheck;
    QLabel *findStatus, *position;
    QTimer *renderTimer;
    QTimer *outlineTimer;
    QPointer<QDialog> helpDialog;
    QString currentMode = "live";
    bool syncingScroll = false;
    QActionGroup *modeActions = nullptr;
    void menus();
    void updateTitle();
    void updateOutline();
    bool maybeSave();
    bool save(bool as = false);
    void open();
    void find(bool backwards = false);
    void replaceOne();
    void tableDialog();
    void preferences();
    void exportFile(const QString &format);
    void chooseFolder();
    void image();
};
}
