#include "editor.h"
#include "markdown.h"
#include "services.h"
#include "live.h"
#include "spell.h"
#include <QtConcurrent>
#include <QStringDecoder>
#include <QSaveFile>
#include <QCryptographicHash>
#include <algorithm>

namespace Mojian {
QList<QPointer<Window>> Window::windows;

QString decodeText(const QByteArray &bytes, bool *ok) {
    QStringDecoder::Encoding encoding = QStringDecoder::Utf8;
    if (bytes.startsWith("\xff\xfe")) encoding = QStringDecoder::Utf16LE;
    else if (bytes.startsWith("\xfe\xff")) encoding = QStringDecoder::Utf16BE;
    QStringDecoder decoder(encoding);
    QString text = decoder.decode(bytes);
    if (text.startsWith(QChar(0xfeff))) text.remove(0, 1);
    if (ok) *ok = !decoder.hasError();
    return text.replace("\r\n", "\n").replace('\r', '\n');
}
QString escapedCell(QString text) {
    return text.replace('|', "\\|").replace('\n', "<br>");
}
QString tableMarkdown(const QList<QStringList> &rows, const QStringList &align) {
    if (rows.isEmpty() || rows.first().isEmpty()) return {};
    int columns = rows.first().size();
    QStringList lines;
    auto line = [](const QStringList &cells) { return "| " + cells.join(" | ") + " |"; };
    for (int r = 0; r < rows.size(); ++r) {
        QStringList cells;
        for (int c = 0; c < columns; ++c) cells << escapedCell(rows[r].value(c));
        lines << line(cells);
        if (r == 0) {
            QStringList marks;
            for (int c = 0; c < columns; ++c) marks << align.value(c, "---");
            lines << line(marks);
        }
    }
    return lines.join('\n') + '\n';
}

Highlighter::Highlighter(QTextDocument *document) : QSyntaxHighlighter(document) {}
void Highlighter::highlightBlock(const QString &text) {
    setCurrentBlockState(0); if(owner){QTextCharFormat base;base.setFontPointSize(owner->font().pointSizeF());setFormat(0,text.size(),base);}
    const bool active = currentBlock().blockNumber() == activeBlock;
    QTextCharFormat code; code.setFontFamilies({"monospace"}); code.setForeground(QColor("#548872"));
    auto fence = QRegularExpression("^ {0,3}(`{3,}|~{3,})(.*)$").match(text);
    int previous = previousBlockState();
    if (previous > 0) {
        setFormat(0, text.size(), code);
        const QChar marker = previous >= 2000 ? '~' : '`';
        int length = previous % 1000;
        if (!(fence.hasMatch() && fence.captured(1).front() == marker && fence.captured(1).size() >= length && fence.captured(2).trimmed().isEmpty())) setCurrentBlockState(previous);
        projectionFormat(); return;
    }
    if (fence.hasMatch()) {
        setCurrentBlockState((fence.captured(1).front() == '~' ? 2000 : 1000) + std::min(999, int(fence.captured(1).size())));
        setFormat(0, text.size(), code); projectionFormat(); return;
    }
    auto hide = [&](int start, int length) {
        QTextCharFormat format;
        if (live) { format.setFontPointSize(1);format.setFontStretch(1); format.setForeground(Qt::transparent); }
        else format.setForeground(QColor("#9399a3"));
        setFormat(start, length, format);
    };
    auto heading = QRegularExpression("^( {0,3})(#{1,6}) +(.+)$").match(text);
    if (heading.hasMatch()) {
        QTextCharFormat format; format.setFontWeight(QFont::Bold); format.setForeground(QColor("#677fca"));
        if (live) format.setFontPointSize(27 - heading.captured(2).size() * 2);
        setFormat(0, text.size(), format); hide(0, heading.capturedStart(3));
    }
    auto mark = [&](const QString &pattern, const QTextCharFormat &format, int delimiter) {
        if(live)return;
        auto matches = QRegularExpression(pattern).globalMatch(text);
        while (matches.hasNext()) {
            auto match = matches.next();
            setFormat(match.capturedStart(), match.capturedLength(), format);
            hide(match.capturedStart(), delimiter);
            hide(match.capturedEnd() - delimiter, delimiter);
        }
    };
    QTextCharFormat bold; bold.setFontWeight(QFont::Bold);
    QTextCharFormat italic; italic.setFontItalic(true);
    QTextCharFormat strike; strike.setFontStrikeOut(true);
    mark("\\*\\*[^*\\n]+\\*\\*", bold, 2);
    mark("(?<!\\*)\\*[^*\\n]+\\*(?!\\*)", italic, 1);
    mark("~~[^~\\n]+~~", strike, 2);
    mark("`[^`\\n]+`", code, 1);
    auto list = QRegularExpression("^\\s*(?:[-+*]|\\d+[.)]) +(?:\\[[ xX]\\] +)?").match(text);
    if (list.hasMatch()) { QTextCharFormat fmt; fmt.setForeground(QColor("#8a85bb")); setFormat(0, list.capturedLength(), fmt); }
    if (text.startsWith('>')) { QTextCharFormat fmt; fmt.setForeground(QColor("#729a8a")); setFormat(0, text.size(), fmt); }
    if (live) {
        auto prefix = QRegularExpression("^ {0,3}(?:> ?)+").match(text);
        if (prefix.hasMatch()) hide(0, prefix.capturedLength());
    }
    if(QSettings().value("spellCheck",false).toBool()) { auto words=QRegularExpression("\\b[A-Za-z][A-Za-z']{2,}\\b").globalMatch(text);while(words.hasNext()){auto word=words.next();if(!Speller::instance().correct(word.captured()))for(int i=word.capturedStart();i<word.capturedEnd();++i){auto fmt=format(i);fmt.setUnderlineStyle(QTextCharFormat::SpellCheckUnderline);fmt.setUnderlineColor(QColor("#ce6572"));setFormat(i,1,fmt);}} }
    projectionFormat();
}
void Highlighter::projectionFormat() {
    if (!live || !owner || !owner->liveController) return;
    auto controller = owner->liveController; int offset = currentBlock().position(), length = currentBlock().text().size();
    for (const auto &style : controller->styles.value(currentBlock().blockNumber())) { for(int i=style.start;i<style.start+style.length;++i){auto merged=format(i);merged.merge(style.format);setFormat(i,1,merged);} }
    for(auto item:controller->inlineByBlock.value(currentBlock().blockNumber())){int start=item.start-offset;QTextCharFormat fmt;fmt.setForeground(Qt::transparent);fmt.setFontPointSize(1);fmt.setFontStretch(1);setFormat(start,item.length,fmt);QFont font=owner->font();font.setPointSizeF(std::max<qreal>(8,item.image.height()*.62));QFontMetricsF metrics(font);fmt.setFont(font);fmt.setFontLetterSpacingType(QFont::AbsoluteSpacing);fmt.setFontLetterSpacing(item.image.width()-metrics.horizontalAdvance(currentBlock().text().at(start)));setFormat(start,1,fmt);}
    int quoteLength=controller->quotePrefixes.value(currentBlock().blockNumber());if(quoteLength>0){QTextCharFormat fmt;fmt.setForeground(Qt::transparent);fmt.setFontPointSize(1);fmt.setFontStretch(1);setFormat(0,quoteLength,fmt);QFont font=owner->font();fmt.setFont(font);fmt.setFontLetterSpacingType(QFont::AbsoluteSpacing);fmt.setFontLetterSpacing(currentBlock().text().left(quoteLength).count('>')*12-QFontMetrics(font).horizontalAdvance(currentBlock().text().front()));setFormat(0,1,fmt);}
    for(auto item:controller->decorationsByBlock.value(currentBlock().blockNumber())){int start=item.start-offset;QTextCharFormat fmt;fmt.setForeground(Qt::transparent);fmt.setFontPointSize(1);fmt.setFontStretch(1);setFormat(start,item.length,fmt);QFont font=owner->font();fmt.setFont(font);fmt.setFontLetterSpacingType(QFont::AbsoluteSpacing);fmt.setFontLetterSpacing((item.task?24:std::max(22,QFontMetrics(font).horizontalAdvance(item.marker)+8))-QFontMetrics(font).horizontalAdvance(currentBlock().text().at(start)));setFormat(start,1,fmt);}
    int height = controller->reserve.value(currentBlock().blockNumber());
    if (height && length) { QTextCharFormat fmt; fmt.setFontPointSize(1);fmt.setFontStretch(1); fmt.setForeground(Qt::transparent); setFormat(0, length, fmt); fmt.setFontStretch(100);fmt.setFontPointSize(std::max<qreal>(12, height * .62)); setFormat(0, 1, fmt); }
}

Editor::Editor(QWidget *parent) : QPlainTextEdit(parent) {
    setObjectName("markdownEditor");
    sourceCache=QPlainTextEdit::toPlainText();connect(this,&QPlainTextEdit::textChanged,this,[this]{auto next=QPlainTextEdit::toPlainText();if(next!=sourceCache){sourceCache=next;emit sourceChanged();}});
    highlighter = new Highlighter(document()); highlighter->owner = this;
    liveController = new LiveController(this);
    QFont font("sans-serif", 13); setFont(font); setTabStopDistance(fontMetrics().horizontalAdvance(' ') * 4);
    setPlaceholderText("开始写作……\n\n# 标题\n\n正文，**粗体** 和 *斜体*。");
    setLineWrapMode(QPlainTextEdit::WidgetWidth);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, [this] {
        int old = highlighter->activeBlock; highlighter->activeBlock = textCursor().blockNumber();
        if (old != highlighter->activeBlock) {
            highlighter->rehighlightBlock(document()->findBlockByNumber(old));
            highlighter->rehighlightBlock(textCursor().block()); viewport()->update();
        }
    });
}
void Editor::setLive(bool live) { highlighter->live = live; if (live) liveController->schedule(); else liveController->restore(); highlighter->rehighlight(); viewport()->update(); }
void Editor::wrap(const QString &before,const QString &after){
    auto cursor=textCursor();QString text=cursor.selectedText().replace(QChar::ParagraphSeparator,'\n'),closing=after.isNull()?before:after;int start=cursor.selectionStart(),end=cursor.selectionEnd();QString source=toPlainText();
    if(!cursor.hasSelection()&&highlighter->live&&QStringList{"**","*","~~","`","==","<u>"}.contains(before)){
        if(outsidePosition>=0){outsidePosition=-1;outsideBefore.clear();outsideAfter.clear();return;}
        auto snapshot=Markdown::parse(source,QSettings().value("dialect","extended").toString());QMap<QString,QString> kinds{{"**","strong"},{"*","emph"},{"~~","strikethrough"},{"`","code"},{"==","mark_inline"}};QList<MdNode> matches;for(auto node:Markdown::descendants(snapshot->root))if(node.type==kinds.value(before,"none")&&source.mid(node.start,before.size())==before&&source.mid(node.end-closing.size(),closing.size())==closing&&start>=node.start+before.size()&&start<=node.end-closing.size())matches<<node;
        if(before=="<u>"){int open=source.lastIndexOf("<u>",start),close=source.indexOf("</u>",start);if(open>=0&&close>=start&&open+3<=start){MdNode node;node.start=open;node.end=close+4;matches<<node;}}
        std::sort(matches.begin(),matches.end(),[](const MdNode &a,const MdNode &b){return a.end-a.start<b.end-b.start;});if(!matches.isEmpty()){auto node=matches.first();pendingPrefix.clear();pendingSuffix.clear();if(start==node.end-closing.size())cursor.setPosition(node.end);else if(start==node.start+before.size()){cursor.setPosition(node.start);outsidePosition=node.start;outsideBefore.clear();outsideAfter.clear();}else{outsidePosition=start;outsideBefore=closing;outsideAfter=before;}setTextCursor(cursor);setFocus();return;}
        if(pendingPrefix==before){pendingPrefix.clear();pendingSuffix.clear();}else{pendingPrefix=before;pendingSuffix=closing;}setFocus();return;
    }
    pendingPrefix.clear();pendingSuffix.clear();outsidePosition=-1;
    if(text.startsWith(before)&&text.endsWith(closing)&&text.size()>=before.size()+closing.size()){text=text.mid(before.size(),text.size()-before.size()-closing.size());cursor.beginEditBlock();cursor.insertText(text);cursor.endEditBlock();cursor.setPosition(start);cursor.setPosition(start+text.size(),QTextCursor::KeepAnchor);}
    else if(!(before=="*"&&start>=2&&end+2<=source.size()&&source.mid(start-2,2)=="**"&&source.mid(end,2)=="**"&&!(start>=3&&source.mid(start-3,3)=="***"))&&start>=before.size()&&source.mid(start-before.size(),before.size())==before&&source.mid(end,closing.size())==closing){start-=before.size();cursor.setPosition(start);cursor.setPosition(end+closing.size(),QTextCursor::KeepAnchor);cursor.beginEditBlock();cursor.insertText(text);cursor.endEditBlock();cursor.setPosition(start);cursor.setPosition(start+text.size(),QTextCursor::KeepAnchor);}
    else{cursor.beginEditBlock();cursor.insertText(before+text+closing);cursor.endEditBlock();cursor.setPosition(start+before.size());cursor.setPosition(start+before.size()+text.size(),QTextCursor::KeepAnchor);}setTextCursor(cursor);setFocus();
}
void Editor::insertFormattedText(const QString &text){auto cursor=textCursor();QString prefix=pendingPrefix,suffix=pendingSuffix;if(outsidePosition>=0){if(cursor.position()==outsidePosition){prefix=outsideBefore;suffix=outsideAfter;}outsidePosition=-1;outsideBefore.clear();outsideAfter.clear();}pendingPrefix.clear();pendingSuffix.clear();int start=cursor.selectionStart();cursor.beginEditBlock();cursor.insertText(prefix+text+suffix);cursor.endEditBlock();cursor.setPosition(start+prefix.size()+text.size());setTextCursor(cursor);}
void Editor::prefix(const QString &prefix) {
    auto cursor = textCursor(); int start = cursor.selectionStart(), end = cursor.selectionEnd();
    auto first = document()->findBlock(start), last = document()->findBlock(end);
    if (end > start && end == last.position()) last = last.previous();
    cursor.beginEditBlock();
    for (auto block = last; block.isValid() && block.position() >= first.position(); block = block.previous()) {
        QTextCursor line(block); line.insertText(prefix);
    }
    cursor.endEditBlock(); setFocus();
}
void Editor::heading(int level){auto cursor=textCursor();int start=cursor.selectionStart(),end=cursor.selectionEnd();auto first=document()->findBlock(start),last=document()->findBlock(end);if(end>start&&last.position()==end)last=last.previous();cursor.beginEditBlock();for(auto block=last;block.isValid()&&block.position()>=first.position();block=block.previous()){QTextCursor line(block);auto match=QRegularExpression("^ {0,3}#{1,6} +").match(block.text());if(match.hasMatch()){line.movePosition(QTextCursor::Right,QTextCursor::KeepAnchor,match.capturedLength());line.removeSelectedText();}if(level>0)line.insertText(QString(level,'#')+" ");}cursor.endEditBlock();setFocus();}
void Editor::unindent(){auto cursor=textCursor();int start=cursor.selectionStart(),end=cursor.selectionEnd();auto first=document()->findBlock(start),last=document()->findBlock(end);if(end>start&&last.position()==end)last=last.previous();cursor.beginEditBlock();for(auto block=last;block.isValid()&&block.position()>=first.position();block=block.previous()){auto match=QRegularExpression("^(?: {1,4}|\\t)").match(block.text());if(match.hasMatch()){QTextCursor line(block);line.movePosition(QTextCursor::Right,QTextCursor::KeepAnchor,match.capturedLength());line.removeSelectedText();}}cursor.endEditBlock();}
void Editor::insertRule() { auto cursor = textCursor(); cursor.insertText("\n\n---\n\n"); setTextCursor(cursor); }
QRegularExpression Editor::expression(const QString &text, bool sensitive, bool regex, bool whole) {
    QString pattern = regex ? text : QRegularExpression::escape(text);
    if (whole) pattern = "(?<![\\p{L}\\p{N}_])(?:" + pattern + ")(?![\\p{L}\\p{N}_])";
    QRegularExpression re(pattern, QRegularExpression::UseUnicodePropertiesOption | (sensitive ? QRegularExpression::NoPatternOption : QRegularExpression::CaseInsensitiveOption));
    return re;
}
bool Editor::findText(const QString &text, bool backwards, bool sensitive, bool regex, bool whole) {
    if (text.isEmpty()) return false;
    auto re = expression(text, sensitive, regex, whole); if (!re.isValid()) return false;
    auto flag = backwards ? QTextDocument::FindBackward : QTextDocument::FindFlags();
    auto found = document()->find(re, textCursor(), flag);
    if (found.isNull()) found = document()->find(re, backwards ? document()->characterCount() - 1 : 0, flag);
    if(!searchScope.isNull()){
        if(scopeSource!=toPlainText())return false;
        if(found.isNull()||found.selectionStart()<searchScope.selectionStart()||found.selectionEnd()>searchScope.selectionEnd())found=document()->find(re,backwards?searchScope.selectionEnd():searchScope.selectionStart(),flag);
        if(found.isNull()||found.selectionStart()<searchScope.selectionStart()||found.selectionEnd()>searchScope.selectionEnd())return false;
    }
    if (found.isNull()) return false; setTextCursor(found); ensureCursorVisible(); return true;
}
int Editor::replaceAll(const QString &needle, const QString &replacement, bool sensitive, bool regex, bool whole) {
    if (needle.isEmpty()) return 0;
    auto re = expression(needle, sensitive, regex, whole); if (!re.isValid()) return -1;
    const QString original = toPlainText();
    if(!searchScope.isNull()&&scopeSource!=original)return -1;
    auto matches = re.globalMatch(original);
    QList<QPair<QRegularExpressionMatch, QString>> edits;
    while (matches.hasNext()) {
        auto match = matches.next(); if(!searchScope.isNull()&&(match.capturedStart()<searchScope.selectionStart()||match.capturedEnd()>searchScope.selectionEnd()))continue; QString value = replacement;
        if (regex) {
            // $1..$99 capture substitutions, $$ for a literal dollar sign.
            value.clear();
            for (int i = 0; i < replacement.size(); ++i) {
                if (replacement[i] == '$' && i + 1 < replacement.size()) {
                    if (replacement[i + 1] == '$') { value += '$'; ++i; continue; }
                    int j = i + 1; while (j < replacement.size() && j < i + 3 && replacement[j].isDigit()) ++j;
                    if (j > i + 1) { value += match.captured(replacement.mid(i + 1, j - i - 1).toInt()); i = j - 1; continue; }
                }
                value += replacement[i];
            }
        }
        edits.append({match, value});
    }
    QTextCursor cursor(document()); cursor.beginEditBlock();
    for (auto it = edits.crbegin(); it != edits.crend(); ++it) {
        cursor.setPosition(it->first.capturedStart()); cursor.setPosition(it->first.capturedEnd(), QTextCursor::KeepAnchor); cursor.insertText(it->second);
    }
    cursor.endEditBlock(); if(!searchScope.isNull())scopeSource=toPlainText(); return edits.size();
}
void Editor::paintEvent(QPaintEvent *event) {
    QPlainTextEdit::paintEvent(event); if (!highlighter->live) return;
    QPainter painter(viewport()); painter.setPen(QPen(palette().color(QPalette::Mid), 1));
    auto block = firstVisibleBlock();
    while (block.isValid()) {
        auto rect = blockBoundingGeometry(block).translated(contentOffset());
        if (rect.top() > viewport()->height()) break;
        if (liveController->thematicBlocks.contains(block.blockNumber())) {
            QRectF background(0, rect.top(), viewport()->width(), rect.height());
            painter.fillRect(background, palette().base());
            painter.drawLine(QPointF(12, rect.center().y()), QPointF(viewport()->width() - 16, rect.center().y()));
        }
        block = block.next();
    }
    liveController->paint(painter); liveController->positionWidgets();
}
bool Editor::event(QEvent *event){if(event->type()==QEvent::ShortcutOverride){auto key=static_cast<QKeyEvent *>(event);if(!composing&&key->modifiers().testFlag(Qt::ControlModifier)&&QList<int>{Qt::Key_Z,Qt::Key_Y}.contains(key->key())){auto owner=qobject_cast<Window *>(window());if(owner&&owner->editor!=this){event->accept();return true;}}if(key->modifiers().testFlag(Qt::ControlModifier)&&QList<int>{Qt::Key_B,Qt::Key_I,Qt::Key_U}.contains(key->key())){event->accept();return true;}if(key->key()==Qt::Key_X&&key->modifiers()==(Qt::ControlModifier|Qt::ShiftModifier)){event->accept();return true;}}return QPlainTextEdit::event(event);}
void Editor::keyPressEvent(QKeyEvent *event) {
    if(!composing&&event->modifiers().testFlag(Qt::ControlModifier)&&QList<int>{Qt::Key_Z,Qt::Key_Y}.contains(event->key())){auto owner=qobject_cast<Window *>(window());if(owner&&owner->editor!=this){owner->editor->setFocus();if(event->key()==Qt::Key_Y||event->modifiers().testFlag(Qt::ShiftModifier))owner->editor->redo();else owner->editor->undo();owner->editor->liveController->refresh();return;}}

    if(!composing&&(!pendingPrefix.isEmpty()||outsidePosition>=0)&&!(event->modifiers()&(Qt::ControlModifier|Qt::AltModifier|Qt::MetaModifier))&&!event->text().isEmpty()&&event->text()[0].isPrint()&&event->key()!=Qt::Key_Return&&event->key()!=Qt::Key_Enter&&event->key()!=Qt::Key_Tab){insertFormattedText(event->text());return;}

    if(!composing&&event->modifiers()==Qt::ControlModifier){if(event->key()==Qt::Key_B){wrap("**");return;}if(event->key()==Qt::Key_I){wrap("*");return;}if(event->key()==Qt::Key_U){wrap("<u>","</u>");return;}}
    if(!composing&&event->modifiers()==(Qt::ControlModifier|Qt::ShiftModifier)&&event->key()==Qt::Key_X){wrap("~~");return;}

    if(highlighter->live&&!composing&&!textCursor().hasSelection()&&event->modifiers()==Qt::NoModifier&&QList<int>{Qt::Key_Left,Qt::Key_Right,Qt::Key_Backspace,Qt::Key_Delete}.contains(event->key())){
        QString source=toPlainText();auto cursor=textCursor();int position=cursor.position();auto snapshot=liveController->snapshot&&liveController->revision==document()->revision()?liveController->snapshot:Markdown::parse(source,QSettings().value("dialect","extended").toString());liveController->snapshot=snapshot;liveController->revision=document()->revision();QList<QPair<int,int>> markers;
        for(auto item:liveController->decorations){markers<<qMakePair(item.start,item.start+item.length);if(event->key()==Qt::Key_Backspace&&position==item.start+item.length){cursor.setPosition(item.start);cursor.setPosition(item.start+item.length,QTextCursor::KeepAnchor);cursor.removeSelectedText();setTextCursor(cursor);return;}}
        for(auto image:liveController->inlineImages){markers<<qMakePair(image.start,image.start+image.length);if((event->key()==Qt::Key_Backspace&&position==image.start+image.length)||(event->key()==Qt::Key_Delete&&position==image.start)){cursor.setPosition(image.start);cursor.setPosition(image.start+image.length,QTextCursor::KeepAnchor);cursor.removeSelectedText();setTextCursor(cursor);return;}}
        for(auto node:Markdown::descendants(snapshot->root))if(QStringList{"strong","emph","strikethrough","link","code","mark_inline","sup_inline","sub_inline"}.contains(node.type)){int start=node.start,end=node.end;if(!node.children.isEmpty()){start=node.children.first().start;end=node.children.last().end;}else if(node.type=="code"||node.type.endsWith("_inline")){QString raw=source.mid(node.start,node.end-node.start);int offset=raw.indexOf(node.literal);if(offset>=0){start=node.start+offset;end=start+node.literal.size();}}if(start>node.start)markers<<qMakePair(node.start,start);if(end<node.end)markers<<qMakePair(end,node.end);
            if((event->key()==Qt::Key_Backspace&&position==start&&start>node.start)||(event->key()==Qt::Key_Delete&&position==end&&end<node.end)){QString body=source.mid(start,end-start);cursor.setPosition(node.start);cursor.setPosition(node.end,QTextCursor::KeepAnchor);cursor.beginEditBlock();cursor.insertText(body);cursor.endEditBlock();cursor.setPosition(node.start+(event->key()==Qt::Key_Delete?body.size():0));setTextCursor(cursor);return;}}
        if(event->key()==Qt::Key_Left||event->key()==Qt::Key_Right){bool backwards=event->key()==Qt::Key_Left;int next=std::clamp(position+(backwards?-1:1),0,int(source.size()));bool moved;do{moved=false;for(auto marker:markers)if(next>marker.first&&next<marker.second){next=backwards?marker.first:marker.second;moved=true;break;}}while(moved);if(next!=position+(backwards?-1:1)){cursor.setPosition(next);setTextCursor(cursor);return;}}
    }

    if (!composing && QSettings().value("autoPairs",true).toBool() && event->modifiers()==Qt::NoModifier) {
        auto cursor=textCursor();QString text=event->text();QString opening="([{\"'",closing=")] }\"'";closing.remove(' ');
        if (text.size()==1 && closing.contains(text) && !cursor.hasSelection() && document()->characterAt(cursor.position())==text[0]) {cursor.movePosition(QTextCursor::Right);setTextCursor(cursor);return;}
        if (text.size()==1 && opening.contains(text) && !(text=="'" && cursor.position()>0 && document()->characterAt(cursor.position()-1).isLetter())) {int index=opening.indexOf(text);wrap(text,QString(closing[index]));return;}
        if(event->key()==Qt::Key_Backspace&&!cursor.hasSelection()&&cursor.position()>0){QChar before=document()->characterAt(cursor.position()-1),after=document()->characterAt(cursor.position());int index=opening.indexOf(before);if(index>=0&&closing[index]==after){cursor.movePosition(QTextCursor::Left);cursor.movePosition(QTextCursor::Right,QTextCursor::KeepAnchor,2);cursor.removeSelectedText();setTextCursor(cursor);return;}}
    }
    if(event->key()==Qt::Key_Backtab){unindent();return;}
    if (event->key() == Qt::Key_Tab && !event->modifiers().testFlag(Qt::ControlModifier)) {
        if (textCursor().hasSelection()) prefix("    "); else insertPlainText("    "); return;
    }
    if (event->key() == Qt::Key_Return && event->modifiers() == Qt::NoModifier && !textCursor().hasSelection()) {
        auto cursor = textCursor();
        auto fence=QRegularExpression("^ {0,3}(`{3,}|~{3,})(.*)$").match(cursor.block().text());
        if(!property("mojianCodeBody").toBool()&&cursor.atBlockEnd()&&fence.hasMatch()&&(!cursor.block().previous().isValid()||cursor.block().previous().userState()<=0)&&!(fence.captured(1).startsWith('`')&&fence.captured(2).contains('`'))){int position=cursor.position();cursor.beginEditBlock();cursor.insertText("\n\n"+fence.captured(1));cursor.endEditBlock();cursor.setPosition(position+1);setTextCursor(cursor);return;}
        auto quote=QRegularExpression("^( {0,3}(?:> ?)+)(.*)$").match(cursor.block().text());
        if(cursor.atBlockEnd()&&quote.hasMatch()){cursor.beginEditBlock();if(quote.captured(2).isEmpty()){cursor.movePosition(QTextCursor::StartOfBlock,QTextCursor::KeepAnchor);cursor.removeSelectedText();}else cursor.insertText("\n"+quote.captured(1));cursor.endEditBlock();setTextCursor(cursor);return;}
        auto match = QRegularExpression("^(\\s*)([-+*]|\\d+[.)]) +(\\[[ xX]\\] +)?(.*)$").match(cursor.block().text());
        if (match.hasMatch() && cursor.atBlockEnd() && cursor.block().userState() == 0) {
            cursor.beginEditBlock();
            if (match.captured(4).isEmpty()) { cursor.movePosition(QTextCursor::StartOfBlock, QTextCursor::KeepAnchor); cursor.removeSelectedText(); }
            else {
                QString marker = match.captured(2); auto number = QRegularExpression("^(\\d+)([.)])$").match(marker);
                if (number.hasMatch()) marker = QString::number(number.captured(1).toInt() + 1) + number.captured(2);
                cursor.insertText("\n" + match.captured(1) + marker + " " + (match.captured(3).isEmpty() ? QString() : "[ ] "));
            }
            cursor.endEditBlock(); setTextCursor(cursor); return;
        }
    }
    QPlainTextEdit::keyPressEvent(event);
}
void Editor::insertFromMimeData(const QMimeData *mime) {
    if (mime->hasImage()) {
        auto owner=qobject_cast<Window *>(window());
        if(owner&&owner->filePath.isEmpty()){QMessageBox::information(owner,"先保存文档","请先保存 Markdown，再粘贴图片附件。");return;}
        if(owner){QImage image=qvariant_cast<QImage>(mime->imageData());if(image.isNull())return;QDir folder(QFileInfo(owner->filePath).absolutePath());folder.mkpath("images");QString relative="images/"+QUuid::createUuid().toString(QUuid::WithoutBraces)+".png";QSaveFile file(folder.filePath(relative));QByteArray bytes;QBuffer buffer(&bytes);buffer.open(QIODevice::WriteOnly);if(image.save(&buffer,"PNG")&&file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size()&&file.commit())insertPlainText("![截图]("+relative+")");return;}
    }
    if (mime->hasUrls()) {
        QStringList text;
        for (const auto &url : mime->urls()) text << "![图片](" + url.toString(QUrl::FullyEncoded) + ")";
        insertPlainText(text.join('\n')); return;
    }
    if(mime->hasText()&&(!pendingPrefix.isEmpty()||outsidePosition>=0)){insertFormattedText(mime->text());return;}
    QPlainTextEdit::insertFromMimeData(mime);
}

