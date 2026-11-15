#include "applocale.h"
//----------------------------------------------
#include "appsettings.h"
//----------------------------------------------
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

//----------------------------------------------

static const char *kKnownLanguages[] = { "en", "ru", "zh" };

//----------------------------------------------

void IniSourceTranslator::setMap(const QHash<QString, QString> &map)
{
    _map = map;
}

//----------------------------------------------

bool IniSourceTranslator::isEmpty() const
{
    // Without false, Qt treats the translator as empty and never calls translate().
    return _map.isEmpty();
}

//----------------------------------------------

QString IniSourceTranslator::translate(const char *context, const char *sourceText,
                                         const char *disambiguation, int n) const
{
    Q_UNUSED(context);
    Q_UNUSED(disambiguation);
    Q_UNUSED(n);
    if (!sourceText)
        return QString();
    const QString source = QString::fromUtf8(sourceText);
    const QHash<QString, QString>::const_iterator it = _map.constFind(source);
    if (it != _map.constEnd() && !it.value().isEmpty())
        return it.value();
    return QString();
}

//----------------------------------------------

AppLocale::AppLocale(QObject *parent)
    : QObject(parent)
    , _language(QString::fromUtf8("en"))
    , _legacyInstalled(false)
{
}

//----------------------------------------------

AppLocale *AppLocale::instance()
{
    static AppLocale *locale = 0;
    if (!locale)
        locale = new AppLocale(QCoreApplication::instance());
    return locale;
}

//----------------------------------------------

QString AppLocale::stringsRoot()
{
    return AppSettings::configDir() + QString::fromUtf8("/strings");
}

//----------------------------------------------

QString AppLocale::normalizeLanguageCode(QString code)
{
    code = code.trimmed().toLower();
    if (code == QString::fromUtf8("zh-cn") || code == QString::fromUtf8("zh_tw"))
        code = QString::fromUtf8("zh");
    if (code.isEmpty())
        code = QString::fromUtf8("en");
    return code;
}

//----------------------------------------------

QString AppLocale::langDir(QString code) const
{
    return stringsRoot() + QChar('/') + normalizeLanguageCode(code);
}

//----------------------------------------------

QStringList AppLocale::availableLanguages() const
{
    QStringList codes;
    for (int i = 0; i < 3; ++i)
        codes.append(QString::fromUtf8(kKnownLanguages[i]));
    QDir root(stringsRoot());
    if (root.exists())
    {
        const QStringList entries = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        foreach (const QString &entry, entries)
        {
            const QString norm = normalizeLanguageCode(entry);
            if (!codes.contains(norm))
                codes.append(norm);
        }
    }
    return codes;
}

//----------------------------------------------

QString AppLocale::language() const
{
    return _language;
}

//----------------------------------------------

QString AppLocale::languageDisplayName(QString code) const
{
    code = normalizeLanguageCode(code);
    if (code == QString::fromUtf8("ru"))
        return QString::fromUtf8("\xD0\xA0\xD1\x83\xD1\x81\xD1\x81\xD0\xBA\xD0\xB8\xD0\xB9");
    if (code == QString::fromUtf8("en"))
        return QString::fromUtf8("English");
    if (code == QString::fromUtf8("zh"))
        return QString::fromUtf8("\xE4\xB8\xAD\xE6\x96\x87");
    return code;
}

//----------------------------------------------

void AppLocale::loadGuiIni(const QString &dir)
{
    _gui.clear();
    const QString path = dir + QString::fromUtf8("/gui.ini");
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;
    // Not QSettings: it splits dots in keys (settings.hint.presets) into groups,
    // and L() starts returning the key itself instead of the text.
    QTextStream stream(&file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    stream.setCodec("UTF-8");
#endif
    bool inGui = false;
    while (!stream.atEnd())
    {
        QString line = stream.readLine();
        if (!line.isEmpty() && line.at(0) == QChar(0xFEFF))
            line.remove(0, 1);
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char(';'))
            || trimmed.startsWith(QLatin1Char('#')))
            continue;
        if (trimmed.startsWith(QLatin1Char('[')) && trimmed.endsWith(QLatin1Char(']')))
        {
            inGui = (trimmed == QString::fromUtf8("[gui]"));
            continue;
        }
        if (!inGui)
            continue;
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0)
            continue;
        const QString key = line.left(eq).trimmed();
        QString value = line.mid(eq + 1);
        if (value.endsWith(QLatin1Char('\r')))
            value.chop(1);
        if ((value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"')))
            || (value.startsWith(QLatin1Char('\'')) && value.endsWith(QLatin1Char('\''))))
            value = value.mid(1, value.size() - 2);
        if (!key.isEmpty())
            _gui.insert(key, value);
    }
}

