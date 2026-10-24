#pragma once
#include <QtWidgets>
#include "markdown.h"
namespace Mojian {
class Editor;
struct TableSpan { int row=0,column=0,rows=1,columns=1; bool contains(int r,int c)const{return r>=row&&r<row+rows&&c>=column&&c<column+columns;} };
struct TableData {
    QList<QStringList> rows; QStringList alignment; QList<TableSpan> spans;
    static TableData parse(const QString &source);
    QString source() const;
    bool merge(int firstRow,int firstColumn,int lastRow,int lastColumn);
    void split(int row,int column);
    bool moveRow(int from,int to);
    bool moveColumn(int from,int to);
};
class TableWidget:public QWidget {
    Q_OBJECT
public:
    explicit TableWidget(TableData data,QWidget *parent=nullptr);
    QTableWidget *table;
    TableData data;
    bool updating=false;
    void rebuild();
    void commit();
signals:
    void changed(const QString &source);
    void finished();
protected:
    bool eventFilter(QObject *watched,QEvent *event)override;
};
struct ListDecoration { int start=0,length=0,line=0;QString marker;bool task=false,checked=false; };
struct InlineImage { int start=0,length=0;QImage image;bool formula=true; };
struct LiveStyle { int start=0,length=0; QTextCharFormat format; };
struct Projection { int start=0,end=0,firstLine=0,lastLine=0;QString type,source;QImage image;QSharedPointer<ImageDocument> document;QPointer<QWidget> widget;int height=0; };
class LiveController:public QObject {
    Q_OBJECT
public:
    explicit LiveController(Editor *editor);
    Editor *editor;
    QTimer timer;
    QList<Projection> projections;
    QMap<int,int> reserve;
    QList<QPair<int,int>> hidden;
    QMap<int,QList<LiveStyle>> styles;
    QList<InlineImage> inlineImages;
    QList<ListDecoration> decorations;
    QMap<int,QList<ListDecoration>> decorationsByBlock;
    QMap<int,int> quotePrefixes;
    QSet<int> thematicBlocks;
    QMap<int,QList<InlineImage>> inlineByBlock;
    QString lastAfter;
    int lastStart=-1;
    bool applying=false;
    QSharedPointer<const MdSnapshot> snapshot;
    int revision=-1;
    void schedule();
    void refresh();
    void restore();
    void paint(QPainter &painter);
    void positionWidgets();
    void commit(int start,int end,const QString &replacement);
    void activateBlock(Projection &projection,const MdNode &node,int sourceCaret = -1);
protected:
    bool eventFilter(QObject *object,QEvent *event)override;
};
}