Window::Window(const QString &path, QWidget *parent) : QMainWindow(parent) {
    setAttribute(Qt::WA_DeleteOnClose); resize(1200, 820); setWindowIcon(QIcon(":/mojian/icon.png"));
    editor = new Editor; preview = new QTextBrowser; preview->setDocument(new ImageDocument(preview)); services = new Services(this); preview->setObjectName("markdownPreview");
    preview->setOpenExternalLinks(false); preview->setOpenLinks(false);
    connect(preview,&QTextBrowser::anchorClicked,this,[this](const QUrl &url){if(url.scheme()=="mojian-task"){bool ok;int offset=url.path().toInt(&ok);if(!ok)return;auto block=editor->document()->findBlock(offset);auto match=QRegularExpression("^\\s*[-+*] +\\[([ xX])\\]").match(block.text());if(match.hasMatch()){auto cursor=editor->textCursor();cursor.setPosition(block.position()+match.capturedStart(1));cursor.movePosition(QTextCursor::Right,QTextCursor::KeepAnchor);cursor.insertText(match.captured(1)==" "?"x":" ");}}else if(url.hasFragment()&&url.fragment().startsWith("source-")){int offset=url.fragment().mid(7).toInt();auto cursor=editor->textCursor();cursor.setPosition(std::clamp(offset,0,editor->document()->characterCount()-1));editor->setTextCursor(cursor);preview->scrollToAnchor(url.fragment());}else if(QStringList{"http","https","mailto","file"}.contains(url.scheme()))QDesktopServices::openUrl(url);}); preview->setFont(QFont("sans-serif", 13));
    preview->document()->setDocumentMargin(24);
    splitter = new QSplitter; splitter->addWidget(editor); splitter->addWidget(preview); splitter->setSizes({600, 600});
    auto center = new QWidget; auto layout = new QVBoxLayout(center); layout->setContentsMargins(12, 8, 12, 8);
    findBar = new QWidget; auto findLayout = new QGridLayout(findBar); needle = new QLineEdit; replacement = new QLineEdit;
    needle->setPlaceholderText("查找内容"); replacement->setPlaceholderText("替换为");
    caseCheck = new QCheckBox("区分大小写"); regexCheck = new QCheckBox("正则表达式"); wholeCheck = new QCheckBox("全字匹配"); selectionCheck = new QCheckBox("仅在选区中");
    connect(selectionCheck, &QCheckBox::toggled, this, [this](bool checked) { if (checked && editor->textCursor().hasSelection()) { editor->searchScope = editor->textCursor(); editor->scopeSource = editor->toPlainText(); } else { editor->searchScope = QTextCursor(); if (checked) selectionCheck->setChecked(false); } });
    auto next = new QPushButton("下一处"), previous = new QPushButton("上一处"), replace = new QPushButton("替换"), all = new QPushButton("全部替换"), close = new QPushButton("关闭");
    findStatus = new QLabel;
    findLayout->addWidget(needle, 0, 0); findLayout->addWidget(previous, 0, 1); findLayout->addWidget(next, 0, 2); findLayout->addWidget(caseCheck, 0, 3); findLayout->addWidget(regexCheck, 0, 4); findLayout->addWidget(wholeCheck, 0, 5);
    findLayout->addWidget(replacement, 1, 0); findLayout->addWidget(replace, 1, 1); findLayout->addWidget(all, 1, 2); findLayout->addWidget(findStatus, 1, 3, 1, 2); findLayout->addWidget(close, 1, 5); findLayout->addWidget(selectionCheck, 2, 3);
    layout->addWidget(findBar); layout->addWidget(splitter, 1); setCentralWidget(center); findBar->hide();
    connect(next, &QPushButton::clicked, this, [this] { find(); }); connect(previous, &QPushButton::clicked, this, [this] { find(true); });
    connect(needle, &QLineEdit::returnPressed, this, [this] { find(); }); connect(replace, &QPushButton::clicked, this, &Window::replaceOne);
    connect(all, &QPushButton::clicked, this, [this] {
        int count = editor->replaceAll(needle->text(), replacement->text(), caseCheck->isChecked(), regexCheck->isChecked(), wholeCheck->isChecked());
        findStatus->setText(count < 0 ? "正则表达式无效" : QString("替换 %1 处").arg(count));
    });
    connect(close, &QPushButton::clicked, findBar, &QWidget::hide);
    outlineDock = new QDockWidget("文档大纲", this); outline = new QTreeWidget; outline->setHeaderHidden(true); outlineDock->setWidget(outline); addDockWidget(Qt::LeftDockWidgetArea, outlineDock);
    connect(outline, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *item, int) {
        auto cursor = editor->textCursor(); cursor.setPosition(item->data(0, Qt::UserRole).toInt()); editor->setTextCursor(cursor); editor->ensureCursorVisible(); editor->setFocus();
    });
    folderDock = new QDockWidget("文件夹", this); files = new QTreeView; fileModel = new QFileSystemModel(this); fileModel->setNameFilters({"*.md", "*.markdown", "*.txt"}); fileModel->setNameFilterDisables(false); files->setModel(fileModel); folderDock->setWidget(files); addDockWidget(Qt::LeftDockWidgetArea, folderDock); folderDock->hide();
    for (int c = 1; c < 4; ++c) files->hideColumn(c);
    connect(files, &QTreeView::doubleClicked, this, [this](const QModelIndex &index) { if (!fileModel->isDir(index)) Window::openDocument(fileModel->filePath(index)); });
    position = new QLabel; statusBar()->addPermanentWidget(position);
    auto statistics=new QLabel;statusBar()->addWidget(statistics);auto countTimer=new QTimer(this);countTimer->setSingleShot(true);countTimer->setInterval(400);auto worker=new QFutureWatcher<QPair<int,int>>(this);
    connect(editor,&Editor::sourceChanged,countTimer,[countTimer]{countTimer->start();});
    connect(countTimer,&QTimer::timeout,this,[this,worker]{if(worker->isRunning())return;QString source=editor->toPlainText();worker->setProperty("revision",editor->document()->revision());worker->setFuture(QtConcurrent::run([source]{int characters=0,words=0;for(int i=0;i<source.size();++i){++characters;if(source[i].isHighSurrogate()&&i+1<source.size()&&source[i+1].isLowSurrogate())++i;}auto matches=QRegularExpression("[\\p{L}\\p{N}_]+",QRegularExpression::UseUnicodePropertiesOption).globalMatch(source);while(matches.hasNext()){matches.next();++words;}return qMakePair(characters,words);}));});
    connect(worker,&QFutureWatcher<QPair<int,int>>::finished,this,[this,statistics,worker,countTimer]{if(worker->property("revision").toInt()!=editor->document()->revision()){countTimer->start();return;}auto result=worker->result();statistics->setText(QString("%1 字符 · %2 词").arg(result.first).arg(result.second));});
    renderTimer = new QTimer(this); renderTimer->setSingleShot(true); renderTimer->setInterval(160);
    preview->viewport()->installEventFilter(this);
    outlineTimer = new QTimer(this); outlineTimer->setSingleShot(true); outlineTimer->setInterval(300);
    connect(renderTimer, &QTimer::timeout, this, &Window::refreshPreview); connect(outlineTimer, &QTimer::timeout, this, &Window::updateOutline);
    connect(editor, &Editor::sourceChanged, this, [this] { if (currentMode == "split" || currentMode == "reading") renderTimer->start(); outlineTimer->start(); updateTitle(); });
    connect(editor->document(), &QTextDocument::modificationChanged, this, [this] { updateTitle(); });
    connect(editor, &QPlainTextEdit::cursorPositionChanged, this, [this] {
        const auto cursor = editor->textCursor(); position->setText(QString("第 %1 行，第 %2 列 · %3 字符").arg(cursor.blockNumber() + 1).arg(cursor.positionInBlock() + 1).arg(editor->document()->characterCount() - 1));
    });
    connect(editor->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        if (currentMode != "split" || syncingScroll || !QSettings().value("syncScroll",true).toBool()) return; syncingScroll = true;
        auto bar = preview->verticalScrollBar(); bar->setValue(editor->verticalScrollBar()->maximum() ? qRound(double(value) / editor->verticalScrollBar()->maximum() * bar->maximum()) : 0); syncingScroll = false;
    });
    connect(&watcher,&QFileSystemWatcher::fileChanged,this,[this](const QString &path){QTimer::singleShot(120,this,[this,path]{QFile file(path);if(!file.open(QIODevice::ReadOnly)){statusBar()->showMessage("磁盘文件已移走或删除，可另存为保留当前文档。",10000);return;}auto hash=QCryptographicHash::hash(file.readAll(),QCryptographicHash::Sha256);if(hash!=diskHash){if(!editor->document()->isModified()&&!focusedEditor()->composing)loadPath(path);else statusBar()->showMessage("磁盘文件已被外部修改，保存时请确认或另存为。",15000);}if(!watcher.files().contains(path)&&QFileInfo::exists(path))watcher.addPath(path);});});
    recoveryKey=QUuid::createUuid().toString(QUuid::WithoutBraces);recoveryTimer.setInterval(15000);connect(&recoveryTimer,&QTimer::timeout,this,[this]{if(!editor->document()->isModified())return;QString directory=QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/recovery";QDir().mkpath(directory);QSaveFile file(directory+"/"+recoveryKey+".json");if(file.open(QIODevice::WriteOnly)){file.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner);file.write(QJsonDocument(QJsonObject{{"path",filePath},{"text",editor->toPlainText()},{"lineEnding",lineEnding}}).toJson());file.commit();}});recoveryTimer.start();
    connect(editor,&QPlainTextEdit::cursorPositionChanged,this,[this]{if(typewriterMode)editor->centerCursor();if(focusMode){QTextEdit::ExtraSelection dim,current;dim.cursor=QTextCursor(editor->document());dim.cursor.select(QTextCursor::Document);dim.format.setForeground(editor->palette().color(QPalette::PlaceholderText));current.cursor=editor->textCursor();current.cursor.select(QTextCursor::BlockUnderCursor);current.format.setForeground(editor->palette().color(QPalette::Text));editor->setExtraSelections({dim,current});}else editor->setExtraSelections({});});
    menus();
    QSettings settings; auto font = editor->font(); font.setPointSize(settings.value("fontSize", 13).toInt()); editor->setFont(font);
    setMode(path.isEmpty()?QString("live"):settings.value("mode", "live").toString());
    if (!path.isEmpty()) loadPath(path); else updateTitle();
    windows.removeIf([](const QPointer<Window> &window){return window.isNull();});windows.append(this); services->applyTheme(); Services::localize(this);
}
Window *Window::openDocument(const QString &path){QString canonical=QFileInfo(path).canonicalFilePath();if(canonical.isEmpty())canonical=QFileInfo(path).absoluteFilePath();for(auto window:windows)if(window&&!window->filePath.isEmpty()){QString existing=QFileInfo(window->filePath).canonicalFilePath();if(existing.isEmpty())existing=window->filePath;if(existing==canonical){window->showNormal();window->raise();window->activateWindow();return window;}}auto window=new Window(path);window->show();return window;}
Editor *Window::focusedEditor()const{auto input=qobject_cast<Editor *>(QApplication::focusWidget());return input&&input->window()==this?input:editor;}
void Window::updateTitle() { setWindowTitle((editor->document()->isModified() ? "● " : "") + (filePath.isEmpty() ? "未命名" : QFileInfo(filePath).fileName()) + " — 墨笺 Linux 1.0.0"); }
bool Window::loadPath(const QString &path) {
    QFile file(path); if (!file.open(QIODevice::ReadOnly)) { QMessageBox::warning(this, "打开失败", file.errorString()); return false; }
    auto bytes = file.readAll(); bool ok; QString text = decodeText(bytes, &ok);
    if (!ok) { QMessageBox::warning(this, "编码无法识别", "请将文件转换为 UTF-8 或 UTF-16 后再打开；当前文档未被覆盖。"); return false; }
    lineEnding = (bytes.contains("\r\n")||bytes.contains(QByteArray::fromHex("0d000a00"))||bytes.contains(QByteArray::fromHex("000d000a"))) ? "\r\n" : "\n";
    editor->pendingPrefix.clear();editor->pendingSuffix.clear();editor->outsidePosition=-1;editor->setPlainText(text); editor->document()->setModified(false); filePath = QFileInfo(path).absoluteFilePath();
    diskHash=QCryptographicHash::hash(bytes,QCryptographicHash::Sha256); if(!watcher.files().isEmpty())watcher.removePaths(watcher.files());watcher.addPath(filePath);
    updateTitle(); refreshPreview(); updateOutline(); return true;
}
bool Window::savePath(const QString &path) {
    QString canonical=QFileInfo(path).canonicalFilePath();if(!canonical.isEmpty())for(auto window:windows)if(window&&window!=this&&!window->filePath.isEmpty()&&QFileInfo(window->filePath).canonicalFilePath()==canonical){QMessageBox::warning(this,"无法保存","目标文件已在另一个文档窗口打开，请切换到该窗口或另存为其他名称。");return false;}
    if(!diskHash.isEmpty()&&QFileInfo(path).absoluteFilePath()==filePath&&QFileInfo::exists(path)){QFile current(path);if(current.open(QIODevice::ReadOnly)&&QCryptographicHash::hash(current.readAll(),QCryptographicHash::Sha256)!=diskHash){if(QMessageBox::question(this,"磁盘文件已变化","其他程序修改了这个文件。是否覆盖磁盘版本？建议先另存为保留两份。",QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes)return false;}}
    QSaveFile file(path); if (!file.open(QIODevice::WriteOnly)) { QMessageBox::warning(this, "保存失败", file.errorString()); return false; }
    QString source = editor->toPlainText(); if (lineEnding == "\r\n") source.replace("\n", "\r\n"); auto bytes = source.toUtf8();
    if (file.write(bytes) != bytes.size() || !file.commit()) { QMessageBox::warning(this, "保存失败", file.errorString()); return false; }
    QFile::remove(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/recovery/"+recoveryKey+".json");
    filePath = QFileInfo(path).absoluteFilePath(); diskHash=QCryptographicHash::hash(bytes,QCryptographicHash::Sha256); if(!watcher.files().isEmpty())watcher.removePaths(watcher.files());watcher.addPath(filePath); editor->document()->setModified(false); updateTitle();
    preview->document()->setBaseUrl(QUrl::fromLocalFile(QFileInfo(filePath).absolutePath() + "/")); return true;
}
bool Window::save(bool as) {
    if(focusedEditor()->composing){statusBar()->showMessage("请先确认输入法候选，再保存。",5000);return false;}
    QString path = filePath;
    if (as || path.isEmpty()) path = QFileDialog::getSaveFileName(this, "保存 Markdown", path.isEmpty() ? QDir::homePath() + "/未命名.md" : path, "Markdown (*.md *.markdown);;文本文件 (*.txt);;所有文件 (*)");
    return !path.isEmpty() && savePath(path);
}
bool Window::maybeSave() {
    if(focusedEditor()->composing){statusBar()->showMessage("请先确认输入法候选，再关闭文档。",5000);return false;}
    if (!editor->document()->isModified()) return true;
    auto answer = QMessageBox::question(this, "保存修改？", "当前文档尚有未保存的修改。", QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    return answer == QMessageBox::Discard || (answer == QMessageBox::Save && save());
}
void Window::closeEvent(QCloseEvent *event) { if (maybeSave()) { QFile::remove(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/recovery/"+recoveryKey+".json"); event->accept(); } else event->ignore(); }
void Window::open() {
    auto paths = QFileDialog::getOpenFileNames(this, "打开文档", {}, "Markdown (*.md *.markdown *.txt);;所有文件 (*)");
    for (const auto &path : paths) {
        bool existing=false;for(auto window:windows)if(window&&!window->filePath.isEmpty()&&QFileInfo(window->filePath).canonicalFilePath()==QFileInfo(path).canonicalFilePath()){Window::openDocument(path);existing=true;break;}if(existing)continue;
        if (filePath.isEmpty() && editor->toPlainText().isEmpty() && !editor->document()->isModified() && !editor->composing) loadPath(path);
        else Window::openDocument(path);
    }
}
void Window::setMode(const QString &mode) {
    if(auto input=qobject_cast<Editor *>(QApplication::focusWidget()))if(input->composing)return;
    if(services)editor->setFocus();
    currentMode = QStringList{"live", "source", "split", "reading"}.contains(mode) ? mode : "live";
    editor->setVisible(currentMode != "reading"); preview->setVisible(currentMode == "split" || currentMode == "reading");
    editor->setLive(currentMode == "live"); if (currentMode == "split" || currentMode == "reading") {refreshPreview();renderTimer->start();}
    if (modeActions) for (auto action : modeActions->actions()) action->setChecked(action->data().toString() == currentMode);
    QSettings().setValue("mode", currentMode);
}
bool Window::eventFilter(QObject *object,QEvent *event){if(object==preview->viewport()&&event->type()==QEvent::Resize&&(currentMode=="split"||currentMode=="reading"))renderTimer->start();return QMainWindow::eventFilter(object,event);}
void Window::refreshPreview() {
    auto bar = preview->verticalScrollBar(); double ratio = bar->maximum() ? double(bar->value()) / bar->maximum() : 0;
    preview->document()->setBaseUrl(filePath.isEmpty() ? QUrl() : QUrl::fromLocalFile(QFileInfo(filePath).absolutePath() + "/"));
    Markdown::render(preview->document(), editor->toPlainText(), preview->document()->baseUrl(), std::max(120,preview->viewport()->width()-48), QSettings().value("dialect","extended").toString());
    bar->setValue(qRound(ratio * bar->maximum()));
}
void Window::updateOutline() {
    outline->clear();auto snapshot=Markdown::parse(editor->toPlainText(),QSettings().value("dialect","extended").toString());
    for(auto heading:Markdown::descendants(snapshot->root))if(heading.type=="heading"){QString title;for(auto child:Markdown::descendants(heading))if(child.type=="text"||child.type=="code")title+=child.literal;auto item=new QTreeWidgetItem(outline,{QString((heading.heading-1)*2,' ')+title});item->setData(0,Qt::UserRole,heading.start);}
}
void Window::find(bool backwards) {
    auto re = Editor::expression(needle->text(), caseCheck->isChecked(), regexCheck->isChecked(), wholeCheck->isChecked());
    if (!re.isValid()) { findStatus->setText("正则表达式无效"); return; }
    bool found = editor->findText(needle->text(), backwards, caseCheck->isChecked(), regexCheck->isChecked(), wholeCheck->isChecked()); findStatus->setText(found ? "已定位" : "没有匹配项");
}
void Window::replaceOne() {
    auto cursor = editor->textCursor(); auto re = Editor::expression(needle->text(), caseCheck->isChecked(), regexCheck->isChecked(), wholeCheck->isChecked());
    if (needle->text().isEmpty() || !re.isValid()) { findStatus->setText("查找条件无效"); return; }
    QString selected = cursor.selectedText().replace(QChar::ParagraphSeparator, '\n'); auto match = re.match(selected);
    if (!cursor.hasSelection() || !match.hasMatch() || match.capturedLength() != selected.size()) { find(); return; }
    // Use the same capture replacement semantics as Replace All without altering other occurrences.
    Editor temporary; temporary.setPlainText(selected); temporary.replaceAll(needle->text(), replacement->text(), caseCheck->isChecked(), regexCheck->isChecked(), wholeCheck->isChecked());
    cursor.insertText(temporary.toPlainText()); editor->setTextCursor(cursor); find();
}
void Window::insertTable(const QList<QStringList> &rows, const QStringList &align) {
    auto cursor = editor->textCursor(); cursor.beginEditBlock(); cursor.insertText(tableMarkdown(rows, align)); cursor.endEditBlock(); editor->setTextCursor(cursor);
}
static QStringList splitCells(QString line) {
    line = line.trimmed(); if (line.startsWith('|')) line.remove(0, 1); if (line.endsWith('|') && !line.endsWith("\\|")) line.chop(1);
    QStringList cells; QString cell;
    for (int i = 0; i < line.size(); ++i) {
        if (line[i] == '\\' && i + 1 < line.size() && (line[i + 1] == '|' || line[i + 1] == '\\')) { cell += line[++i]; }
        else if (line[i] == '|') { cells << cell.trimmed(); cell.clear(); }
        else cell += line[i];
    }
    cells << cell.trimmed(); return cells;
}
void Window::tableDialog() {
    QTextCursor target=editor->textCursor();TableData data;auto snapshot=Markdown::parse(editor->toPlainText(),QSettings().value("dialect","extended").toString());
    for(auto node:snapshot->root.children)if((node.type=="table"||(node.type=="html_block"&&editor->toPlainText().mid(node.start,20).trimmed().startsWith("<table")))&&target.position()>=node.start&&target.position()<=node.end){int end=node.end;if(end<editor->toPlainText().size()&&editor->toPlainText()[end]=='\n')++end;target.setPosition(node.start);target.setPosition(end,QTextCursor::KeepAnchor);data=TableData::parse(target.selectedText().replace(QChar::ParagraphSeparator,'\n'));break;}
    if(data.rows.isEmpty()){data.rows={{"列 1","列 2","列 3"},{"","",""}};data.alignment={"---","---","---"};}
    QDialog dialog(this);dialog.setWindowTitle("编辑表格");dialog.resize(850,450);QVBoxLayout layout(&dialog);TableWidget widget(data);layout.addWidget(&widget);QDialogButtonBox buttons(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);layout.addWidget(&buttons);connect(&buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(&buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);connect(&widget,&TableWidget::finished,&dialog,&QDialog::accept);
    if(dialog.exec()!=QDialog::Accepted)return;widget.commit();target.beginEditBlock();target.insertText(widget.data.source());target.endEditBlock();editor->setTextCursor(target);editor->liveController->schedule();
}
void Window::chooseFolder() {
    QString folder = QFileDialog::getExistingDirectory(this, "打开文档文件夹"); if (folder.isEmpty()) return;
    fileModel->setRootPath(folder); files->setRootIndex(fileModel->index(folder)); folderDock->show();
}
void Window::image() {
    auto path = QFileDialog::getOpenFileName(this, "插入图片", {}, "图片 (*.png *.jpg *.jpeg *.gif *.webp *.svg)"); if (path.isEmpty()) return;
    QString reference = filePath.isEmpty() ? QUrl::fromLocalFile(path).toString() : QUrl(QDir(QFileInfo(filePath).absolutePath()).relativeFilePath(path)).toString(QUrl::FullyEncoded);
    focusedEditor()->insertPlainText("![" + QFileInfo(path).completeBaseName() + "](" + reference + ")");
}
void Window::preferences() { services->settings(); }
void Window::exportFile(const QString &format) {
    QString base = filePath.isEmpty() ? "未命名" : QFileInfo(filePath).completeBaseName();
    auto path = QFileDialog::getSaveFileName(this, "导出 " + format.toUpper(), base + "." + format, format.toUpper() + " (*." + format + ")"); if (path.isEmpty()) return;
    refreshPreview();
    if (format == "pdf") { ImageDocument printDocument;printDocument.setDefaultFont(QFont("sans-serif",12));Markdown::render(&printDocument,editor->toPlainText(),preview->document()->baseUrl(),470,QSettings().value("dialect","extended").toString(),true);QPrinter printer(QPrinter::HighResolution); printer.setOutputFormat(QPrinter::PdfFormat); printer.setOutputFileName(path); printDocument.print(&printer); auto snapshot=Markdown::parse(editor->toPlainText(),QSettings().value("dialect","extended").toString());QList<QPair<QString,int>> headings;QList<int> levels;auto all=Markdown::descendants(snapshot->root);for(auto heading:all)if(heading.type=="heading"){QString label;for(auto child:Markdown::descendants(heading))if(child.type=="text"||child.type=="code")label+=child.literal;int page=0;QString anchor="source-"+QString::number(heading.start);for(auto block=printDocument.begin();block.isValid();block=block.next()){bool found=false;for(auto part=block.begin();!part.atEnd();++part)if(part.fragment().charFormat().anchorNames().contains(anchor)){found=true;break;}if(found){qreal y=printDocument.documentLayout()->blockBoundingRect(block).top();qreal height=printer.pageRect(QPrinter::Point).height();if(height>0)page=int(y/height);break;}}headings<<qMakePair(label,page);levels<<heading.heading;}if(!Services::pdfBookmarks(path,headings,levels))QMessageBox::warning(this,"PDF 书签","PDF 已保存，但未能写入标题书签。"); }
    else { QSaveFile file(path); if (!file.open(QIODevice::WriteOnly)) { QMessageBox::warning(this, "导出失败", file.errorString()); return; } QString content;if(format=="html"){ImageDocument document;document.setDefaultFont(QFont("sans-serif",12));Markdown::render(&document,editor->toPlainText(),preview->document()->baseUrl(),600,QSettings().value("dialect","extended").toString(),true);content=Services::portableHtml(&document);}else content=preview->toPlainText();auto bytes=content.toUtf8(); if (file.write(bytes) != bytes.size() || !file.commit()) QMessageBox::warning(this, "导出失败", file.errorString()); }
}
void Window::showHelp() {
    if (helpDialog) { helpDialog->show(); helpDialog->raise(); helpDialog->activateWindow(); return; }
    auto dialog = new QDialog(this); dialog->setAttribute(Qt::WA_DeleteOnClose); dialog->setWindowTitle("墨笺 Linux 中文帮助"); dialog->resize(1000, 720); helpDialog = dialog;
    auto layout = new QVBoxLayout(dialog); auto search = new QLineEdit; search->setPlaceholderText("搜索帮助章节，例如：保存、表格、横线"); layout->addWidget(search);
    auto split = new QSplitter; auto chapters = new QListWidget; auto body = new QTextBrowser; body->setOpenExternalLinks(true); body->setFont(QFont("sans-serif", 12)); split->addWidget(chapters); split->addWidget(body); split->setSizes({240, 760}); layout->addWidget(split);
    QFile file(":/mojian/help.md"); if (!file.open(QIODevice::ReadOnly)) { body->setPlainText("帮助资源无法读取，请重新安装完整应用。"); dialog->show(); return; } QString source = QString::fromUtf8(file.readAll());
    QStringList titles, contents; QString title = "开始使用", content;
    // Chapter headings only occur at top level in the bundled guide.
    for (const auto &line : source.split('\n')) {
        if (line.startsWith("## ")) { titles << title; contents << content; title = line.mid(3); content.clear(); }
        content += line + '\n';
    }
    titles << title; contents << content;
    auto populate = [=](const QString &term) {
        chapters->clear();
        for (int i = 0; i < titles.size(); ++i) if (contents[i].contains(term, Qt::CaseInsensitive)) { auto item = new QListWidgetItem(titles[i], chapters); item->setData(Qt::UserRole, i); }
        if (chapters->count()) chapters->setCurrentRow(0); else body->setPlainText("没有匹配章节，请试试其他关键词。");
    };
    connect(chapters, &QListWidget::currentItemChanged, dialog, [=](QListWidgetItem *item) { if (item) body->document()->setMarkdown(contents[item->data(Qt::UserRole).toInt()], QTextDocument::MarkdownFeatures::fromInt(int(QTextDocument::MarkdownDialectGitHub) | int(QTextDocument::MarkdownNoHTML))); });
    connect(search, &QLineEdit::textChanged, dialog, populate); populate(""); dialog->show();
}
void Window::menus() {
    auto add = [this](QMenu *menu, const QString &title, const QKeySequence &key, std::function<void()> callback) {
        auto action = menu->addAction(title); if (!key.isEmpty()) action->setShortcut(key); connect(action, &QAction::triggered, this, [callback] { callback(); }); return action;
    };
    auto file = menuBar()->addMenu("文件(&F)");
    auto newAction = add(file, "新建(&N)", QKeySequence::New, [] { (new Window)->show(); });
    auto openAction = add(file, "打开(&O)…", QKeySequence::Open, [this] { open(); });
    add(file, "打开文件夹…", {}, [this] { chooseFolder(); });
    auto saveAction = add(file, "保存(&S)", QKeySequence::Save, [this] { save(); });
    add(file, "另存为…", QKeySequence::SaveAs, [this] { save(true); }); file->addSeparator();
    auto exports = file->addMenu("导出"); for (const auto &format : {"pdf", "html", "txt"}) add(exports, QString(format).toUpper(), {}, [this, format] { exportFile(format); });
    add(file, "打印…", QKeySequence::Print, [this] { refreshPreview(); QPrinter printer; QPrintDialog dialog(&printer, this); if (dialog.exec() == QDialog::Accepted) { ImageDocument doc;doc.setDefaultFont(QFont("sans-serif",12));Markdown::render(&doc,editor->toPlainText(),preview->document()->baseUrl(),470,QSettings().value("dialect","extended").toString(),true);doc.print(&printer); } });
    file->addSeparator(); add(file, "关闭文档", QKeySequence::Close, [this] { close(); }); add(file, "退出", QKeySequence::Quit, [] { qApp->closeAllWindows(); });
    auto edit = menuBar()->addMenu("编辑(&E)");
    add(edit, "撤销", QKeySequence::Undo, [this] { editor->setFocus(); editor->undo(); editor->liveController->refresh(); }); add(edit, "重做", QKeySequence::Redo, [this] { editor->setFocus(); editor->redo(); editor->liveController->refresh(); }); edit->addSeparator();
    add(edit, "剪切", QKeySequence::Cut, [this] { if (auto focused = qobject_cast<QWidget *>(QApplication::focusWidget())) QMetaObject::invokeMethod(focused, "cut"); });
    add(edit, "复制", QKeySequence::Copy, [this] { if (auto focused = qobject_cast<QWidget *>(QApplication::focusWidget())) QMetaObject::invokeMethod(focused, "copy"); });
    add(edit, "粘贴", QKeySequence::Paste, [this] { if (auto focused = qobject_cast<QWidget *>(QApplication::focusWidget())) QMetaObject::invokeMethod(focused, "paste"); });
    add(edit, "全选", QKeySequence::SelectAll, [this] { if (auto focused = qobject_cast<QWidget *>(QApplication::focusWidget())) QMetaObject::invokeMethod(focused, "selectAll"); }); edit->addSeparator();
    add(edit, "查找…", QKeySequence::Find, [this] { findBar->show(); needle->setFocus(); needle->selectAll(); });
    add(edit, "查找与替换…", QKeySequence("Ctrl+H"), [this] { findBar->show(); needle->setFocus(); });
    add(edit, "下一处", QKeySequence::FindNext, [this] { find(); }); add(edit, "上一处", QKeySequence::FindPrevious, [this] { find(true); });
    add(edit, "跳转到行…", QKeySequence("Ctrl+L"), [this] { bool ok; int line = QInputDialog::getInt(this, "跳转", "源码行号", 1, 1, editor->blockCount(), 1, &ok); if (ok) { auto cursor = editor->textCursor(); cursor.setPosition(editor->document()->findBlockByNumber(line - 1).position()); editor->setTextCursor(cursor); editor->ensureCursorVisible(); } });
    add(edit, "设置…", QKeySequence("Ctrl+,"), [this] { preferences(); });
    auto format = menuBar()->addMenu("格式(&M)");
    add(format, "粗体", QKeySequence("Ctrl+B"), [this] { focusedEditor()->wrap("**"); }); add(format, "斜体", QKeySequence("Ctrl+I"), [this] { focusedEditor()->wrap("*"); });
    add(format, "删除线", QKeySequence("Ctrl+Shift+X"), [this] { focusedEditor()->wrap("~~"); }); add(format, "行内代码", {}, [this] { focusedEditor()->wrap("`"); });
    add(format, "链接…", QKeySequence("Ctrl+K"), [this] { bool ok; auto url = QInputDialog::getText(this, "链接", "地址", QLineEdit::Normal, "https://", &ok); if (ok) focusedEditor()->wrap("[", "](" + url + ")"); });
    add(format, "图片…", QKeySequence("Ctrl+Shift+I"), [this] { image(); });
    auto paragraph = menuBar()->addMenu("段落(&P)");
    for (int i = 1; i <= 6; ++i) add(paragraph,QString("%1 级标题").arg(i),QKeySequence("Ctrl+"+QString::number(i)),[this,i]{focusedEditor()->heading(i);});
    add(paragraph,"普通段落",QKeySequence("Ctrl+0"),[this]{focusedEditor()->heading(0);});add(paragraph,"缩进",QKeySequence("Ctrl+]"),[this]{focusedEditor()->prefix("    ");});add(paragraph,"减少缩进",QKeySequence("Ctrl+["),[this]{focusedEditor()->unindent();});
    add(paragraph, "无序列表", {}, [this] { focusedEditor()->prefix("- "); }); add(paragraph, "有序列表", {}, [this] { focusedEditor()->prefix("1. "); }); add(paragraph, "任务列表", {}, [this] { focusedEditor()->prefix("- [ ] "); }); add(paragraph, "引用", {}, [this] { focusedEditor()->prefix("> "); });
    add(paragraph, "代码块", QKeySequence("Ctrl+Shift+K"), [this] { focusedEditor()->wrap("```\n", "\n```\n"); }); add(paragraph, "分隔线", {}, [this] { editor->insertRule(); }); add(paragraph, "插入 / 编辑表格…", QKeySequence("Ctrl+Alt+T"), [this] { tableDialog(); });
    auto view = menuBar()->addMenu("显示(&V)"); modeActions = new QActionGroup(this); modeActions->setExclusive(true);
    QStringList modes{"live", "split", "source", "reading"}, names{"实时编辑", "源码与预览", "仅源码", "阅读模式"};
    for (int i = 0; i < modes.size(); ++i) { auto action = add(view, names[i], QKeySequence("Ctrl+Alt+" + QString::number(i + 1)), [this, mode = modes[i]] { setMode(mode); }); action->setCheckable(true); action->setData(modes[i]); modeActions->addAction(action); }
    view->addSeparator(); view->addAction(outlineDock->toggleViewAction()); view->addAction(folderDock->toggleViewAction());
    add(format, "高亮", {}, [this] { focusedEditor()->wrap("=="); }); add(format, "下划线", QKeySequence("Ctrl+U"), [this] { focusedEditor()->wrap("<u>", "</u>"); }); add(format, "上标", {}, [this] { focusedEditor()->wrap("^"); }); add(format, "下标", {}, [this] { focusedEditor()->wrap("~"); });
    add(format, "图片设置…", {}, [this] { services->inspectImage(); }); add(format, "上传图片…", {}, [this] { services->uploadImage(); });
    add(paragraph, "目录", {}, [this] { focusedEditor()->insertPlainText("\n[TOC]\n"); }); add(paragraph, "数学公式块", {}, [this] { focusedEditor()->wrap("$$\n", "\n$$\n"); }); add(paragraph, "脚注", {}, [this] { focusedEditor()->insertPlainText("[^1]\n\n[^1]: "); });
    for (auto format : {"rtf","docx","epub","odt","rst","mediawiki","latex"}) add(exports,QString(format).toUpper(),{},[this,format] { services->exportFormat(format); });
    add(file,"导入 Word / RTF / EPUB…",{},[this] { services->importDocument(); });
    add(view,"专注模式",{},[this] { focusMode=!focusMode;QMetaObject::invokeMethod(editor,"cursorPositionChanged"); })->setCheckable(true);add(view,"打字机模式",{},[this] { typewriterMode=!typewriterMode;if(typewriterMode)editor->centerCursor(); })->setCheckable(true);
    add(view,"导入主题…",{},[this] { services->importTheme(); });add(view,"导出主题模板…",{},[this] { services->exportTheme(); });
    auto help = menuBar()->addMenu("帮助(&H)"); add(help, "中文使用帮助", QKeySequence("F1"), [this] { showHelp(); });
    add(help, "关于墨笺", {}, [this] { QMessageBox::about(this, "关于墨笺", "墨笺 Linux 1.0.0\n原生 C++ / Qt 6 Markdown 编辑器\n构建时间：" MOJIAN_BUILD_TIME "\n\nCtrl+S 保存，F1 查看完整中文帮助。"); });
    add(help,"第三方许可证",{},[this] { QFile file(":/mojian/ThirdPartyNotices.txt"); if(file.open(QIODevice::ReadOnly)){auto dialog=new QDialog(this);dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setWindowTitle("第三方许可证");dialog->resize(850,650);auto layout=new QVBoxLayout(dialog);auto view=new QPlainTextEdit;view->setReadOnly(true);view->setPlainText(QString::fromUtf8(file.readAll()));layout->addWidget(view);dialog->show();} });
    auto toolbar = addToolBar("文档"); toolbar->setObjectName("documentToolbar"); toolbar->addAction(newAction); toolbar->addAction(openAction); toolbar->addAction(saveAction); toolbar->addSeparator();
}
} // namespace Mojian
