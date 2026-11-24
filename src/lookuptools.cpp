#include "lookuptools.h"
//----------------------------------------------
#include "appsettings.h"
#include "pdfpage.h"
#include "pdftext.h"
#include "projectworkspace.h"
#include "worker.h"
//----------------------------------------------
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegExp>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

//----------------------------------------------

namespace
{

const int kMaxHits = 200;
const int kDefaultHits = 50;
const int kMaxPatterns = 8;
const int kMaxNamedFiles = 32;
const int kMaxScanBytes = 2 * 1024 * 1024;
const int kPdfDefaultChars = 16000;
const int kPdfMaxChars = 80000;
const int kPdfMaxBytes = 32 * 1024 * 1024;
const int kHtmlMaxBytes = 2 * 1024 * 1024;
const int kPageDefaultChars = 12000;
const int kPageMaxChars = 40000;

//----------------------------------------------

bool parseArgs(QString argumentsJson, QJsonObject *object, QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(argumentsJson.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        if (error)
            *error = QString::fromUtf8("Invalid JSON arguments.");
        return false;
    }
    if (object)
        *object = document.object();
    return true;
}

//----------------------------------------------

int clampHits(int requested)
{
    if (requested <= 0)
        return kDefaultHits;
    if (requested > kMaxHits)
        return kMaxHits;
    return requested;
}

//----------------------------------------------

int clampContext(int requested)
{
    if (requested < 0)
        return 0;
    if (requested > 3)
        return 3;
    return requested;
}

//----------------------------------------------

bool skippedExtension(const QString &fileName)
{
    const QString ext = QFileInfo(fileName).suffix().toLower();
    if (ext.isEmpty())
        return false;
    static const char *names[] = {
        "exe", "dll", "obj", "o", "pdb", "png", "jpg", "jpeg", "gif", "webp", "ico",
        "zip", "7z", "rar", "pdf", "mp3", "mp4", "wav", "ogg", "bin", "lib", "a",
        "so", "ttf", "woff", "woff2", "pyc", "msi", "cab", "img", "pak",
        0
    };
    for (int i = 0; names[i]; ++i)
    {
        if (ext == QString::fromLatin1(names[i]))
            return true;
    }
    return false;
}

//----------------------------------------------

bool looksBinary(const QByteArray &head)
{
    return head.contains('\0');
}

//----------------------------------------------

bool globHit(const QString &relative, const QStringList &globs)
{
    if (globs.isEmpty())
        return true;
    const QString base = QFileInfo(relative).fileName();
    foreach (const QString &glob, globs)
    {
        const QRegExp onName(glob, Qt::CaseInsensitive, QRegExp::Wildcard);
        const QRegExp onPath(glob, Qt::CaseInsensitive, QRegExp::Wildcard);
        if (onName.exactMatch(base) || onPath.exactMatch(relative))
            return true;
    }
    return false;
}

//----------------------------------------------

QString clipLine(const QString &line)
{
    QString text = line;
    text.replace(QChar('\t'), QChar(' '));
    if (text.size() > 300)
        text = text.left(300) + QString::fromUtf8("…");
    return text;
}

//----------------------------------------------

struct TextHit
{
    int     line;
    QString text;
};

QString searchText(const QString &body, const QStringList &patterns, bool asRegex,
                   bool ignoreCase, int context, int maxHits, bool *truncated,
                   int *foundCount, QString *error)
{
    QRegularExpression re;
    if (asRegex)
    {
        QRegularExpression::PatternOptions options = QRegularExpression::NoPatternOption;
        if (ignoreCase)
            options |= QRegularExpression::CaseInsensitiveOption;
        re.setPattern(patterns.at(0));
        re.setPatternOptions(options);
        if (!re.isValid())
        {
            if (error)
                *error = QString::fromUtf8("Invalid regex: %1").arg(re.errorString());
            return QString();
        }
    }

    const QStringList lines = body.split(QChar('\n'));
    const Qt::CaseSensitivity sense = ignoreCase ? Qt::CaseInsensitive : Qt::CaseSensitive;
    QList<TextHit> hits;
    for (int i = 0; i < lines.size(); ++i)
    {
        bool matched = false;
        if (asRegex)
            matched = re.match(lines.at(i)).hasMatch();
        else
        {
            foreach (const QString &pattern, patterns)
            {
                if (lines.at(i).contains(pattern, sense))
                {
                    matched = true;
                    break;
                }
            }
        }
        if (!matched)
            continue;
        if (hits.size() >= maxHits)
        {
            if (truncated)
                *truncated = true;
            break;
        }
        TextHit hit;
        hit.line = i + 1;
        hit.text = clipLine(lines.at(i));
        hits.append(hit);
    }
    if (foundCount)
        *foundCount = hits.size();

    QString report;
    foreach (const TextHit &hit, hits)
    {
        if (context > 0)
        {
            const int from = qMax(0, hit.line - 1 - context);
            const int to = qMin(lines.size() - 1, hit.line - 1 + context);
            for (int i = from; i <= to; ++i)
            {
                const QString mark = (i == hit.line - 1)
                    ? QString::fromUtf8(":")
                    : QString::fromUtf8(" ");
                report += QString::fromUtf8("%1%2%3\n")
                              .arg(i + 1, 5)
                              .arg(mark)
                              .arg(clipLine(lines.at(i)));
            }
            report += QChar('\n');
        }
        else
        {
            report += QString::fromUtf8("%1: %2\n").arg(hit.line).arg(hit.text);
        }
    }
    return report;
}

//----------------------------------------------

QString readTextFile(const QString &absolute, QString *error)
{
    QFileInfo info(absolute);
    if (!info.isFile())
    {
        if (error)
            *error = QString::fromUtf8("Not a file: %1").arg(absolute);
        return QString();
    }
    if (info.size() > kMaxScanBytes)
    {
        if (error)
            *error = QString::fromUtf8("File is too large to scan (%1 bytes).").arg(info.size());
        return QString();
    }
    QFile file(absolute);
    if (!file.open(QIODevice::ReadOnly))
    {
        if (error)
            *error = QString::fromUtf8("Failed to read file: %1").arg(file.errorString());
        return QString();
    }
    const QByteArray raw = file.readAll();
    file.close();
    if (looksBinary(raw.left(4096)))
    {
        if (error)
            *error = QString::fromUtf8("File looks binary.");
        return QString();
    }
    return QString::fromUtf8(raw);
}

//----------------------------------------------

QString htmlUnescape(QString text)
{
    text.replace(QString::fromLatin1("&nbsp;"), QString::fromLatin1(" "));
    text.replace(QString::fromLatin1("&amp;"), QString::fromLatin1("&"));
    text.replace(QString::fromLatin1("&lt;"), QString::fromLatin1("<"));
    text.replace(QString::fromLatin1("&gt;"), QString::fromLatin1(">"));
    text.replace(QString::fromLatin1("&quot;"), QString::fromLatin1("\""));
    text.replace(QString::fromLatin1("&#39;"), QString::fromLatin1("'"));
    text.replace(QString::fromLatin1("&apos;"), QString::fromLatin1("'"));
    QRegularExpression num(QString::fromLatin1("&#(x?[0-9A-Fa-f]+);"));
    QRegularExpressionMatchIterator it = num.globalMatch(text);
    QString out = text;
    int shift = 0;
    while (it.hasNext())
    {
        const QRegularExpressionMatch match = it.next();
        const QString token = match.captured(1);
        bool ok = false;
        int code = 0;
        if (token.startsWith(QLatin1Char('x'), Qt::CaseInsensitive))
            code = token.mid(1).toInt(&ok, 16);
        else
            code = token.toInt(&ok, 10);
        if (!ok || code <= 0 || code > 0x10ffff)
            continue;
        const int start = match.capturedStart() + shift;
        const int length = match.capturedLength();
        const QString repl = QString(QChar(code));
        out.replace(start, length, repl);
        shift += repl.size() - length;
    }
    return out;
}

//----------------------------------------------

QString stripTags(QString html)
{
    html.replace(QRegularExpression(QString::fromLatin1("<[^>]+>")), QString());
    return htmlUnescape(html).simplified();
}

//----------------------------------------------

QString attrValue(const QString &tag, const QString &name)
{
    const QRegularExpression re(
        QString::fromLatin1("%1\\s*=\\s*([\"'])(.*?)\\1").arg(QRegularExpression::escape(name)),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = re.match(tag);
    if (!match.hasMatch())
        return QString();
    return htmlUnescape(match.captured(2));
}

//----------------------------------------------

struct WebHit
{
    QString title;
    QString url;
    QString snippet;
};

QString duckUrl(const QString &href)
{
    QUrl url(href);
    if (url.host().contains(QString::fromLatin1("duckduckgo."), Qt::CaseInsensitive))
    {
        const QString real = QUrlQuery(url).queryItemValue(QString::fromLatin1("uddg"),
                                                           QUrl::FullyDecoded);
        if (real.startsWith(QString::fromLatin1("http")))
            return real;
    }
    if (href.startsWith(QString::fromLatin1("//")))
        return QString::fromLatin1("https:") + href;
    return href;
}

//----------------------------------------------

QString elementInnerText(const QString &html, int mark)
{
    const int tagStart = html.lastIndexOf(QLatin1Char('<'), mark);
    const int tagEnd = html.indexOf(QLatin1Char('>'), mark);
    if (tagStart < 0 || tagEnd < 0 || tagStart > mark)
        return QString();
    QString open = html.mid(tagStart + 1, tagEnd - tagStart - 1).trimmed();
    const int space = open.indexOf(QRegExp(QString::fromLatin1("[\\s/]")));
    if (space >= 0)
        open = open.left(space);
    open = open.toLower();
    if (open.isEmpty() || open.startsWith(QLatin1Char('/')) || open.startsWith(QLatin1Char('!')))
        return QString();
    const int close = html.indexOf(QString::fromLatin1("</%1>").arg(open),
                                   tagEnd, Qt::CaseInsensitive);
    if (close < 0 || close > tagEnd + 6000)
        return QString();
    return stripTags(html.mid(tagEnd + 1, close - tagEnd - 1));
}

//----------------------------------------------

QString bingTargetUrl(const QString &href)
{
    QString raw = href.trimmed();
    const int at = raw.indexOf(QString::fromLatin1("u="));
    if (at >= 0)
    {
        int end = raw.indexOf(QLatin1Char('&'), at + 2);
        if (end < 0)
            end = raw.size();
        QString u = QUrl::fromPercentEncoding(raw.mid(at + 2, end - at - 2).toUtf8());
        if (u.startsWith(QString::fromLatin1("a1")))
            u = u.mid(2);
        QByteArray b64 = u.toLatin1();
        b64.replace('-', '+');
        b64.replace('_', '/');
        while (b64.size() % 4 != 0)
            b64.append('=');
        const QString decoded = QString::fromUtf8(QByteArray::fromBase64(b64)).trimmed();
        if (decoded.startsWith(QString::fromLatin1("http://"))
            || decoded.startsWith(QString::fromLatin1("https://")))
            return decoded;
    }
    QUrl url(raw);
    if (url.host().contains(QString::fromLatin1("bing."), Qt::CaseInsensitive))
        return QString();
    return raw;
}

//----------------------------------------------

bool isMicrosoftUtility(const QUrl &url)
{
    const QString host = url.host().toLower();
    if (host == QString::fromLatin1("bing.com") || host.endsWith(QString::fromLatin1(".bing.com")))
        return true;
    if (host == QString::fromLatin1("support.microsoft.com")
        || host == QString::fromLatin1("account.microsoft.com")
        || host == QString::fromLatin1("go.microsoft.com")
        || host == QString::fromLatin1("techcommunity.microsoft.com"))
        return true;
    if (host == QString::fromLatin1("www.microsoft.com") || host == QString::fromLatin1("microsoft.com"))
    {
        const QString path = url.path().toLower();
        if (path.contains(QString::fromLatin1("/edge"))
            || path.contains(QString::fromLatin1("/windows"))
            || path.isEmpty()
            || path == QString::fromLatin1("/"))
            return true;
    }
    return false;
}

//----------------------------------------------

bool isSearchEnginePage(const QUrl &url)
{
    const QString host = url.host().toLower();
    const QString path = url.path().toLower();
    if (host.contains(QString::fromLatin1("duckduckgo."))
        && (path.isEmpty() || path == QString::fromLatin1("/")
            || path.contains(QString::fromLatin1("html"))))
        return true;
    if (host.contains(QString::fromLatin1("bing.")) && path.startsWith(QString::fromLatin1("/search")))
        return true;
    if (host.contains(QString::fromLatin1("google.")) && path.startsWith(QString::fromLatin1("/search")))
        return true;
    if ((host.contains(QString::fromLatin1("yandex.")) || host == QString::fromLatin1("ya.ru"))
        && path.contains(QString::fromLatin1("search")))
        return true;
    return false;
}

//----------------------------------------------

QString htmlToPlain(QString html)
{
    const QRegularExpression block(
        QString::fromLatin1("<(script|style|noscript|svg)\\b[^>]*>.*?</\\1>"),
        QRegularExpression::CaseInsensitiveOption
            | QRegularExpression::DotMatchesEverythingOption);
    html.replace(block, QString::fromLatin1(" "));
    html.replace(QRegularExpression(QString::fromLatin1("<br\\s*/?>"),
                                    QRegularExpression::CaseInsensitiveOption),
                 QString::fromLatin1("\n"));
    html.replace(QRegularExpression(
                     QString::fromLatin1("</p>|</div>|</li>|</tr>|</h[1-6]>"),
                     QRegularExpression::CaseInsensitiveOption),
                 QString::fromLatin1("\n"));
    html.replace(QRegularExpression(QString::fromLatin1("<[^>]+>")), QString());
    html = htmlUnescape(html);
    html.replace(QRegularExpression(QString::fromLatin1("[\\t\\f\\r ]+")),
                 QString::fromLatin1(" "));
    html.replace(QRegularExpression(QString::fromLatin1(" *\\n *")),
                 QString::fromLatin1("\n"));
    html.replace(QRegularExpression(QString::fromLatin1("\\n{3,}")),
                 QString::fromLatin1("\n\n"));
    return html.trimmed();
}

//----------------------------------------------

void addHit(QList<WebHit> *hits, const QString &title, const QString &url,
            const QString &snippet, int limit)
{
    if (!hits || hits->size() >= limit)
        return;
    const QString cleanUrl = url.trimmed();
    const QString cleanTitle = title.simplified().left(180);
    if (!cleanUrl.startsWith(QString::fromLatin1("http")) || cleanTitle.isEmpty())
        return;
    foreach (const WebHit &have, *hits)
    {
        if (have.url == cleanUrl)
            return;
    }
    WebHit hit;
    hit.title = cleanTitle;
    hit.url = cleanUrl;
    hit.snippet = snippet.simplified().left(480);
    hits->append(hit);
}

//----------------------------------------------

QList<WebHit> parseDuck(const QString &html, int limit)
{
    QList<WebHit> hits;
    int pos = 0;
    while (hits.size() < limit)
    {
        const int mark = html.indexOf(QString::fromLatin1("result__a"), pos);
        if (mark < 0)
            break;
        const int tag = html.lastIndexOf(QString::fromLatin1("<a"), mark);
        const int tagEnd = html.indexOf(QChar('>'), mark);
        const int close = html.indexOf(QString::fromLatin1("</a>"), tagEnd);
        if (tag < 0 || tagEnd < 0 || close < 0)
            break;
        const QString href = duckUrl(attrValue(html.mid(tag, tagEnd - tag),
                                               QString::fromLatin1("href")));
        const QString title = stripTags(html.mid(tagEnd + 1, close - tagEnd - 1));
        QString snippet;
        const int snip = html.indexOf(QString::fromLatin1("result__snippet"), close);
        if (snip >= 0 && snip < close + 2500)
            snippet = elementInnerText(html, snip);
        addHit(&hits, title, href, snippet, limit);
        pos = close + 4;
    }
    return hits;
}

//----------------------------------------------

QList<WebHit> parseBing(const QString &html, int limit)
{
    QList<WebHit> hits;
    int pos = 0;
    while (hits.size() < limit)
    {
        const int mark = html.indexOf(QString::fromLatin1("b_algo"), pos);
        if (mark < 0)
            break;
        const int end = html.indexOf(QString::fromLatin1("</li>"), mark);
        const QString block = html.mid(mark, (end < 0 ? 8000 : qMax(1, end - mark)));
        QString href;
        QString title;
        int scan = 0;
        while (scan < block.size())
        {
            const int hrefAt = block.indexOf(QString::fromLatin1("href=\""), scan);
            if (hrefAt < 0)
                break;
            const int hrefEnd = block.indexOf(QChar('"'), hrefAt + 6);
            if (hrefEnd < 0)
                break;
            const QString target = bingTargetUrl(
                htmlUnescape(block.mid(hrefAt + 6, hrefEnd - hrefAt - 6)));
            const QUrl targetUrl(target);
            if (target.startsWith(QString::fromLatin1("http")) && !isMicrosoftUtility(targetUrl))
            {
                href = target;
                const int titleStart = block.indexOf(QChar('>'), hrefEnd);
                const int titleEnd = block.indexOf(QString::fromLatin1("</a>"), titleStart);
                if (titleStart > 0 && titleEnd > titleStart)
                    title = stripTags(block.mid(titleStart + 1, titleEnd - titleStart - 1));
                break;
            }
            scan = hrefEnd + 1;
        }
        const int h2 = block.indexOf(QString::fromLatin1("<h2"));
        if (h2 >= 0)
        {
            const QString heading = elementInnerText(block, h2 + 2);
            if (!heading.isEmpty())
                title = heading;
        }
        const int httpAt = title.indexOf(QString::fromLatin1("http"));
        if (httpAt > 0)
            title = title.left(httpAt).simplified();
        QString snippet;
        const int p = block.indexOf(QString::fromLatin1("<p"));
        if (p >= 0)
            snippet = elementInnerText(block, p + 1);
        if (!href.isEmpty())
            addHit(&hits, title, href, snippet, limit);
        pos = mark + qMax(6, block.size());
    }
    return hits;
}

//----------------------------------------------

QList<WebHit> parseGoogle(const QString &html, int limit)
{
    QList<WebHit> hits;
    int pos = 0;
    const QString key = QString::fromLatin1("/url?q=");
    while (hits.size() < limit)
    {
        const int mark = html.indexOf(key, pos);
        if (mark < 0)
            break;
        const int end = html.indexOf(QChar('&'), mark + key.size());
        const int quote = html.indexOf(QChar('"'), mark + key.size());
        int stop = end;
        if (stop < 0 || (quote > 0 && quote < stop))
            stop = quote;
        if (stop < 0)
            break;
        const QString href = QUrl::fromPercentEncoding(
            html.mid(mark + key.size(), stop - mark - key.size()).toUtf8());
        const int tagEnd = html.indexOf(QChar('>'), stop);
        const int close = html.indexOf(QString::fromLatin1("</a>"), tagEnd);
        const QString title = (tagEnd > 0 && close > tagEnd)
            ? stripTags(html.mid(tagEnd + 1, close - tagEnd - 1))
            : href;
        addHit(&hits, title, href, QString(), limit);
        pos = stop + 1;
    }
    return hits;
}

//----------------------------------------------

QList<WebHit> parseYandex(const QString &html, int limit)
{
    QList<WebHit> hits;
    int pos = 0;
    while (hits.size() < limit)
    {
        const int tag = html.indexOf(QString::fromLatin1("<a"), pos);
        if (tag < 0)
            break;
        const int tagEnd = html.indexOf(QChar('>'), tag);
        const int close = html.indexOf(QString::fromLatin1("</a>"), tagEnd);
        if (tagEnd < 0 || close < 0)
            break;
        const QString href = attrValue(html.mid(tag, tagEnd - tag), QString::fromLatin1("href"));
        const QString title = stripTags(html.mid(tagEnd + 1, close - tagEnd - 1));
        const QUrl url(href);
        const QString host = url.host().toLower();
        const bool own = host.contains(QString::fromLatin1("yandex."))
            || host.contains(QString::fromLatin1("ya.ru"))
            || host.contains(QString::fromLatin1("yastatic."));
        if (!own)
            addHit(&hits, title, href, QString(), limit);
        pos = close + 4;
    }
    return hits;
}

//----------------------------------------------

QString normalizeEngine(QString engine, QString *error)
{
    engine = engine.trimmed().toLower();
    if (engine == QString::fromLatin1("ddg"))
        engine = QString::fromLatin1("duckduckgo");
    if (engine == QString::fromLatin1("duckduckgo")
        || engine == QString::fromLatin1("bing")
        || engine == QString::fromLatin1("google")
        || engine == QString::fromLatin1("yandex"))
        return engine;
    if (error)
    {
        *error = QString::fromUtf8(
            "Unknown engine \"%1\". Use duckduckgo, bing, google, or yandex.")
                     .arg(engine);
    }
    return QString();
}

//----------------------------------------------

QUrl engineUrl(const QString &engine, const QString &query, int limit)
{
    const QByteArray encoded = QUrl::toPercentEncoding(query);
    if (engine == QString::fromLatin1("bing"))
    {
        return QUrl(QString::fromLatin1("https://www.bing.com/search?q=%1&count=%2")
                        .arg(QString::fromUtf8(encoded))
                        .arg(limit));
    }
    if (engine == QString::fromLatin1("google"))
    {
        return QUrl(QString::fromLatin1("https://www.google.com/search?gbv=1&num=%1&q=%2")
                        .arg(limit)
                        .arg(QString::fromUtf8(encoded)));
    }
    if (engine == QString::fromLatin1("yandex"))
    {
        return QUrl(QString::fromLatin1("https://yandex.com/search/?text=%1")
                        .arg(QString::fromUtf8(encoded)));
    }
    return QUrl(QString::fromLatin1("https://html.duckduckgo.com/html/?q=%1")
                    .arg(QString::fromUtf8(encoded)));
}

//----------------------------------------------

QByteArray httpGet(const QUrl &url, int timeoutMs, int maxBytes,
                   QNetworkReply **replySlot, QEventLoop **loopSlot, bool *interrupted,
                   int *status, QString *error)
{
    if (url.scheme() != QString::fromLatin1("http")
        && url.scheme() != QString::fromLatin1("https"))
    {
        if (error)
            *error = QString::fromUtf8("Only http and https URLs are allowed.");
        return QByteArray();
    }

    QNetworkAccessManager manager;
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
    request.setRawHeader("User-Agent", "Mozilla/5.0 (compatible; Unique)");
    request.setRawHeader("Accept", "text/html,application/pdf,application/octet-stream,*/*");
    request.setRawHeader("Accept-Language", "en,ru;q=0.8");

    QNetworkReply *reply = manager.get(request);
    if (replySlot)
        *replySlot = reply;
    QEventLoop loop;
    if (loopSlot)
        *loopSlot = &loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, SIGNAL(timeout()), &loop, SLOT(quit()));
    QObject::connect(reply, SIGNAL(finished()), &loop, SLOT(quit()));
    timer.start(timeoutMs);
    loop.exec();
    if (loopSlot)
        *loopSlot = 0;
    if (replySlot)
        *replySlot = 0;

    if (interrupted && *interrupted)
    {
        reply->abort();
        reply->deleteLater();
        if (error)
            *error = QString::fromUtf8("Interrupted.");
        return QByteArray();
    }
    if (reply->isRunning())
    {
        reply->abort();
        reply->deleteLater();
        if (error)
            *error = QString::fromUtf8("Timed out.");
        return QByteArray();
    }

    if (status)
        *status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QByteArray body = reply->readAll();
    if (body.size() > maxBytes)
        body.truncate(maxBytes);
    if (reply->error() != QNetworkReply::NoError && body.isEmpty())
    {
        if (error)
            *error = reply->errorString();
    }
    reply->deleteLater();
    return body;
}

//----------------------------------------------

QString formatWebHits(const QString &engine, const QList<WebHit> &hits, int status)
{
    if (hits.isEmpty())
    {
        if (status >= 200 && status < 300)
        {
            return QString::fromUtf8(
                "engine: %1\n"
                "The result page was not parsed (HTTP %2). "
                "The markup changed or the engine returned a consent page. "
                "This is not an empty web. Try another engine.")
                .arg(engine)
                .arg(status);
        }
        return QString::fromUtf8(
            "engine: %1\nNo results parsed (HTTP %2). Try another engine.")
            .arg(engine)
            .arg(status);
    }
    QString out = QString::fromUtf8("engine: %1\n").arg(engine);
    for (int i = 0; i < hits.size(); ++i)
    {
        const WebHit &hit = hits.at(i);
        out += QString::fromUtf8("%1. %2\n   %3\n")
                   .arg(i + 1)
                   .arg(hit.title)
                   .arg(hit.url);
        if (!hit.snippet.isEmpty())
            out += QString::fromUtf8("   %1\n").arg(hit.snippet);
    }
    return out.trimmed();
}

//----------------------------------------------

bool approvedInternet(Worker *owner, const QString &action, bool *ok, QString *error)
{
    if (!owner)
    {
        if (ok)
            *ok = false;
        if (error)
            *error = QString::fromUtf8("Worker is missing.");
        return false;
    }
    const QString blocked = owner->openingBlockReason();
    if (!blocked.isEmpty())
    {
        if (ok)
            *ok = false;
        if (error)
            *error = blocked;
        return false;
    }
    const QString decision = owner->askPermission(
        action, true, QString::fromUtf8("tool:internet"));
    if (!decision.startsWith(QString::fromUtf8("approved")))
    {
        if (ok)
            *ok = true;
        if (error)
            *error = QString::fromUtf8("Not run. User answer: %1").arg(decision);
        return false;
    }
    owner->noteWorkAction();
    return true;
}

} // namespace

