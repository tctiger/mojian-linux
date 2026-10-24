#include "markdown.h"
#include "media.h"
#include "live.h"
extern "C" {
#include "MojianMarkdown.h"
}
namespace Mojian {
int Markdown::parseCount = 0;
int Markdown::lastParsedUnits = 0;
int Markdown::reusedNodes = 0;
static QString prepare(QString source, const QString &dialect, bool documentStart = true) {
    if (dialect != "extended") return source;
    auto lines = source.split('\n'); bool math = false, fenced = false; QChar marker; int length = 0;
    for (int i = 0; i < lines.size(); ++i) {
        auto match = QRegularExpression("^ {0,3}(`{3,}|~{3,})(.*)$").match(lines[i]);
        if (match.hasMatch() && !math) { if (!fenced) { fenced = true; marker = match.captured(1).front(); length = match.captured(1).size(); } else if (match.captured(1).front() == marker && match.captured(1).size() >= length && match.captured(2).trimmed().isEmpty()) fenced = false; continue; }
        if (fenced) continue;
        if (documentStart && i == 0 && lines[i] == "---") { int end = i + 1; while (end < lines.size() && lines[end] != "---" && lines[end] != "...") ++end; if (end < lines.size()) { lines[i] = "```mojian-frontmatter"; lines[end] = "```"; i = end; continue; } }
        if (lines[i].trimmed() == "$$") { lines[i] = math ? "```" : "```mojian-math"; math = !math; }
    }
    return lines.join('\n');
}
static MdNode parseRaw(const QString &source,const QString &dialect,bool documentStart=true,QString *references=nullptr) {
    QString prepared = prepare(source, dialect, documentStart); auto bytes = prepared.toUtf8(); auto document = mj_parse(bytes.constData(), bytes.size(), dialect == "commonmark", dialect == "extended" ? "table,strikethrough,autolink,tagfilter,tasklist,footnotes" : nullptr);
    ++Markdown::parseCount; Markdown::lastParsedUnits = source.size(); auto rawLines = source.split('\n'); QList<int> offsets; int offset = 0; for (auto line : rawLines) { offsets << offset; offset += line.size() + 1; }
    QList<QVector<int>> byteColumns;
    for(const auto &line:rawLines){QVector<int> columns{0};int index=0;while(index<line.size()){int units=line[index].isHighSurrogate()&&index+1<line.size()&&line[index+1].isLowSurrogate()?2:1;int bytes=line.mid(index,units).toUtf8().size();for(int j=1;j<=bytes;++j)columns<<int(j==bytes?index+units:index);index+=units;}byteColumns<<columns;}
    auto position = [&](int line, int column, bool end) { int index = std::clamp(line - 1, 0, int(rawLines.size()) - 1); int byte = std::clamp(column - (end ? 0 : 1), 0, int(byteColumns[index].size())-1);return offsets[index]+byteColumns[index][byte]; };
    std::function<MdNode(MJNode *, int)> visit = [&](MJNode *raw, int depth) {
        MdNode node; node.type = QString::fromUtf8(mj_type(document, raw)); node.literal = QString::fromUtf8(mj_literal(document, raw)); node.url = QString::fromUtf8(mj_url(document, raw)); node.title = QString::fromUtf8(mj_title(document, raw)); node.info = QString::fromUtf8(mj_fence_info(document, raw));
        node.line = mj_start_line(document, raw); node.start = position(node.line, mj_start_column(document, raw), false); node.end = position(mj_end_line(document, raw), mj_end_column(document, raw), true); if (node.type.startsWith("footnote")) { auto label=QRegularExpression("\\[\\^([^\\]]+)\\]").match(source.mid(node.start,node.end-node.start));if(label.hasMatch())node.literal=label.captured(1); }
        if(node.type=="image"&&dialect=="extended"){auto metadata=QRegularExpression("^\\{width=(\\d+)\\}").match(source.mid(node.end,40));if(metadata.hasMatch())node.displayWidth=metadata.captured(1).toInt();}
        node.heading = mj_heading_level(document, raw); node.ordered = mj_list_type(document, raw) == 2; node.listStart = mj_list_start(document, raw); node.tight = mj_list_tight(document, raw); node.checked = mj_checked(document, raw);
        int columns = mj_columns(document, raw); for (int c = 0; c < columns; ++c) node.alignment << mj_alignment(document, raw, c);
        if (depth < 256) for (auto child = mj_first_child(document, raw); child; child = mj_next(document, child)) node.children << visit(child, depth + 1);
        else { node.type = "source"; node.literal = source.mid(node.start, node.end - node.start); }
        for(int i=0;i<node.children.size();++i){auto &child=node.children[i];if((child.type=="softbreak"||child.type=="linebreak")&&child.line<=0){int previous=i>0?node.children[i-1].end:node.start;int newline=source.indexOf('\n',previous);if(newline>=0&&newline<=node.end){child.start=newline;child.end=newline+1;child.line=std::upper_bound(offsets.cbegin(),offsets.cend(),newline)-offsets.cbegin();}}}
        return node;
    };
    auto root=visit(mj_root(document),0);if(references){references->clear();QSet<QString> seen;QList<void *> entries;for(auto entry=mj_first_reference(document);entry;entry=mj_next_reference(entry))entries.prepend(entry);for(auto entry:entries){QString label=QString::fromUtf8(mj_reference_label(entry));if(seen.contains(label))continue;seen.insert(label);QString url=QString::fromLatin1(QUrl(QString::fromUtf8(mj_reference_url(document,entry))).toEncoded(QUrl::FullyEncoded));QString title=QString::fromUtf8(mj_reference_title(document,entry));title.replace('\\',"\\\\").replace('\"',"\\\"").replace('\n',' ');*references+="["+label+"]: <"+url+">"+(title.isEmpty()?QString():" \""+title+"\"")+"\n";}}mj_document_free(document);return root;
}
static void shifted(MdNode &node,int characters,int lines){node.start+=characters;node.end+=characters;node.line+=lines;for(auto &child:node.children)shifted(child,characters,lines);}
static bool closedFences(const QString &piece){QChar marker;int length=0;bool math=false;for(auto line:piece.split('\n')){if(line.trimmed()=="$$"){if(marker.isNull())math=!math;continue;}auto match=QRegularExpression("^ {0,3}(`{3,}|~{3,})(.*)$").match(line);if(!match.hasMatch()||math)continue;if(marker.isNull()){marker=match.captured(1).front();length=match.captured(1).size();}else if(marker==match.captured(1).front()&&match.captured(1).size()>=length&&match.captured(2).trimmed().isEmpty())marker=QChar();}return marker.isNull()&&!math;}
QSharedPointer<const MdSnapshot> Markdown::parse(const QString &source,const QString &dialect,bool forceFull){
    static QMutex mutex;static QCache<QString,QSharedPointer<const MdSnapshot>> cache(64000);static QSharedPointer<const MdSnapshot> previous;
    QMutexLocker lock(&mutex);if(forceFull){auto value=QSharedPointer<MdSnapshot>::create();value->source=source;value->dialect=dialect;value->root=parseRaw(source,dialect,true,&value->referenceDefinitions);return value;}QString key=dialect+':'+QString::fromLatin1(QCryptographicHash::hash(source.toUtf8(),QCryptographicHash::Sha256).toHex());if(auto stored=cache.object(key)){if(source.size()>4096&&dialect=="extended")previous=*stored;return *stored;}
    auto snapshot=QSharedPointer<MdSnapshot>::create();snapshot->source=source;snapshot->dialect=dialect;bool incremental=false;reusedNodes=0;
    if(dialect=="extended"&&source.size()>4096&&previous&&previous->dialect==dialect&&!source.startsWith("---")&&!source.contains(QRegularExpression("(?m)^ {0,3}\\[[^\n]+\\]:"))&&!source.contains("[^")&&!previous->source.contains("[^")&&!previous->source.contains(QRegularExpression("(?m)^ {0,3}\\[[^\n]+\\]:"))){
        const auto &old=previous->source;int prefix=0,suffix=0;while(prefix<std::min(old.size(),source.size())&&old[prefix]==source[prefix])++prefix;while(suffix<std::min(old.size(),source.size())-prefix&&old[old.size()-suffix-1]==source[source.size()-suffix-1])++suffix;
        const auto &nodes=previous->root.children;int first=0,last=nodes.size()-1;while(first<nodes.size()&&nodes[first].end<prefix)++first;while(last>=0&&nodes[last].start>old.size()-suffix)--last;first=std::max(0,first-1);last=std::min(int(nodes.size())-1,last+1);
        if(!nodes.isEmpty()&&prefix+suffix>std::min(old.size(),source.size())*3/4&&first<=last){int start=old.lastIndexOf('\n',std::max(0,nodes[first].start-1))+1;int oldEnd=last+1<nodes.size()?old.lastIndexOf('\n',std::max(0,nodes[last+1].start-1))+1:old.size();int delta=source.size()-old.size(),end=oldEnd+delta;
            if(end>=start&&end<=source.size()){QString piece=source.mid(start,end-start);bool safe=!piece.contains('<')&&(end==source.size()||piece.endsWith("\n\n"))&&closedFences(piece);if(safe){auto partial=parseRaw(piece,dialect,start==0);int lineStart=source.left(start).count('\n');for(auto &node:partial.children)shifted(node,start,lineStart);snapshot->root=previous->root;snapshot->root.children.clear();for(int i=0;i<first;++i)snapshot->root.children<<nodes[i];snapshot->root.children<<partial.children;int lineDelta=source.count('\n')-old.count('\n');for(int i=last+1;i<nodes.size();++i){auto node=nodes[i];shifted(node,delta,lineDelta);snapshot->root.children<<node;}snapshot->root.end=source.size()-(source.endsWith('\n')?1:0);reusedNodes=first+nodes.size()-last-1;incremental=true;}}
        }
    }
    if(!incremental)snapshot->root=parseRaw(source,dialect,true,&snapshot->referenceDefinitions);QSharedPointer<const MdSnapshot> result=snapshot;cache.insert(key,new QSharedPointer<const MdSnapshot>(result),std::max(1,int((source.size()*2+descendants(snapshot->root).size()*256)/1024)));if(source.size()>4096&&dialect=="extended")previous=result;return result;
}
QString Markdown::html(const QString &source, const QString &dialect, const QString &extensions) {
    auto bytes = source.toUtf8(); auto names = extensions.toUtf8(); auto document = mj_parse(bytes.constData(), bytes.size(), dialect == "commonmark", extensions.isNull() ? (dialect=="extended"?"table,strikethrough,autolink,tagfilter,tasklist,footnotes":nullptr) : names.constData());
    char *raw = mj_html(document, 1); QString result = QString::fromUtf8(raw); mj_string_free(raw); mj_document_free(document); return result;
}
QList<MdNode> Markdown::descendants(const MdNode &node) { QList<MdNode> result; for (const auto &child : node.children) { result << child; result << descendants(child); } return result; }
void Markdown::render(QTextDocument *document, const QString &source, const QUrl &base, qreal width, const QString &dialect, bool paper) {
    auto snapshot = parse(source, dialect); document->setBaseUrl(base); bool extended = dialect == "extended";
    QMap<QUrl, QImage> images; int imageIndex = 0;
    auto image = [&](QImage value) { if (value.isNull()) return QString(); QString name = "mojian-generated:" + QString::number(++imageIndex); images[QUrl(name)] = value; qreal scale = std::min<qreal>(1, std::max<qreal>(100, width) / value.width()); return QString("<img src=\"%1\" width=\"%2\" height=\"%3\" />").arg(name).arg(value.width() * scale).arg(value.height() * scale); };
    QColor foreground = paper ? QColor(Qt::black) : QApplication::palette().color(QPalette::Text);
    auto inlineExtensions=[](const QString &text){return text.toHtmlEscaped();};
    std::function<QString(const MdNode &)> content;
    std::function<QString(const QString &)> richInline;
    auto children = [&](const MdNode &node) { QString result; for (int i=0;i<node.children.size();++i){auto child=node.children[i];if(extended&&i>0&&node.children[i-1].type=="image"&&child.type=="text")child.literal.remove(QRegularExpression("^\\{width=\\d+\\}"));result+=content(child);} return result; };
    content = [&](const MdNode &node) -> QString {
        QString type = node.type, anchor = "<a name=\"source-" + QString::number(node.start) + "\"></a>";
        if(type=="math_inline"){auto rendered=image(mathImage(node.literal,15,foreground));return rendered.isEmpty()?("$"+node.literal+"$").toHtmlEscaped():rendered;}
        if(type=="mark_inline"||type=="sup_inline"||type=="sub_inline"){QString body=node.children.isEmpty()?richInline(node.literal):children(node);return type=="mark_inline"?"<span style=\"background-color:#fff0a6\">"+body+"</span>":type=="sup_inline"?"<sup>"+body+"</sup>":"<sub>"+body+"</sub>";}
        if (type == "text") return inlineExtensions(node.literal);
        if (type == "softbreak") return "\n"; if (type == "linebreak") return "<br />";
        if (type == "code") return "<code>" + node.literal.toHtmlEscaped() + "</code>";
        if (type == "emph") return "<i>" + children(node) + "</i>";
        if (type == "strong") return "<b>" + children(node) + "</b>";
        if (type == "strikethrough") return "<s>" + children(node) + "</s>";
        if (type == "link") { QString url = node.url; if (url.startsWith("javascript:", Qt::CaseInsensitive)) url.clear(); return "<a href=\"" + url.toHtmlEscaped() + "\">" + children(node) + "</a>"; }
        if(type=="image"){QString path=node.url;QString description;for(auto child:descendants(node))if(child.type=="text"||child.type=="code")description+=child.literal;qreal desired=width;QUrl url=base.resolved(QUrl(path));if(url.isLocalFile()){QImageReader reader(url.toLocalFile());if(reader.size().width()>0)desired=std::min<qreal>(width,reader.size().width());}if(node.displayWidth>0)desired=std::min<qreal>(width,node.displayWidth);return "<img src=\""+path.toHtmlEscaped()+"\" alt=\""+description.toHtmlEscaped()+"\" width=\""+QString::number(std::max<qreal>(1,desired))+"\" />";}

        if (type == "thematic_break") return anchor + "<hr />";
        if (type == "heading") return anchor + QString("<h%1>%2</h%1>").arg(node.heading).arg(children(node));
        if (type == "paragraph") {
            QString text = children(node);
            if (extended && text.trimmed().compare("[TOC]", Qt::CaseInsensitive) == 0) { QString toc; for (auto heading : descendants(snapshot->root)) if (heading.type == "heading") toc += QString("<p style=\"margin-left:%1px\"><a href=\"#source-%2\">%3</a></p>").arg((heading.heading - 1) * 16).arg(heading.start).arg(children(heading)); return toc; }
            return anchor + "<p>" + text + "</p>";
        }
        if (type == "code_block") {
            QImage rendered;
            if (extended && node.info == "mermaid") rendered = diagramImage(node.literal);
            if (extended && node.info == "mojian-math") rendered = mathImage(node.literal, 21, foreground);
            if (!rendered.isNull()) return anchor + "<p>" + image(rendered) + "</p>";
            return anchor + "<pre style=\"background-color:" + (!paper && QApplication::palette().color(QPalette::Base).lightness() < 100 ? QString("#30343d") : QString("#f1f3f7")) + "\"><code>" + [&]{QString result;int offset=0;auto matches=QRegularExpression(R"((//[^
]*|\#[^
]*|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|\b(?:let|var|func|class|struct|enum|if|else|for|while|return|def|import|from|print|const|function|public|private|true|false|null|nil|int|double|void)\b|\b\d+(?:\.\d+)?\b))").globalMatch(node.literal);while(matches.hasNext()){auto m=matches.next();result+=node.literal.mid(offset,m.capturedStart()-offset).toHtmlEscaped();QString value=m.captured();QString color=value.startsWith("//")||value.startsWith('#')?"#789579":value.startsWith('\"')||value.startsWith('\'')?"#b48b5d":"#8b79bd";result+="<span style=\"color:"+color+"\">"+value.toHtmlEscaped()+"</span>";offset=m.capturedEnd();}return result+node.literal.mid(offset).toHtmlEscaped();}() + "</code></pre>";
        }
        if (type == "block_quote") { QString body = children(node); if (extended) body.replace(QRegularExpression("\\[!(NOTE|TIP|IMPORTANT|WARNING|CAUTION)\\]"), "<b>\\1</b><br>"); return anchor + "<blockquote>" + body + "</blockquote>"; }
        if (type == "list") return (node.ordered ? QString("<ol start=\"%1\">").arg(node.listStart) : QString("<ul>")) + children(node) + (node.ordered ? "</ol>" : "</ul>");
        if (type == "item" || type == "tasklist") return "<li>" + (type == "tasklist" ? QString("<a href=\"mojian-task:%1\">%2</a> ").arg(node.start).arg(node.checked ? "☑" : "☐") : QString()) + children(node) + "</li>";
        if (type == "table") {
            QString table = "<table border=\"1\" cellspacing=\"0\" cellpadding=\"7\" width=\"100%\">"; int rowIndex = 0;
            for (auto row : node.children) { table += "<tr>"; for (int col = 0; col < row.children.size(); ++col) { int alignment = node.alignment.value(col); QString align = alignment == 'c' ? "center" : alignment == 'r' ? "right" : "left"; QString tag = rowIndex == 0 ? "th" : "td"; table += "<" + tag + " align=\"" + align + "\">" + children(row.children[col]) + "</" + tag + ">"; } table += "</tr>"; ++rowIndex; } return anchor + table + "</table>";
        }
        if (type == "html_inline" || type == "html_block") {
            // Qt's limited rich-text reader never executes JS. Preserve unknown tags as source.
            QString raw = node.literal;
            if(extended&&raw.trimmed().startsWith("<table",Qt::CaseInsensitive)){auto table=TableData::parse(raw);if(!table.rows.isEmpty()){QString result="<table border=\"1\" cellspacing=\"0\" cellpadding=\"7\" width=\"100%\">";for(int r=0;r<table.rows.size();++r){result+="<tr>";for(int c=0;c<table.rows[r].size();++c){bool covered=false;TableSpan anchor{r,c,1,1};for(auto span:table.spans)if(span.contains(r,c)){covered=span.row!=r||span.column!=c;if(!covered)anchor=span;break;}if(covered)continue;QString tag=r==0?"th":"td",alignment=table.alignment.value(c)==":---:"?"center":table.alignment.value(c)=="---:"?"right":"left";result+="<"+tag+" align=\""+alignment+"\" rowspan=\""+QString::number(anchor.rows)+"\" colspan=\""+QString::number(anchor.columns)+"\">"+richInline(table.rows[r][c])+"</"+tag+">";}result+="</tr>";}return result+"</table>";}}

            static QRegularExpression safe(R"(^\s*</?(?:u|s|del|sub|sup|b|strong|i|em|br|p|div|span|blockquote|h[1-6]|pre|code|table|thead|tbody|tr|th|td|hr)(?:\s[^>]*)?/?>)", QRegularExpression::CaseInsensitiveOption);
            if (safe.match(raw).hasMatch() && !raw.contains(QRegularExpression("<(?:script|iframe|object|embed)", QRegularExpression::CaseInsensitiveOption))) { raw.remove(QRegularExpression(R"(\s+on\w+\s*=\s*(?:"[^"]*"|'[^']*'|[^ >]+))", QRegularExpression::CaseInsensitiveOption)); return raw; }
            return raw.toHtmlEscaped();
        }
        if (type == "footnote_reference") return "<sup><a href=\"#footnote-" + node.literal.toHtmlEscaped() + "\">[" + node.literal.toHtmlEscaped() + "]</a></sup>";
        if (type == "footnote_definition") return "<div><a name=\"footnote-" + node.literal.toHtmlEscaped() + "\"></a>" + children(node) + "</div>";
        if (type == "source") return "<pre>" + node.literal.toHtmlEscaped() + "</pre>";
        return children(node);
    };
    int inlineDepth=0;
    richInline=[&](const QString &text){if(inlineDepth>=256)return text.toHtmlEscaped();QScopedValueRollback<int> depth(inlineDepth,inlineDepth+1);auto nested=parse("MJI "+text+"\n\n"+snapshot->referenceDefinitions,dialect);QString result;bool first=true;for(auto block:nested->root.children){for(auto node:block.children){if(first&&node.type=="text"&&node.literal.startsWith("MJI "))node.literal.remove(0,4);first=false;result+=content(node);}}result.remove(QRegularExpression("<a name=\"source-\\d+\"></a>"));return result;};
    QString html = "<html><body style=\"color:"+foreground.name()+"\">" + content(snapshot->root) + "</body></html>";
    document->setHtml(html); for (auto it = images.cbegin(); it != images.cend(); ++it) document->addResource(QTextDocument::ImageResource, it.key(), it.value());
    document->setTextWidth(width); document->markContentsDirty(0, document->characterCount());
    auto spacing = QSettings().value("lineSpacing", 4).toDouble(); QTextCursor cursor(document); cursor.select(QTextCursor::Document); QTextBlockFormat format; format.setLineHeight(100 + spacing * 3, QTextBlockFormat::ProportionalHeight); cursor.mergeBlockFormat(format);
}
ImageDocument::ImageDocument(QObject *parent) : QTextDocument(parent), manager(this) {}
namespace {
struct ImageWaiter{QPointer<QTextDocument> document;QUrl name;};
struct ImageStore{QCache<QUrl,QImage> images{48000};QHash<QUrl,QList<ImageWaiter>> pending;QHash<QUrl,qint64> failed;QSet<QUrl> inFlight;QNetworkAccessManager *manager=nullptr;};
ImageStore &imageStore(){static ImageStore store;if(!store.manager)store.manager=new QNetworkAccessManager(qApp);return store;}
}
QVariant ImageDocument::loadResource(int type,const QUrl &name){
    QUrl url=baseUrl().resolved(name);if(type!=QTextDocument::ImageResource||(url.scheme()!="https"&&url.scheme()!="http"))return QTextDocument::loadResource(type,name);auto &store=imageStore();if(auto image=store.images.object(url))return *image;
    auto &waiters=store.pending[url];waiters.removeIf([](const ImageWaiter &waiter){return waiter.document.isNull();});bool present=false;for(auto waiter:waiters)if(waiter.document==this&&waiter.name==name)present=true;if(!present)waiters<<ImageWaiter{this,name};
    if(store.inFlight.size()>=8&&!store.inFlight.contains(url)){QTimer::singleShot(250,this,[this,type,name]{auto value=loadResource(type,name);addResource(type,name,value);markContentsDirty(0,characterCount());});}else if(!store.inFlight.contains(url)&&(!store.failed.contains(url)||QDateTime::currentMSecsSinceEpoch()-store.failed[url]>60000)){
        store.inFlight.insert(url);QNetworkRequest request(url);request.setTransferTimeout(15000);request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::NoLessSafeRedirectPolicy);auto reply=store.manager->get(request);connect(reply,&QNetworkReply::downloadProgress,reply,[reply](qint64 bytes,qint64 total){if(bytes>25000000||total>25000000)reply->abort();});
        connect(reply,&QNetworkReply::finished,qApp,[reply,url]{auto &cache=imageStore();QImage image;if(reply->error()==QNetworkReply::NoError){auto bytes=reply->readAll();QBuffer buffer(&bytes);buffer.open(QIODevice::ReadOnly);QImageReader reader(&buffer);auto size=reader.size();if(size.width()>0&&size.height()>0&&qint64(size.width())*size.height()<=40000000)image=reader.read();}cache.inFlight.remove(url);auto waiters=cache.pending.take(url);if(image.isNull())cache.failed[url]=QDateTime::currentMSecsSinceEpoch();else{cache.images.insert(url,new QImage(image),std::max(1,int(image.sizeInBytes()/1024)));for(auto waiter:waiters)if(waiter.document){waiter.document->addResource(QTextDocument::ImageResource,waiter.name,image);waiter.document->addResource(QTextDocument::ImageResource,url,image);waiter.document->markContentsDirty(0,waiter.document->characterCount());waiter.document->documentLayout()->documentSize();}for(auto widget:QApplication::allWidgets())if(auto view=qobject_cast<QAbstractItemView *>(widget))view->viewport()->update();}reply->deleteLater();});
    }
    QImage placeholder(120,34,QImage::Format_ARGB32_Premultiplied);placeholder.fill(QColor("#e9ebf0"));return placeholder;
}
}