//----------------------------------------------

void AppLocale::loadModelFiles(const QString &dir)
{
    _model.clear();
    const QString modelDir = dir + QString::fromUtf8("/model");
    QDir folder(modelDir);
    if (!folder.exists())
        return;
    const QStringList files = folder.entryList(QStringList() << QString::fromUtf8("*.txt"),
                                               QDir::Files);
    foreach (const QString &fileName, files)
    {
        QFile file(modelDir + QChar('/') + fileName);
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QString body = QString::fromUtf8(file.readAll()).trimmed();
        const QString key = QFileInfo(fileName).completeBaseName();
        if (!key.isEmpty() && !body.isEmpty())
            _model.insert(key, body);
    }
}

//----------------------------------------------

void AppLocale::loadLegacyTranslator(const QString &dir)
{
    QHash<QString, QString> map;
    const QString path = dir + QString::fromUtf8("/gui_by_source.ini");
    if (!QFileInfo::exists(path))
    {
        _legacyTranslator.setMap(map);
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        _legacyTranslator.setMap(map);
        return;
    }
    QTextStream stream(&file);
    stream.setCodec("UTF-8");
    while (!stream.atEnd())
    {
        QString line = stream.readLine();
        if (line.endsWith(QLatin1Char('\r')))
            line.chop(1);
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#'))
            || trimmed.startsWith(QLatin1Char(';')))
            continue;
        const int sep = line.indexOf(QLatin1Char('|'));
        if (sep <= 0)
            continue;
        QString source = line.left(sep);
        QString translation = line.mid(sep + 1);
        // In the file, a newline is the two characters backslash-n, and a backslash is doubled.
        source.replace(QString::fromUtf8("\\\\"), QString::fromUtf8("\x01"));
        translation.replace(QString::fromUtf8("\\\\"), QString::fromUtf8("\x01"));
        source.replace(QString::fromUtf8("\\n"), QString::fromUtf8("\n"));
        translation.replace(QString::fromUtf8("\\n"), QString::fromUtf8("\n"));
        source.replace(QString::fromUtf8("\x01"), QString::fromUtf8("\\"));
        translation.replace(QString::fromUtf8("\x01"), QString::fromUtf8("\\"));
        // Spaces at the edges are part of the tr() key.
        if (!source.isEmpty() && !translation.isEmpty())
            map.insert(source, translation);
    }
    _legacyTranslator.setMap(map);
}

//----------------------------------------------

void AppLocale::installLegacyTranslator()
{
    QCoreApplication *app = QCoreApplication::instance();
    if (!app)
        return;
    if (_legacyInstalled)
    {
        app->removeTranslator(&_legacyTranslator);
        _legacyInstalled = false;
    }
    if (_language != QString::fromUtf8("ru"))
    {
        app->installTranslator(&_legacyTranslator);
        _legacyInstalled = true;
    }
    // Do not broadcast LanguageChange to every widget: chat bubbles are widgets
    // too, and a full walk recalculates the whole feed. Texts are
    // updated by retranslateUi on the languageChanged signal.
}

//----------------------------------------------

void AppLocale::reload()
{
    const QString dir = langDir(_language);
    loadGuiIni(dir);
    loadModelFiles(dir);
    loadLegacyTranslator(dir);
    installLegacyTranslator();
}

//----------------------------------------------

bool AppLocale::setLanguage(QString code)
{
    code = normalizeLanguageCode(code);
    QStringList codes = availableLanguages();
    if (!codes.contains(code))
        code = QString::fromUtf8("en");
    const bool changed = (code != _language);
    _language = code;
    reload();
    AppSettings::instance()->persistUiLanguage(code);
    if (changed)
        emit languageChanged(code);
    return true;
}

//----------------------------------------------

QString AppLocale::text(const QString &key, const QString &fallback) const
{
    const QHash<QString, QString>::const_iterator it = _gui.constFind(key);
    if (it != _gui.constEnd() && !it.value().isEmpty())
        return it.value();
    if (!fallback.isEmpty())
        return fallback;
    return key;
}

//----------------------------------------------

QString AppLocale::model(const QString &key, const QString &fallback) const
{
    const QHash<QString, QString>::const_iterator it = _model.constFind(key);
    if (it != _model.constEnd() && !it.value().isEmpty())
        return it.value();
    if (!fallback.isEmpty())
        return fallback;
    return key;
}