//----------------------------------------------

SearchFilesAgentTool::SearchFilesAgentTool(ProjectWorkspace *workspace, Worker *owner,
                                           QObject *parent)
    : AgentTool(parent)
    , _workspace(workspace)
    , _owner(owner)
{
}

//----------------------------------------------

QString SearchFilesAgentTool::name() const
{
    return QString::fromUtf8("search_files");
}

//----------------------------------------------

QString SearchFilesAgentTool::description() const
{
    return QString::fromUtf8(
        "Literal substring search across project files. Use this instead of findstr, "
        "Select-String, or a script. pattern is one string, or patterns is up to 8. "
        "path is a file or a folder (default: project root). glob filters names, "
        "for example *.cpp;*.h. ignore_case defaults to false. context is 0..3 lines "
        "around a hit. max_hits defaults to 50 and cannot exceed 200. "
        "PDF files are skipped; use read_pdf for those.");
}

//----------------------------------------------

QJsonObject SearchFilesAgentTool::parametersSchema() const
{
    QJsonObject pattern;
    pattern.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    pattern.insert(QString::fromUtf8("description"),
                   QString::fromUtf8("One literal substring to find."));

    QJsonObject item;
    item.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    QJsonObject patterns;
    patterns.insert(QString::fromUtf8("type"), QString::fromUtf8("array"));
    patterns.insert(QString::fromUtf8("items"), item);
    patterns.insert(QString::fromUtf8("description"),
                    QString::fromUtf8("Up to 8 literal substrings, searched together."));

    QJsonObject path;
    path.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    path.insert(QString::fromUtf8("description"),
                QString::fromUtf8("File or folder relative to the project. Default is the root."));

    QJsonObject glob;
    glob.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    glob.insert(QString::fromUtf8("description"),
                QString::fromUtf8("Name mask. Several masks separated by ';', for example *.cpp;*.h."));

    QJsonObject ignoreCase;
    ignoreCase.insert(QString::fromUtf8("type"), QString::fromUtf8("boolean"));
    ignoreCase.insert(QString::fromUtf8("description"),
                      QString::fromUtf8("Case-insensitive match. Default false."));

    QJsonObject context;
    context.insert(QString::fromUtf8("type"), QString::fromUtf8("integer"));
    context.insert(QString::fromUtf8("description"),
                   QString::fromUtf8("Lines before and after a hit, 0..3. Default 0."));

    QJsonObject maxHits;
    maxHits.insert(QString::fromUtf8("type"), QString::fromUtf8("integer"));
    maxHits.insert(QString::fromUtf8("description"),
                   QString::fromUtf8("Maximum hits to return, 1..200. Default 50."));

    QJsonObject properties;
    properties.insert(QString::fromUtf8("pattern"), pattern);
    properties.insert(QString::fromUtf8("patterns"), patterns);
    properties.insert(QString::fromUtf8("path"), path);
    properties.insert(QString::fromUtf8("glob"), glob);
    properties.insert(QString::fromUtf8("ignore_case"), ignoreCase);
    properties.insert(QString::fromUtf8("context"), context);
    properties.insert(QString::fromUtf8("max_hits"), maxHits);

    QJsonObject schema;
    schema.insert(QString::fromUtf8("type"), QString::fromUtf8("object"));
    schema.insert(QString::fromUtf8("properties"), properties);
    return schema;
}

