#pragma once
#include <QtWidgets>
#include <QtNetwork>
namespace Mojian {
struct MdNode {
    QString type, literal, url, title, info;
    int start = 0, end = 0, line = 0, heading = 0, listStart = 1,displayWidth = 0;
    bool ordered = false, checked = false, tight = false;
    QList<int> alignment;
    QList<MdNode> children;
};
struct MdSnapshot {
    QString source, dialect,referenceDefinitions;
    MdNode root;
};
class Markdown {
public:
    static QSharedPointer<const MdSnapshot> parse(const QString &source, const QString &dialect, bool forceFull = false);
    static QString html(const QString &source, const QString &dialect, const QString &extensions = QString());
    static void render(QTextDocument *document, const QString &source, const QUrl &base, qreal width, const QString &dialect, bool paper = false);
    static QList<MdNode> descendants(const MdNode &node);
    static int parseCount;
    static int lastParsedUnits;
    static int reusedNodes;
};
class ImageDocument : public QTextDocument {
    Q_OBJECT
public:
    explicit ImageDocument(QObject *parent = nullptr);
    QNetworkAccessManager manager;
    QSet<QUrl> pending;
protected:
    QVariant loadResource(int type, const QUrl &name) override;
};
}
