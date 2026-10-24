#pragma once
#include <QtWidgets>
#include <QtNetwork>
namespace Mojian {
class Window;
class Services : public QObject {
public:
    explicit Services(Window *window);
    Window *window;
    QNetworkAccessManager network;
    QPointer<QNetworkReply> upload;
    std::function<QString()> tokenProvider;
    void settings();
    void uploadImage();
    void exportFormat(const QString &format);
    void importDocument();
    void inspectImage();
    void applyTheme();
    void importTheme();
    void exportTheme();
    static bool validateTheme(const QJsonObject &object, QString *error = nullptr);
    static QString rtf(QTextDocument *document);
    static QString pandocExecutable();
    static QString portableHtml(QTextDocument *document);
    static bool pdfBookmarks(const QString &path,const QList<QPair<QString,int>> &headings,const QList<int> &levels = {});
    static QString credential(bool save = false, const QString &token = QString());
    static QString responseUrl(const QByteArray &data, const QString &path);
    static QUrl uploadUrl(const QString &text);
    static void localize(QWidget *widget);
    static void installLocalization(QApplication *application);
private:
    void convert(const QString &input, const QString &destination, const QString &format, bool importing, QSharedPointer<QTemporaryDir> temporary = {});
};
}