//----------------------------------------------

QString SearchFilesAgentTool::invoke(QString argumentsJson, bool *ok)
{
    QJsonObject args;
    QString error;
    if (!parseArgs(argumentsJson, &args, &error))
    {
        if (ok)
            *ok = false;
        return error;
    }
    if (!_workspace)
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8("Workspace is missing.");
    }

    QStringList patterns;
    const QString one = args.value(QString::fromUtf8("pattern")).toString();
    if (!one.isEmpty())
        patterns.append(one);
    const QJsonArray many = args.value(QString::fromUtf8("patterns")).toArray();
    for (int i = 0; i < many.size() && patterns.size() < kMaxPatterns; ++i)
    {
        const QString pattern = many.at(i).toString();
        if (!pattern.isEmpty() && !patterns.contains(pattern))
            patterns.append(pattern);
    }
    if (patterns.isEmpty())
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8("Provide pattern or patterns.");
    }

    const bool ignoreCase = args.value(QString::fromUtf8("ignore_case")).toBool(false);
    const int context = clampContext(args.value(QString::fromUtf8("context")).toInt(0));
    const int maxHits = clampHits(args.value(QString::fromUtf8("max_hits")).toInt(0));
    QStringList globs;
    foreach (const QString &part, args.value(QString::fromUtf8("glob")).toString().split(QChar(';')))
    {
        const QString glob = part.trimmed();
        if (!glob.isEmpty())
            globs.append(glob);
    }

    const QString requested = args.value(QString::fromUtf8("path")).toString().trimmed();
    QStringList files;
    QString resolveError;
    QString asFile;
    if (!requested.isEmpty()
        && _workspace->resolvePath(requested, true, false, &asFile, &resolveError))
    {
        files.append(asFile);
    }
    else
    {
        QString folder;
        if (!_workspace->resolveDirectory(requested, &folder, &resolveError))
        {
            if (ok)
                *ok = false;
            return resolveError;
        }
        QDirIterator it(folder, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
        int seen = 0;
        while (it.hasNext())
        {
            it.next();
            seen += 1;
            if (seen > 20000)
                break;
            const QString absolute = it.filePath();
            if (!_workspace->contains(absolute))
                continue;
            const QString relative = _workspace->toRelative(absolute);
            if (relative.startsWith(QString::fromUtf8("___project___/chats"))
                || relative.startsWith(QString::fromUtf8("___project___\\chats")))
                continue;
            if (skippedExtension(it.fileName()))
                continue;
            if (!globHit(relative, globs))
                continue;
            files.append(absolute);
        }
    }

    if (_owner)
        _owner->noteWorkAction();

    QString report;
    int hits = 0;
    bool truncated = false;
    int scanned = 0;
    foreach (const QString &absolute, files)
    {
        if (hits >= maxHits)
        {
            truncated = true;
            break;
        }
        QString readError;
        const QString body = readTextFile(absolute, &readError);
        if (body.isEmpty())
            continue;
        scanned += 1;
        bool fileTruncated = false;
        int added = 0;
        const QString found = searchText(body, patterns, false, ignoreCase, context,
                                         maxHits - hits, &fileTruncated, &added, 0);
        if (found.isEmpty())
            continue;
        const QString relative = _workspace->toRelative(absolute);
        report += relative;
        report += QChar('\n');
        report += found;
        report += QChar('\n');
        hits += added;
        if (fileTruncated)
            truncated = true;
    }

    if (hits == 0)
    {
        if (ok)
            *ok = true;
        return QString::fromUtf8("No matches in %1 file(s).").arg(scanned);
    }
    if (truncated || hits >= maxHits)
        report += QString::fromUtf8("Stopped at max_hits %1.\n").arg(maxHits);
    if (ok)
        *ok = true;
    return report.trimmed();
}

