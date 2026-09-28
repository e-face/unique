#include "knowledgestore.h"
//----------------------------------------------
#include "chatsession.h"
//----------------------------------------------
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegExp>
#include <QSaveFile>
#include <QUuid>
#include <QtAlgorithms>
#include <QtGlobal>

//----------------------------------------------

static const int    kMaxEntries        = 300;
static const int    kMaxLessonChars    = 2000;
static const int    kMaxTaskChars      = 600;
static const double kMergeThreshold    = 0.80;
/** Порог похожести задач для подсказки: ниже — шум, который только тратит токены. */
static const double kRecallThreshold   = 0.25;
static const int    kMinCommonTokens   = 2;
/** Сколько записей попадает в подсказку новому воркеру. */
static const int    kHintMaxEntries    = 2;
/** Суммарный размер подсказки (символов), включая заголовок. */
static const int    kHintMaxChars      = 1500;

//----------------------------------------------

KnowledgeStore::KnowledgeStore()
    : _dirtyUses(false)
{
}

//----------------------------------------------

KnowledgeStore *KnowledgeStore::instance()
{
    static KnowledgeStore store;
    return &store;
}

//----------------------------------------------

QString KnowledgeStore::filePath() const
{
    return ChatStore::storeDir() + QString::fromUtf8("/knowledge.json");
}

//----------------------------------------------

QString KnowledgeStore::newId()
{
    QString id = QUuid::createUuid().toString();
    if (id.startsWith(QChar('{')))
        id.remove(0, 1);
    if (id.endsWith(QChar('}')))
        id.chop(1);
    return id;
}

//----------------------------------------------

QString KnowledgeStore::clip(QString text, int maxChars)
{
    text = text.trimmed();
    if (text.size() <= maxChars)
        return text;
    return text.left(maxChars - 1) + QString::fromUtf8("…");
}

//----------------------------------------------

QSet<QString> KnowledgeStore::tokens(QString text)
{
    static QSet<QString> stop;
    if (stop.isEmpty())
    {
        const char *words[] = {
            "the", "and", "for", "with", "that", "this", "from", "into", "use", "using",
            "all", "any", "are", "not", "you", "your", "must", "should", "file", "files",
            "task", "make", "sure", "add", "new", "one", "two", "also", "then", "than",
            "will", "can", "может", "нужно", "надо", "чтобы", "это", "все", "для", "или",
            "при", "как", "так", "что", "его", "она", "они", "файл", "файлы", "задача",
            "сделать", "сделай", "добавить", "добавь", "создать", "создай", 0 };
        for (int i = 0; words[i]; ++i)
            stop.insert(QString::fromUtf8(words[i]));
    }

    QSet<QString> out;
    const QString lower = text.toLower();
    QRegExp re(QString::fromUtf8("[\\w\\.\\-/\\\\+#]+"));
    int pos = 0;
    while ((pos = re.indexIn(lower, pos)) != -1)
    {
        QString word = re.cap(0);
        pos += qMax(1, re.matchedLength());
        while (word.endsWith(QChar('.')) || word.endsWith(QChar(',')))
            word.chop(1);
        if (word.size() < 3)
            continue;
        if (stop.contains(word))
            continue;
        out.insert(word);
    }
    return out;
}

//----------------------------------------------

double KnowledgeStore::similarity(const QSet<QString> &a, const QSet<QString> &b)
{
    if (a.isEmpty() || b.isEmpty())
        return 0;
    int overlap = 0;
    foreach (const QString &token, a)
    {
        if (b.contains(token))
            ++overlap;
    }
    if (overlap < kMinCommonTokens)
        return 0;
    const int denom = a.size() + b.size() - overlap;
    if (denom <= 0)
        return 0;
    return double(overlap) / double(denom);
}

//----------------------------------------------

void KnowledgeStore::ensureLoaded()
{
    const QString dir = ChatStore::storeDir();
    if (dir == _loadedDir)
        return;
    if (!_loadedDir.isEmpty() && _dirtyUses)
        save();
    _loadedDir = dir;
    load();
}

