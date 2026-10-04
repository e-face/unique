#ifndef APPLOCALE_H
#define APPLOCALE_H

//----------------------------------------------
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTranslator>

//----------------------------------------------

/**
 * @brief Перевод tr(): ключ — исходная русская строка из исходников.
 */
class IniSourceTranslator : public QTranslator
{
    Q_OBJECT
public:
    void setMap(const QHash<QString, QString> &map);
    virtual bool isEmpty() const;
    virtual QString translate(const char *context, const char *sourceText,
                              const char *disambiguation, int n) const;

private:
    QHash<QString, QString> _map;
};

//----------------------------------------------

/**
 * @brief Локализация GUI (gui.ini) и текстов промптов модели.
 */
class AppLocale : public QObject
{
    Q_OBJECT
public:
    static AppLocale *instance();
    static QString stringsRoot();

    QStringList availableLanguages() const;
    QString language() const;
    QString languageDisplayName(QString code) const;
    bool setLanguage(QString code);
    QString text(const QString &key, const QString &fallback = QString()) const;
    QString model(const QString &key, const QString &fallback = QString()) const;
    void reload();

signals:
    void languageChanged(const QString &code);

private:
    explicit AppLocale(QObject *parent = 0);
    QString langDir(QString code) const;
    void loadGuiIni(const QString &dir);
    void loadModelFiles(const QString &dir);
    void loadLegacyTranslator(const QString &dir);
    void installLegacyTranslator();
    static QString normalizeLanguageCode(QString code);

    QString                 _language;
    QHash<QString, QString> _gui;
    QHash<QString, QString> _model;
    IniSourceTranslator     _legacyTranslator;
    bool                    _legacyInstalled;
};

//----------------------------------------------

inline QString L(const char *key)
{
    return AppLocale::instance()->text(QString::fromUtf8(key));
}

inline QString LM(const char *key)
{
    return AppLocale::instance()->model(QString::fromUtf8(key));
}

#endif // APPLOCALE_H