//----------------------------------------------

FindInFilesAgentTool::FindInFilesAgentTool(ProjectWorkspace *workspace, Worker *owner,
                                           QObject *parent)
    : AgentTool(parent)
    , _workspace(workspace)
    , _owner(owner)
{
}

//----------------------------------------------

QString FindInFilesAgentTool::name() const
{
    return QString::fromUtf8("find_in_files");
}

//----------------------------------------------

QString FindInFilesAgentTool::description() const
{
    return QString::fromUtf8(
        "Search the files you name, either a literal substring or a regular expression. "
        "Use this instead of a python or PowerShell one-off. paths is the file list "
        "(max 32). pattern is the substring, or a regex when regex is true. "
        "A regex is applied to each line, not across lines. ignore_case defaults to false. "
        "context is 0..3 lines around a hit. max_hits defaults to 50 and cannot exceed 200.");
}

//----------------------------------------------

QJsonObject FindInFilesAgentTool::parametersSchema() const
{
    QJsonObject pathItem;
    pathItem.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    QJsonObject paths;
    paths.insert(QString::fromUtf8("type"), QString::fromUtf8("array"));
    paths.insert(QString::fromUtf8("items"), pathItem);
    paths.insert(QString::fromUtf8("description"),
                 QString::fromUtf8("Project-relative files to search, max 32."));

    QJsonObject pattern;
    pattern.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    pattern.insert(QString::fromUtf8("description"),
                   QString::fromUtf8("Literal substring, or a regular expression when regex is true."));

    QJsonObject regex;
    regex.insert(QString::fromUtf8("type"), QString::fromUtf8("boolean"));
    regex.insert(QString::fromUtf8("description"),
                 QString::fromUtf8("true: pattern is a regex. false: literal substring. Default false."));

    QJsonObject ignoreCase;
    ignoreCase.insert(QString::fromUtf8("type"), QString::fromUtf8("boolean"));
    ignoreCase.insert(QString::fromUtf8("description"),
                      QString::fromUtf8("Case-insensitive match. Default false."));

    QJsonObject context;
    context.insert(QString::fromUtf8("type"), QString::fromUtf8("integer"));
    context.insert(QString::fromUtf8("description"),
                   QString::fromUtf8("Lines before and after a hit, 0..3. Default 0."));

    QJsonObject maxHits;
    maxHits.insert(QString::fromUtf8("type"), QString::fromUtf8("integer"));
    maxHits.insert(QString::fromUtf8("description"),
                   QString::fromUtf8("Maximum hits to return, 1..200. Default 50."));

    QJsonObject properties;
    properties.insert(QString::fromUtf8("paths"), paths);
    properties.insert(QString::fromUtf8("pattern"), pattern);
    properties.insert(QString::fromUtf8("regex"), regex);
    properties.insert(QString::fromUtf8("ignore_case"), ignoreCase);
    properties.insert(QString::fromUtf8("context"), context);
    properties.insert(QString::fromUtf8("max_hits"), maxHits);

    QJsonArray required;
    required.append(QString::fromUtf8("paths"));
    required.append(QString::fromUtf8("pattern"));

    QJsonObject schema;
    schema.insert(QString::fromUtf8("type"), QString::fromUtf8("object"));
    schema.insert(QString::fromUtf8("properties"), properties);
    schema.insert(QString::fromUtf8("required"), required);
    return schema;
}