//----------------------------------------------

void KnowledgeStore::load()
{
    _entries.clear();
    _dirtyUses = false;
    QFile file(filePath());
    if (!file.exists() || !file.open(QIODevice::ReadOnly))
        return;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject())
        return;
    const QJsonArray list = doc.object().value(QString::fromUtf8("entries")).toArray();
    for (int i = 0; i < list.size(); ++i)
    {
        const QJsonObject object = list.at(i).toObject();
        KnowledgeEntry entry;
        entry.id = object.value(QString::fromUtf8("id")).toString();
        entry.workerType = object.value(QString::fromUtf8("workerType")).toString();
        entry.task = object.value(QString::fromUtf8("task")).toString();
        entry.lessons = object.value(QString::fromUtf8("lessons")).toString();
        entry.created = qint64(object.value(QString::fromUtf8("created")).toDouble());
        entry.uses = object.value(QString::fromUtf8("uses")).toInt();
        entry.ok = object.value(QString::fromUtf8("ok")).toBool(true);
        if (entry.lessons.trimmed().isEmpty())
            continue;
        if (entry.id.isEmpty())
            entry.id = newId();
        _entries.append(entry);
    }
}

//----------------------------------------------

bool KnowledgeStore::save()
{
    const QString path = filePath();
    const QString dir = QFileInfo(path).absolutePath();
    if (!QDir().mkpath(dir))
        return false;

    QJsonArray list;
    foreach (const KnowledgeEntry &entry, _entries)
    {
        QJsonObject object;
        object.insert(QString::fromUtf8("id"), entry.id);
        object.insert(QString::fromUtf8("workerType"), entry.workerType);
        object.insert(QString::fromUtf8("task"), entry.task);
        object.insert(QString::fromUtf8("lessons"), entry.lessons);
        object.insert(QString::fromUtf8("created"), double(entry.created));
        object.insert(QString::fromUtf8("uses"), entry.uses);
        object.insert(QString::fromUtf8("ok"), entry.ok);
        list.append(object);
    }
    QJsonObject root;
    root.insert(QString::fromUtf8("version"), 1);
    root.insert(QString::fromUtf8("entries"), list);

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    if (!file.commit())
        return false;
    _dirtyUses = false;
    return true;
}

//----------------------------------------------

static bool olderAndLessUsed(const KnowledgeEntry &a, const KnowledgeEntry &b)
{
    if (a.uses != b.uses)
        return a.uses < b.uses;
    return a.created < b.created;
}

//----------------------------------------------

void KnowledgeStore::remember(WorkerType type, QString task, QString lessons, bool ok)
{
    const QString text = clip(lessons, kMaxLessonChars);
    if (text.size() < 20)
        return;
    ensureLoaded();

    const QString typeName = workerTypeToString(type);
    const QString taskText = clip(task, kMaxTaskChars);
    const QSet<QString> want = tokens(taskText);

    // Похожая запись того же типа: обновляем, сохраняя ещё не известные строки.
    int bestIndex = -1;
    double bestScore = 0;
    for (int i = 0; i < _entries.size(); ++i)
    {
        const KnowledgeEntry &entry = _entries.at(i);
        if (entry.workerType != typeName)
            continue;
        const double score = similarity(want, tokens(entry.task));
        if (score > bestScore)
        {
            bestScore = score;
            bestIndex = i;
        }
    }

    if (bestIndex >= 0 && bestScore >= kMergeThreshold)
    {
        KnowledgeEntry &entry = _entries[bestIndex];
        QString merged = text;
        foreach (const QString &line, entry.lessons.split(QChar('\n')))
        {
            const QString trimmed = line.trimmed();
            if (trimmed.isEmpty() || merged.contains(trimmed))
                continue;
            if (merged.size() + trimmed.size() + 1 > kMaxLessonChars)
                break;
            merged += QChar('\n') + trimmed;
        }
        entry.lessons = merged;
        entry.task = taskText;
        entry.created = QDateTime::currentMSecsSinceEpoch();
        entry.ok = ok;
        save();
        return;
    }

    KnowledgeEntry entry;
    entry.id = newId();
    entry.workerType = typeName;
    entry.task = taskText;
    entry.lessons = text;
    entry.created = QDateTime::currentMSecsSinceEpoch();
    entry.ok = ok;
    _entries.append(entry);

    if (_entries.size() > kMaxEntries)
    {
        qStableSort(_entries.begin(), _entries.end(), olderAndLessUsed);
        while (_entries.size() > kMaxEntries)
            _entries.removeFirst();
    }
    save();
}

