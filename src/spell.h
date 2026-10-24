#pragma once
#include <QtCore>
namespace Mojian {
class Speller {
public:
    static Speller &instance();
    bool available() const;
    bool correct(const QString &word);
    QStringList suggestions(const QString &word);
    void remember(const QString &word);
    ~Speller();
private:
    Speller();
    QLibrary library;
    QTemporaryDir dictionary;
    void *handle=nullptr;
    using Create=void *(*)(const char *,const char *);
    using Destroy=void (*)(void *);
    using Spell=int (*)(void *,const char *);
    using Suggest=int (*)(void *,char ***,const char *);
    using FreeList=void (*)(void *,char ***,int);
    Destroy destroy=nullptr;Spell spell=nullptr;Suggest suggest=nullptr;FreeList freeList=nullptr;
    QHash<QString,bool> cache;
};
}