//----------------------------------------------

QString FindInFilesAgentTool::invoke(QString argumentsJson, bool *ok)
{
    QJsonObject args;
    QString error;
    if (!parseArgs(argumentsJson, &args, &error))
    {
        if (ok)
            *ok = false;
        return error;
    }
    if (!_workspace)
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8("Workspace is missing.");
    }

    const QString pattern = args.value(QString::fromUtf8("pattern")).toString();
    if (pattern.isEmpty())
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8("pattern is empty.");
    }
    if (pattern.size() > 200)
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8("pattern is too long (max 200 characters).");
    }

    QStringList paths;
    const QJsonArray listed = args.value(QString::fromUtf8("paths")).toArray();
    for (int i = 0; i < listed.size() && paths.size() < kMaxNamedFiles; ++i)
    {
        const QString path = listed.at(i).toString().trimmed();
        if (!path.isEmpty())
            paths.append(path);
    }
    const QString single = args.value(QString::fromUtf8("path")).toString().trimmed();
    if (paths.isEmpty() && !single.isEmpty())
        paths.append(single);
    if (paths.isEmpty())
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8("Provide paths.");
    }

    const bool asRegex = args.value(QString::fromUtf8("regex")).toBool(false);
    const bool ignoreCase = args.value(QString::fromUtf8("ignore_case")).toBool(false);
    const int context = clampContext(args.value(QString::fromUtf8("context")).toInt(0));
    const int maxHits = clampHits(args.value(QString::fromUtf8("max_hits")).toInt(0));
    QStringList patterns;
    patterns.append(pattern);

    if (_owner)
        _owner->noteWorkAction();

    QString report;
    bool truncated = false;
    int left = maxHits;
    foreach (const QString &relative, paths)
    {
        if (left <= 0)
        {
            truncated = true;
            break;
        }
        QString absolute;
        if (!_workspace->resolvePath(relative, true, false, &absolute, &error))
        {
            report += relative + QString::fromUtf8(": ") + error + QChar('\n');
            continue;
        }
        const QString body = readTextFile(absolute, &error);
        if (body.isEmpty())
        {
            report += _workspace->toRelative(absolute) + QString::fromUtf8(": ") + error + QChar('\n');
            continue;
        }
        bool fileTruncated = false;
        int added = 0;
        QString matchError;
        const QString found = searchText(body, patterns, asRegex, ignoreCase, context,
                                         left, &fileTruncated, &added, &matchError);
        left -= added;
        if (!matchError.isEmpty())
        {
            if (ok)
                *ok = false;
            return matchError;
        }
        if (found.isEmpty())
            continue;
        report += _workspace->toRelative(absolute);
        report += QChar('\n');
        report += found;
        report += QChar('\n');
        if (fileTruncated)
        {
            truncated = true;
            break;
        }
    }

    if (report.trimmed().isEmpty())
    {
        if (ok)
            *ok = true;
        return QString::fromUtf8("No matches.");
    }
    if (truncated)
        report += QString::fromUtf8("Stopped at max_hits %1.\n").arg(maxHits);
    if (ok)
        *ok = true;
    return report.trimmed();
}

//----------------------------------------------

WebSearchAgentTool::WebSearchAgentTool(Worker *owner, QObject *parent)
    : AgentTool(parent)
    , _owner(owner)
    , _reply(0)
    , _loop(0)
    , _interrupted(false)
{
}

//----------------------------------------------

QString WebSearchAgentTool::name() const
{
    return QString::fromUtf8("web_search");
}

//----------------------------------------------

QString WebSearchAgentTool::description() const
{
    return QString::fromUtf8(
        "Search the web and return titles, addresses, and short snippets. "
        "You choose engine: duckduckgo, bing, google, or yandex. "
        "limit is 1..30, default 10. A title or a snippet is not the page: "
        "call read_page on the result address that bears on the fact. "
        "If nothing was parsed, the engine page changed or asked for consent; "
        "that is not an empty web. The user confirms the first call; "
        "Always then covers later web_search, read_page, and read_pdf downloads. "
        "Do not curl a search page and do not parse its HTML yourself.");
}

