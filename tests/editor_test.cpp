#include "editor.h"
#include <QtTest>
#include <QInputMethodEvent>
#include <QTemporaryDir>
#include <QSignalSpy>
using namespace Mojian;
class EditorTests : public QObject {
    Q_OBJECT
private slots:
    void utf8() { bool ok; QCOMPARE(decodeText(QString::fromUtf8("中文😀\r\n").toUtf8(), &ok), QString::fromUtf8("中文😀\n")); QVERIFY(ok); }
    void bom() { bool ok; QCOMPARE(decodeText(QByteArray::fromHex("efbbbf") + "hello", &ok), QString("hello")); QVERIFY(ok); }
    void utf16() { bool ok; QCOMPARE(decodeText(QByteArray::fromHex("fffe2d4e8765"), &ok), QString::fromUtf8("中文")); QVERIFY(ok); }
    void invalidEncoding() { bool ok; decodeText(QByteArray::fromHex("ff81"), &ok); QVERIFY(!ok); }
    void typing() {
        Editor editor; editor.show(); editor.setFocus(); QTest::keyClicks(&editor, "Hello ");
        QInputMethodEvent composition(QString::fromUtf8("zhong"), {}); QApplication::sendEvent(&editor, &composition);
        QCOMPARE(editor.toPlainText(), QString("Hello "));
        QInputMethodEvent commit; commit.setCommitString(QString::fromUtf8("中文😀")); QApplication::sendEvent(&editor, &commit);
        QCOMPARE(editor.toPlainText(), QString::fromUtf8("Hello 中文😀"));
        editor.undo(); QCOMPARE(editor.toPlainText(), QString("Hello ")); editor.redo(); QCOMPARE(editor.toPlainText(), QString::fromUtf8("Hello 中文😀"));
    }
    void wrapUndo() { Editor editor; editor.setPlainText("text"); editor.selectAll(); editor.wrap("**"); QCOMPARE(editor.toPlainText(), QString("**text**")); editor.undo(); QCOMPARE(editor.toPlainText(), QString("text")); editor.redo(); QCOMPARE(editor.toPlainText(), QString("**text**")); }
    void emptyWrapCaret() { Editor editor; editor.setLive(false); editor.wrap("*"); QCOMPARE(editor.toPlainText(), QString("**")); QCOMPARE(editor.textCursor().position(), 1); }
    void multilinePrefixUndo() { Editor editor; editor.setPlainText("one\ntwo"); editor.selectAll(); editor.prefix("- "); QCOMPARE(editor.toPlainText(), QString("- one\n- two")); editor.undo(); QCOMPARE(editor.toPlainText(), QString("one\ntwo")); }
    void formattingPreservesSource() { Editor editor; QString source = "# Title\n\n**bold** *italic*\n\n---\n"; editor.setPlainText(source); editor.setLive(true); editor.highlighter->rehighlight(); QCOMPARE(editor.toPlainText(), source); QVERIFY(!editor.document()->isModified()); }
    void listContinuation() { Editor editor; editor.setPlainText("- first"); editor.moveCursor(QTextCursor::End); QTest::keyClick(&editor, Qt::Key_Return); QCOMPARE(editor.toPlainText(), QString("- first\n- ")); QTest::keyClick(&editor, Qt::Key_Return); QCOMPARE(editor.toPlainText(), QString("- first\n")); }
    void numberedContinuation() { Editor editor; editor.setPlainText("9. first"); editor.moveCursor(QTextCursor::End); QTest::keyClick(&editor, Qt::Key_Return); QCOMPARE(editor.toPlainText(), QString("9. first\n10. ")); }
    void codeNoListContinuation() { Editor editor; editor.setPlainText("```\n- code"); editor.highlighter->rehighlight(); editor.moveCursor(QTextCursor::End); QTest::keyClick(&editor, Qt::Key_Return); QCOMPARE(editor.toPlainText(), QString("```\n- code\n")); }
    void literalReplacementUndo() { Editor editor; editor.setPlainText(QString::fromUtf8("苹果 apple 苹果😀")); QCOMPARE(editor.replaceAll(QString::fromUtf8("苹果"), "pear", true, false, false), 2); QCOMPARE(editor.toPlainText(), QString::fromUtf8("pear apple pear😀")); editor.undo(); QCOMPARE(editor.toPlainText(), QString::fromUtf8("苹果 apple 苹果😀")); }
    void regexReplacement() { Editor editor; editor.setPlainText("a12 a34"); QCOMPARE(editor.replaceAll("a(\\d+)", "$1-$$", true, true, false), 2); QCOMPARE(editor.toPlainText(), QString("12-$ 34-$")); editor.undo(); QCOMPARE(editor.toPlainText(), QString("a12 a34")); }
    void invalidRegexNoChange() { Editor editor; editor.setPlainText("original"); QCOMPARE(editor.replaceAll("[", "x", true, true, false), -1); QCOMPARE(editor.toPlainText(), QString("original")); }
    void zeroWidthRegex() { Editor editor; editor.setPlainText("abc"); QCOMPARE(editor.replaceAll("(?=.)", "-", true, true, false), 3); QCOMPARE(editor.toPlainText(), QString("-a-b-c")); editor.undo(); QCOMPARE(editor.toPlainText(), QString("abc")); }
    void emptySearch() { Editor editor; editor.setPlainText("safe"); QCOMPARE(editor.replaceAll("", "x", false, false, false), 0); QCOMPARE(editor.toPlainText(), QString("safe")); }
    void wholeWords() { Editor editor; editor.setPlainText("cat catalog cat"); QCOMPARE(editor.replaceAll("cat", "dog", true, false, true), 2); QCOMPARE(editor.toPlainText(), QString("dog catalog dog")); }
    void findWrap() { Editor editor; editor.setPlainText("abc def abc"); editor.moveCursor(QTextCursor::End); QVERIFY(editor.findText("abc", false, true, false, false)); QCOMPARE(editor.textCursor().selectionStart(), 0); QVERIFY(editor.findText("abc", true, true, false, false)); QCOMPARE(editor.textCursor().selectionStart(), 8); }
    void atomicSaveReload() {
        QTemporaryDir dir; Window window; QString source = QString::fromUtf8("# 中文😀\n\n*斜体*\n\n---\n"); window.editor->setPlainText(source); auto path = dir.filePath("note.md");
        QVERIFY(window.savePath(path)); QVERIFY(!window.editor->document()->isModified()); QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), source.toUtf8());
        Window reload; QVERIFY(reload.loadPath(path)); QCOMPARE(reload.editor->toPlainText(), source);
    }
    void crlfPreserved() {
        QTemporaryDir dir; auto path = dir.filePath("crlf.md"); QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("a\r\nb\r\n"); file.close();
        Window window; QVERIFY(window.loadPath(path)); QCOMPARE(window.editor->toPlainText(), QString("a\nb\n")); window.editor->moveCursor(QTextCursor::End); window.editor->insertPlainText("c\n"); QVERIFY(window.savePath(path)); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), QByteArray("a\r\nb\r\nc\r\n"));
    }
    void modesPreserveSource() { Window window; QString source = "# Title\n\n---\n\n|A|B|\n|-|-|\n|1|2|\n"; window.editor->setPlainText(source); for (auto mode : {"live", "source", "split", "reading"}) { window.setMode(mode); QCOMPARE(window.editor->toPlainText(), source); QCOMPARE(window.mode(), QString(mode)); } }
    void preShowSplitPreview() { Window window; window.editor->setPlainText("# Preview content"); window.setMode("split"); QVERIFY(window.preview->toPlainText().contains("Preview content")); }
    void thematicBreak() { Window window; window.editor->setPlainText("before\n\n---\n\nafter"); window.refreshPreview(); QVERIFY(window.preview->document()->toHtml().contains("<hr")); QVERIFY(!window.preview->toPlainText().contains("---")); }
    void setextHeading() { Window window; window.editor->setPlainText("Title\n---\n"); window.refreshPreview(); QVERIFY(window.preview->document()->toHtml().contains("font-weight:700")); }
    void gfmTable() { QSettings().setValue("dialect", "gfm"); Window window; window.editor->setPlainText("| A | B |\n| --- | --- |\n| **bold** | *italic* |\n"); window.refreshPreview(); QVERIFY(window.preview->document()->toHtml().contains("<table")); QVERIFY(window.preview->toPlainText().contains("bold")); QVERIFY(!window.preview->toPlainText().contains("**")); }
    void tableSourceUndo() { Window window; window.editor->setPlainText("before\n"); window.editor->moveCursor(QTextCursor::End); window.insertTable({{"A", "B"}, {"a|b", "**bold**"}}, {"---", "---:"}); QVERIFY(window.editor->toPlainText().contains("a\\|b")); window.editor->undo(); QCOMPARE(window.editor->toPlainText(), QString("before\n")); window.editor->redo(); QVERIFY(window.editor->toPlainText().contains("---:")); }
    void tableDialogRoundTrip() {
        Window window; const QString source = "| A | B |\n| --- | --- |\n| a\\|b | **bold** |\n\nafter\n";
        window.editor->setPlainText(source); auto cursor = window.editor->textCursor(); cursor.setPosition(4); window.editor->setTextCursor(cursor);
        bool visited = false;
        QTimer::singleShot(0, [&] {
            auto dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()); QVERIFY(dialog);
            auto table = dialog->findChild<QTableWidget *>(); QVERIFY(table); QCOMPARE(table->rowCount(), 2); QCOMPARE(table->item(1, 0)->text(), QString("a|b"));
            table->item(1, 1)->setText("changed"); visited = true; dialog->accept();
        });
        auto menu = window.menuBar()->actions()[3]->menu();
        for (auto action : menu->actions()) if (action->text().contains(QString::fromUtf8("表格"))) { action->trigger(); break; }
        QVERIFY(visited); QVERIFY(window.editor->toPlainText().contains("changed |\n\nafter")); window.editor->undo(); QCOMPARE(window.editor->toPlainText(), source);
    }
    void tableEscaping() { QCOMPARE(escapedCell("a|b\\c\nd"), QString("a\\|b\\c<br>d")); }
    void noHtmlExecution() { Window window; window.editor->setPlainText("<script>alert('x')</script>\n"); window.refreshPreview(); QVERIFY(window.preview->document()->toHtml().contains("&lt;script&gt;")); }
    void newMenuExactlyOne() {
        Window window; window.show();
        auto liveCount = [] { int n = 0; for (auto w : Window::windows) if (w) ++n; return n; };
        int before = liveCount(); auto action = window.menuBar()->actions().first()->menu()->actions().first();
        action->trigger(); QCoreApplication::processEvents(); QCOMPARE(liveCount(), before + 1);
        action->trigger(); QCoreApplication::processEvents(); QCOMPARE(liveCount(), before + 2);
        for (auto pointer : Window::windows) if (pointer && pointer != &window) { pointer->editor->document()->setModified(false); delete pointer; }
    }
    void bundledHelp() { QFile file(":/mojian/help.md"); QVERIFY(file.open(QIODevice::ReadOnly)); auto source = file.readAll(); QVERIFY(source.contains("Linux")); QVERIFY(QString::fromUtf8(source).contains("22. 快捷键")); Window window; window.showHelp(); QCoreApplication::processEvents(); auto dialog = window.findChild<QDialog *>(); QVERIFY(dialog); auto list = dialog->findChild<QListWidget *>(); QVERIFY(list && list->count() >= 22); auto search = dialog->findChild<QLineEdit *>(); search->setText(QString::fromUtf8("原子写入")); QVERIFY(list->count() > 0 && list->count() < 22); }
    void pdfOutput() { QTemporaryDir dir; Window window; window.editor->setPlainText(QString::fromUtf8("# 中文导出\n\n*中文斜体*\n\n---\n")); window.refreshPreview(); QPrinter printer; printer.setOutputFormat(QPrinter::PdfFormat); auto path = dir.filePath("test.pdf"); printer.setOutputFileName(path); window.preview->document()->print(&printer); QVERIFY(QFileInfo(path).size() > 1000); }
    void screenshot() { Window window; window.editor->setPlainText(QString::fromUtf8("# 墨笺 Linux\n\n原生 Qt 编辑器 · 中文与 emoji 😀\n\n**粗体** 和 *斜体*，支持撤销与原子保存。\n\n---\n\n| 项目 | 状态 |\n| --- | --- |\n| 输入 / 保存 / 撤销 | 已实现 |\n| 中文帮助 | F1 |\n\n- [x] 原生桌面界面\n- [ ] 继续完善 Linux 功能\n")); window.setMode("split"); window.show(); QTest::qWait(350); QVERIFY(window.grab().save("linux-window.png")); window.editor->document()->setModified(false); }
};
int main(int argc, char **argv) {
    QApplication app(argc, argv); Q_INIT_RESOURCE(resources); QStandardPaths::setTestModeEnabled(true); QApplication::setOrganizationName("MojianTests"); QApplication::setApplicationName("native-regression"); QSettings().clear();QSettings().setValue("language","zh_CN");
    EditorTests tests; int result = QTest::qExec(&tests, argc, argv); QSettings().clear(); return result;
}
#include "editor_test.moc"