//----------------------------------------------

struct KnowledgeMatch
{
    int    index;
    double score;
};

//----------------------------------------------

static bool betterMatch(const KnowledgeMatch &a, const KnowledgeMatch &b)
{
    return a.score > b.score;
}

//----------------------------------------------

QList<KnowledgeEntry> KnowledgeStore::recall(WorkerType type, QString task, int maxCount)
{
    ensureLoaded();
    QList<KnowledgeEntry> out;
    if (_entries.isEmpty() || maxCount <= 0)
        return out;

    const QString typeName = workerTypeToString(type);
    const QSet<QString> want = tokens(task);
    if (want.isEmpty())
        return out;

    QList<KnowledgeMatch> matches;
    for (int i = 0; i < _entries.size(); ++i)
    {
        const KnowledgeEntry &entry = _entries.at(i);
        double score = similarity(want, tokens(entry.task));
        // Уроки самой записи тоже могут упоминать нужные вещи (команды, файлы).
        score = qMax(score, 0.8 * similarity(want, tokens(entry.lessons)));
        if (score < kRecallThreshold)
            continue;
        if (entry.workerType == typeName)
            score += 0.15;
        if (!entry.ok)
            score -= 0.05;
        KnowledgeMatch match;
        match.index = i;
        match.score = score;
        matches.append(match);
    }
    qStableSort(matches.begin(), matches.end(), betterMatch);

    for (int i = 0; i < matches.size() && out.size() < maxCount; ++i)
    {
        KnowledgeEntry &entry = _entries[matches.at(i).index];
        ++entry.uses;
        _dirtyUses = true;
        out.append(entry);
    }
    return out;
}

//----------------------------------------------

QString KnowledgeStore::hintFor(WorkerType type, QString task)
{
    const QList<KnowledgeEntry> found = recall(type, task, kHintMaxEntries);
    if (found.isEmpty())
        return QString();

    // Подсказка попадает в первое сообщение каждого воркера, поэтому её
    // размер ограничен жёстко: лучшую запись даём полнее, остаток — по бюджету.
    QString text = QString::fromUtf8(
        "Experience of previous workers in this project on similar tasks "
        "(verified in practice; follow it unless it contradicts your assignment, "
        "and do not re-investigate what is already known here):");
    int n = 0;
    foreach (const KnowledgeEntry &entry, found)
    {
        const int budget = kHintMaxChars - text.size();
        if (budget < 120)
            break;
        ++n;
        QString head = QString::fromUtf8("\n%1. [%2%3] task: %4\n   lessons: ")
                           .arg(n)
                           .arg(entry.workerType)
                           .arg(entry.ok ? QString() : QString::fromUtf8(", failed"))
                           .arg(clip(entry.task, 120).simplified());
        QString lessons = entry.lessons.trimmed();
        const int room = budget - head.size();
        if (room < 40)
            break;
        if (lessons.size() > room)
            lessons = lessons.left(room - 3).trimmed() + QString::fromUtf8("...");
        lessons.replace(QChar('\n'), QString::fromUtf8("\n   "));
        text += head + lessons;
    }
    if (n == 0)
        return QString();
    return text;
}

//----------------------------------------------

int KnowledgeStore::count()
{
    ensureLoaded();
    return _entries.size();
}