//----------------------------------------------

QJsonObject WebSearchAgentTool::parametersSchema() const
{
    QJsonObject query;
    query.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    query.insert(QString::fromUtf8("description"),
                 QString::fromUtf8("Search query."));

    QJsonObject engine;
    engine.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    engine.insert(QString::fromUtf8("description"),
                  QString::fromUtf8("Search engine: duckduckgo, bing, google, or yandex."));

    QJsonObject limit;
    limit.insert(QString::fromUtf8("type"), QString::fromUtf8("integer"));
    limit.insert(QString::fromUtf8("description"),
                 QString::fromUtf8("How many results to return, 1..30. Default 10."));

    QJsonObject properties;
    properties.insert(QString::fromUtf8("query"), query);
    properties.insert(QString::fromUtf8("engine"), engine);
    properties.insert(QString::fromUtf8("limit"), limit);

    QJsonArray required;
    required.append(QString::fromUtf8("query"));
    required.append(QString::fromUtf8("engine"));

    QJsonObject schema;
    schema.insert(QString::fromUtf8("type"), QString::fromUtf8("object"));
    schema.insert(QString::fromUtf8("properties"), properties);
    schema.insert(QString::fromUtf8("required"), required);
    return schema;
}

//----------------------------------------------

void WebSearchAgentTool::interrupt()
{
    _interrupted = true;
    if (_reply)
        _reply->abort();
    if (_loop)
        _loop->quit();
}

//----------------------------------------------

QString WebSearchAgentTool::invoke(QString argumentsJson, bool *ok)
{
    QJsonObject args;
    QString error;
    if (!parseArgs(argumentsJson, &args, &error))
    {
        if (ok)
            *ok = false;
        return error;
    }
    const QString query = args.value(QString::fromUtf8("query")).toString().trimmed();
    if (query.isEmpty())
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8("query is empty.");
    }
    const QString engine = normalizeEngine(args.value(QString::fromUtf8("engine")).toString(),
                                           &error);
    if (engine.isEmpty())
    {
        if (ok)
            *ok = false;
        return error;
    }
    int limit = args.value(QString::fromUtf8("limit")).toInt(10);
    if (limit < 1)
        limit = 1;
    if (limit > 30)
        limit = 30;

    const QString action = QString::fromUtf8("Internet search (%1):\n%2").arg(engine, query);
    if (!approvedInternet(_owner, action, ok, &error))
        return error;

    _interrupted = false;
    int status = 0;
    const QByteArray body = httpGet(engineUrl(engine, query, limit), 20000, kHtmlMaxBytes,
                                    &_reply, &_loop, &_interrupted, &status, &error);
    if (_interrupted)
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8("Interrupted.");
    }
    if (body.isEmpty())
    {
        if (ok)
            *ok = false;
        return error.isEmpty() ? QString::fromUtf8("Empty response.") : error;
    }

    const QString html = QString::fromUtf8(body);
    QList<WebHit> hits;
    if (engine == QString::fromLatin1("bing"))
        hits = parseBing(html, limit);
    else if (engine == QString::fromLatin1("google"))
        hits = parseGoogle(html, limit);
    else if (engine == QString::fromLatin1("yandex"))
        hits = parseYandex(html, limit);
    else
        hits = parseDuck(html, limit);

    if (ok)
        *ok = true;
    return formatWebHits(engine, hits, status);
}

//----------------------------------------------

ReadPageAgentTool::ReadPageAgentTool(Worker *owner, QObject *parent)
    : AgentTool(parent)
    , _owner(owner)
    , _reply(0)
    , _loop(0)
    , _interrupted(false)
{
}

//----------------------------------------------

QString ReadPageAgentTool::name() const
{
    return QString::fromUtf8("read_page");
}

//----------------------------------------------

QString ReadPageAgentTool::description() const
{
    return QString::fromUtf8(
        "Read one http or https page as text. Pass a result address from web_search, "
        "not a search-engine query page. A title or a short snippet is not the fact. "
        "The user confirms the first call; Always then covers later web_search, "
        "read_page, and read_pdf downloads. offset is the first character, 1-based, "
        "default 1. limit is how many characters, default 12000, max 40000.");
}

//----------------------------------------------

QJsonObject ReadPageAgentTool::parametersSchema() const
{
    QJsonObject url;
    url.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    url.insert(QString::fromUtf8("description"),
               QString::fromUtf8("http or https address of one result page."));

    QJsonObject offset;
    offset.insert(QString::fromUtf8("type"), QString::fromUtf8("integer"));
    offset.insert(QString::fromUtf8("description"),
                  QString::fromUtf8("First character to return, 1-based. Default 1."));

    QJsonObject limit;
    limit.insert(QString::fromUtf8("type"), QString::fromUtf8("integer"));
    limit.insert(QString::fromUtf8("description"),
                 QString::fromUtf8("Maximum characters to return. Default 12000, max 40000."));

    QJsonObject properties;
    properties.insert(QString::fromUtf8("url"), url);
    properties.insert(QString::fromUtf8("offset"), offset);
    properties.insert(QString::fromUtf8("limit"), limit);

    QJsonArray required;
    required.append(QString::fromUtf8("url"));

    QJsonObject schema;
    schema.insert(QString::fromUtf8("type"), QString::fromUtf8("object"));
    schema.insert(QString::fromUtf8("properties"), properties);
    schema.insert(QString::fromUtf8("required"), required);
    return schema;
}

//----------------------------------------------

void ReadPageAgentTool::interrupt()
{
    _interrupted = true;
    if (_reply)
        _reply->abort();
    if (_loop)
        _loop->quit();
}

//----------------------------------------------

QString ReadPageAgentTool::invoke(QString argumentsJson, bool *ok)
{
    QJsonObject args;
    QString error;
    if (!parseArgs(argumentsJson, &args, &error))
    {
        if (ok)
            *ok = false;
        return error;
    }
    const QString urlText = args.value(QString::fromUtf8("url")).toString().trimmed();
    const QUrl url(urlText);
    if (!url.isValid()
        || (url.scheme() != QString::fromLatin1("http")
            && url.scheme() != QString::fromLatin1("https")))
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8("url must be an http or https address.");
    }
    if (isSearchEnginePage(url))
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8(
            "This is a search page. Call web_search for the query, then read_page "
            "on a result address.");
    }

    int offset = args.value(QString::fromUtf8("offset")).toInt(1);
    if (offset < 1)
        offset = 1;
    int limit = args.value(QString::fromUtf8("limit")).toInt(kPageDefaultChars);
    if (limit <= 0)
        limit = kPageDefaultChars;
    if (limit > kPageMaxChars)
        limit = kPageMaxChars;

    const QString action = QString::fromUtf8("Open web page:\n%1").arg(urlText);
    if (!approvedInternet(_owner, action, ok, &error))
        return error;

    _interrupted = false;
    int status = 0;
    const QByteArray body = httpGet(url, 20000, kHtmlMaxBytes,
                                    &_reply, &_loop, &_interrupted, &status, &error);
    if (_interrupted)
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8("Interrupted.");
    }
    if (body.isEmpty())
    {
        if (ok)
            *ok = false;
        return error.isEmpty() ? QString::fromUtf8("Empty response.") : error;
    }
    if (body.startsWith("%PDF"))
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8("This address is a PDF. Call read_pdf with the same url.");
    }
    if (body.indexOf('\0') >= 0 && body.indexOf('\0') < 512)
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8("This address is not a text page.");
    }

    const QString text = htmlToPlain(QString::fromUtf8(body));
    if (text.isEmpty())
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8("The page had no text (HTTP %1).").arg(status);
    }
    if (offset > text.size())
    {
        if (ok)
            *ok = true;
        return QString::fromUtf8("Page text is %1 characters. offset is past the end.")
            .arg(text.size());
    }
    const QString slice = text.mid(offset - 1, limit);
    QString report = QString::fromUtf8("url: %1\ncharacters: %2\noffset: %3\n\n%4")
                         .arg(urlText)
                         .arg(text.size())
                         .arg(offset)
                         .arg(slice);
    if (offset - 1 + slice.size() < text.size())
    {
        report += QString::fromUtf8(
                      "\n\nMore text remains. Call read_page again with offset %1.")
                      .arg(offset + slice.size());
    }
    if (ok)
        *ok = true;
    return report;
}

//----------------------------------------------

ReadPdfAgentTool::ReadPdfAgentTool(ProjectWorkspace *workspace, Worker *owner,
                                   QObject *parent)
    : AgentTool(parent)
    , _workspace(workspace)
    , _owner(owner)
    , _reply(0)
    , _loop(0)
    , _interrupted(false)
{
}

//----------------------------------------------

QString ReadPdfAgentTool::name() const
{
    return QString::fromUtf8("read_pdf");
}

//----------------------------------------------

QString ReadPdfAgentTool::description() const
{
    return QString::fromUtf8(
        "Extract text from a PDF. path is a project file, or url is an http(s) address. "
        "Decodes Flate and LZW streams. Do not decompress a PDF in python or PowerShell. "
        "offset is the first character to return, 1-based, default 1. "
        "limit is how many characters, default 16000, max 80000. "
        "A url asks the user to confirm, the same way web_search does.");
}

//----------------------------------------------

QJsonObject ReadPdfAgentTool::parametersSchema() const
{
    QJsonObject path;
    path.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    path.insert(QString::fromUtf8("description"),
                QString::fromUtf8("PDF path relative to the project."));

    QJsonObject url;
    url.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    url.insert(QString::fromUtf8("description"),
               QString::fromUtf8("http or https address of a PDF, when it is not in the project."));

    QJsonObject offset;
    offset.insert(QString::fromUtf8("type"), QString::fromUtf8("integer"));
    offset.insert(QString::fromUtf8("description"),
                  QString::fromUtf8("First character to return, 1-based. Default 1."));

    QJsonObject limit;
    limit.insert(QString::fromUtf8("type"), QString::fromUtf8("integer"));
    limit.insert(QString::fromUtf8("description"),
                 QString::fromUtf8("Maximum characters to return. Default 16000, max 80000."));

    QJsonObject properties;
    properties.insert(QString::fromUtf8("path"), path);
    properties.insert(QString::fromUtf8("url"), url);
    properties.insert(QString::fromUtf8("offset"), offset);
    properties.insert(QString::fromUtf8("limit"), limit);

    QJsonObject schema;
    schema.insert(QString::fromUtf8("type"), QString::fromUtf8("object"));
    schema.insert(QString::fromUtf8("properties"), properties);
    return schema;
}

//----------------------------------------------

void ReadPdfAgentTool::interrupt()
{
    _interrupted = true;
    if (_reply)
        _reply->abort();
    if (_loop)
        _loop->quit();
}

//----------------------------------------------

QString ReadPdfAgentTool::invoke(QString argumentsJson, bool *ok)
{
    QJsonObject args;
    QString error;
    if (!parseArgs(argumentsJson, &args, &error))
    {
        if (ok)
            *ok = false;
        return error;
    }

    const QString path = args.value(QString::fromUtf8("path")).toString().trimmed();
    const QString urlText = args.value(QString::fromUtf8("url")).toString().trimmed();
    if (path.isEmpty() && urlText.isEmpty())
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8("Provide path or url.");
    }
    if (!path.isEmpty() && !urlText.isEmpty())
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8("Provide path or url, not both.");
    }

    int offset = args.value(QString::fromUtf8("offset")).toInt(1);
    if (offset < 1)
        offset = 1;
    int limit = args.value(QString::fromUtf8("limit")).toInt(kPdfDefaultChars);
    if (limit <= 0)
        limit = kPdfDefaultChars;
    if (limit > kPdfMaxChars)
        limit = kPdfMaxChars;

    QByteArray bytes;
    QString source;
    if (!path.isEmpty())
    {
        if (!_workspace)
        {
            if (ok)
                *ok = false;
            return QString::fromUtf8("Workspace is missing.");
        }
        QString absolute;
        if (!_workspace->resolvePath(path, true, false, &absolute, &error))
        {
            if (ok)
                *ok = false;
            return error;
        }
        QFile file(absolute);
        if (!file.open(QIODevice::ReadOnly))
        {
            if (ok)
                *ok = false;
            return QString::fromUtf8("Failed to read file: %1").arg(file.errorString());
        }
        if (file.size() > kPdfMaxBytes)
        {
            file.close();
            if (ok)
                *ok = false;
            return QString::fromUtf8("PDF is too large (%1 bytes).").arg(file.size());
        }
        bytes = file.readAll();
        file.close();
        source = _workspace->toRelative(absolute);
        if (_owner)
            _owner->noteWorkAction();
    }
    else
    {
        const QUrl url(urlText);
        const QString action = QString::fromUtf8("Read PDF from the web:\n%1").arg(urlText);
        if (!approvedInternet(_owner, action, ok, &error))
            return error;
        _interrupted = false;
        int status = 0;
        bytes = httpGet(url, 45000, kPdfMaxBytes, &_reply, &_loop, &_interrupted, &status, &error);
        if (_interrupted)
        {
            if (ok)
                *ok = false;
            return QString::fromUtf8("Interrupted.");
        }
        if (bytes.isEmpty())
        {
            if (ok)
                *ok = false;
            return error.isEmpty() ? QString::fromUtf8("Empty response.") : error;
        }
        source = urlText;
    }

    const QString text = extractPdfText(bytes, &error);
    if (text.isEmpty())
    {
        if (ok)
            *ok = false;
        return error.isEmpty() ? QString::fromUtf8("No text in this PDF.") : error;
    }

    const int start = offset - 1;
    if (start >= text.size())
    {
        if (ok)
            *ok = true;
        return QString::fromUtf8("Offset %1 is past the end (%2 characters).")
            .arg(offset)
            .arg(text.size());
    }
    const QString slice = text.mid(start, limit);
    const int next = offset + slice.size();
    QString report = QString::fromUtf8("PDF %1, characters %2-%3 of %4.\n")
                         .arg(source)
                         .arg(offset)
                         .arg(offset + slice.size() - 1)
                         .arg(text.size());
    report += slice;
    if (next <= text.size())
    {
        report += QString::fromUtf8("\nContinue with ONE call: read_pdf ");
        if (!path.isEmpty())
            report += QString::fromUtf8("path=\"%1\" ").arg(path);
        else
            report += QString::fromUtf8("url=\"%1\" ").arg(urlText);
        report += QString::fromUtf8("offset=%1").arg(next);
    }
    if (ok)
        *ok = true;
    return report;
}

//----------------------------------------------

static bool appendPageNumber(int page, QList<int> *pages, QString *error)
{
    if (page < 1)
    {
        if (error)
            *error = QString::fromUtf8("Page numbers start at 1.");
        return false;
    }
    if (!pages->contains(page))
        pages->append(page);
    if (pages->size() > 12)
    {
        if (error)
            *error = QString::fromUtf8("At most 12 pages per call.");
        return false;
    }
    return true;
}

//----------------------------------------------

static bool appendPageToken(const QString &token, QList<int> *pages, QString *error)
{
    const QString text = token.trimmed();
    if (text.isEmpty())
        return true;
    const int dash = text.indexOf(QChar('-'));
    if (dash > 0)
    {
        bool okA = false;
        bool okB = false;
        const int first = text.left(dash).trimmed().toInt(&okA);
        const int last = text.mid(dash + 1).trimmed().toInt(&okB);
        if (!okA || !okB || last < first)
        {
            if (error)
                *error = QString::fromUtf8("Bad page range: %1").arg(text);
            return false;
        }
        for (int page = first; page <= last; ++page)
        {
            if (!appendPageNumber(page, pages, error))
                return false;
        }
        return true;
    }
    bool ok = false;
    const int page = text.toInt(&ok);
    if (!ok)
    {
        if (error)
            *error = QString::fromUtf8("Bad page number: %1").arg(text);
        return false;
    }
    return appendPageNumber(page, pages, error);
}

//----------------------------------------------

PdfPageImageAgentTool::PdfPageImageAgentTool(ProjectWorkspace *workspace, Worker *owner,
                                             QObject *parent)
    : AgentTool(parent)
    , _workspace(workspace)
    , _owner(owner)
    , _interrupted(false)
{
}

//----------------------------------------------

QString PdfPageImageAgentTool::name() const
{
    return QString::fromUtf8("pdf_page_image");
}

//----------------------------------------------

QString PdfPageImageAgentTool::description() const
{
    return QString::fromUtf8(
        "Render PDF pages to PNG at a resolution you choose. path is a project PDF. "
        "dpi is required, from 36 to 300: 72 matches the page size in points, "
        "144 is a sharp page. pages is 1, or 1-3, or 1,4 (default 1, at most 12). "
        "output_dir is optional; otherwise files go to ___project___/tmp. "
        "Each result path can be shown in chat as an image line. "
        "Do not render pages with your own script.");
}

//----------------------------------------------

QJsonObject PdfPageImageAgentTool::parametersSchema() const
{
    QJsonObject path;
    path.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    path.insert(QString::fromUtf8("description"),
                QString::fromUtf8("PDF path relative to the project."));

    QJsonObject dpi;
    dpi.insert(QString::fromUtf8("type"), QString::fromUtf8("integer"));
    dpi.insert(QString::fromUtf8("description"),
               QString::fromUtf8("Resolution in dots per inch, 36..300."));

    QJsonObject pages;
    pages.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    pages.insert(QString::fromUtf8("description"),
                 QString::fromUtf8("Pages to render, 1-based. Examples: 1, 2-4, 1,3,5. Default 1."));

    QJsonObject outputDir;
    outputDir.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    outputDir.insert(QString::fromUtf8("description"),
                     QString::fromUtf8("Folder for the PNG files. Default is ___project___/tmp."));

    QJsonObject properties;
    properties.insert(QString::fromUtf8("path"), path);
    properties.insert(QString::fromUtf8("dpi"), dpi);
    properties.insert(QString::fromUtf8("pages"), pages);
    properties.insert(QString::fromUtf8("output_dir"), outputDir);

    QJsonArray required;
    required.append(QString::fromUtf8("path"));
    required.append(QString::fromUtf8("dpi"));

    QJsonObject schema;
    schema.insert(QString::fromUtf8("type"), QString::fromUtf8("object"));
    schema.insert(QString::fromUtf8("properties"), properties);
    schema.insert(QString::fromUtf8("required"), required);
    return schema;
}

//----------------------------------------------

void PdfPageImageAgentTool::interrupt()
{
    _interrupted = true;
}

//----------------------------------------------

QString PdfPageImageAgentTool::invoke(QString argumentsJson, bool *ok)
{
    QJsonObject args;
    QString error;
    if (!parseArgs(argumentsJson, &args, &error))
    {
        if (ok)
            *ok = false;
        return error;
    }
    if (_owner)
    {
        const QString blocked = _owner->openingBlockReason();
        if (!blocked.isEmpty())
        {
            if (ok)
                *ok = false;
            return blocked;
        }
    }
    if (!_workspace)
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8("Workspace is missing.");
    }

    const QString relPath = args.value(QString::fromUtf8("path")).toString().trimmed();
    if (relPath.isEmpty())
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8("path is empty.");
    }
    if (!args.contains(QString::fromUtf8("dpi")))
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8(
            "dpi is required, from 36 to 300. 72 matches the page size in points; "
            "144 is a sharp page.");
    }
    const int dpi = args.value(QString::fromUtf8("dpi")).toInt();
    if (dpi < 36 || dpi > 300)
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8("dpi must be from 36 to 300.");
    }

    QList<int> pages;
    const QJsonValue pagesValue = args.value(QString::fromUtf8("pages"));
    if (pagesValue.isArray())
    {
        const QJsonArray pageArray = pagesValue.toArray();
        for (int i = 0; i < pageArray.size(); ++i)
        {
            if (!appendPageNumber(pageArray.at(i).toInt(), &pages, &error))
            {
                if (ok)
                    *ok = false;
                return error;
            }
        }
    }
    else if (pagesValue.isDouble())
    {
        if (!appendPageNumber(pagesValue.toInt(), &pages, &error))
        {
            if (ok)
                *ok = false;
            return error;
        }
    }
    else
    {
        const QString text = pagesValue.toString().trimmed();
        if (text.isEmpty())
        {
            pages.append(1);
        }
        else
        {
            foreach (const QString &part, text.split(QChar(',')))
            {
                if (!appendPageToken(part, &pages, &error))
                {
                    if (ok)
                        *ok = false;
                    return error;
                }
            }
        }
    }
    if (pages.isEmpty())
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8("No pages to render.");
    }

    QString absolute;
    if (!_workspace->resolvePath(relPath, true, false, &absolute, &error))
    {
        if (ok)
            *ok = false;
        return error;
    }

    QString outDir;
    const QString requestedDir = args.value(QString::fromUtf8("output_dir")).toString().trimmed();
    if (requestedDir.isEmpty())
    {
        if (!_workspace->ensureTempDir())
        {
            if (ok)
                *ok = false;
            return QString::fromUtf8("Failed to create ___project___/tmp.");
        }
        outDir = _workspace->tempDir();
    }
    else if (!_workspace->resolveDirectory(requestedDir, &outDir, &error))
    {
        const QString root = QDir::cleanPath(_workspace->root());
        const QString candidate = QDir::cleanPath(root + QChar('/') + requestedDir);
        const bool inside = candidate.compare(root, Qt::CaseInsensitive) == 0
            || candidate.startsWith(root + QChar('/'), Qt::CaseInsensitive);
        if (!inside || !QDir().mkpath(candidate))
        {
            if (ok)
                *ok = false;
            return error.isEmpty()
                ? QString::fromUtf8("Failed to create output directory.")
                : error;
        }
        outDir = candidate;
    }

    QString prefix;
    const QString base = QFileInfo(absolute).completeBaseName();
    for (int i = 0; i < base.size() && prefix.size() < 40; ++i)
    {
        const QChar c = base.at(i);
        if (c.isLetterOrNumber() || c == QChar('-') || c == QChar('_'))
            prefix.append(c);
        else
            prefix.append(QChar('-'));
    }
    if (prefix.isEmpty())
        prefix = QString::fromUtf8("page");

    QFile file(absolute);
    if (file.size() > kPdfMaxBytes)
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8("PDF is larger than 32 MB.");
    }
    if (!file.open(QIODevice::ReadOnly))
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8("Failed to read %1.").arg(relPath);
    }
    const QByteArray data = file.readAll();
    file.close();

    if (_owner)
        _owner->noteWorkAction();

    _interrupted = false;
    QList<PdfRenderedPage> images;
    const QString renderError = renderPdfPages(data, pages, dpi, outDir, prefix,
                                               &images, &_interrupted);
    if (!renderError.isEmpty())
    {
        if (ok)
            *ok = false;
        return renderError;
    }

    QString report;
    int made = 0;
    foreach (const PdfRenderedPage &image, images)
    {
        if (!image.error.isEmpty())
        {
            report += image.error;
            report += QString::fromUtf8("\n");
            continue;
        }
        const QString relative = _workspace->toRelative(image.path);
        const QString shown = relative.isEmpty() ? image.path : relative;
        report += QString::fromUtf8("ok page=%1 %2x%3\n%4\n")
                      .arg(image.page)
                      .arg(image.width)
                      .arg(image.height)
                      .arg(shown);
        if (_owner)
            _owner->noteFileChanged(shown);
        made += 1;
    }
    if (_interrupted && made == 0)
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8("Interrupted.");
    }
    if (made == 0)
    {
        if (ok)
            *ok = false;
        return report.trimmed().isEmpty()
            ? QString::fromUtf8("PDF page render produced no image.")
            : report.trimmed();
    }
    if (_interrupted)
        report += QString::fromUtf8("Interrupted.\n");
    if (ok)
        *ok = true;
    return report.trimmed();
}
