#include "pdftext.h"
//----------------------------------------------
#include "inflate.h"
//----------------------------------------------
#include <QHash>
#include <QRegularExpression>
#include <QVector>

//----------------------------------------------

namespace
{

const int kMaxDecoded = 8 * 1024 * 1024;

//----------------------------------------------

int pdfBit(const QByteArray &raw, int *cursor, int *buffer, int *count, int width)
{
    while (*count < width)
    {
        if (*cursor >= raw.size())
            return -1;
        *buffer = (*buffer << 8) | (unsigned char)raw.at(*cursor);
        *cursor += 1;
        *count += 8;
    }
    *count -= width;
    const int value = (*buffer >> *count) & ((1 << width) - 1);
    if (*count == 0)
        *buffer = 0;
    else
        *buffer &= (1 << *count) - 1;
    return value;
}

//----------------------------------------------

QByteArray decodeLzw(const QByteArray &raw)
{
    QVector<QByteArray> table(4096);
    int nextCode = 258;
    int width = 9;
    for (int i = 0; i < 256; ++i)
        table[i] = QByteArray(1, (char)i);

    int cursor = 0;
    int buffer = 0;
    int count = 0;
    QByteArray out;
    QByteArray prev;
    bool hasPrev = false;

    while (out.size() < kMaxDecoded)
    {
        const int code = pdfBit(raw, &cursor, &buffer, &count, width);
        if (code < 0)
            break;
        if (code == 256)
        {
            nextCode = 258;
            width = 9;
            hasPrev = false;
            prev.clear();
            continue;
        }
        if (code == 257)
            break;

        QByteArray entry;
        if (code < 256)
            entry = table.at(code);
        else if (code < nextCode && code < table.size() && !table.at(code).isEmpty())
            entry = table.at(code);
        else if (code == nextCode && hasPrev)
            entry = prev + prev.left(1);
        if (entry.isEmpty())
            break;

        out += entry;
        if (hasPrev && nextCode < 4096)
        {
            table[nextCode] = prev + entry.left(1);
            nextCode += 1;
            if (width < 12
                && (nextCode == 511 || nextCode == 1023 || nextCode == 2047))
                width += 1;
        }
        prev = entry;
        hasPrev = true;
    }
    return out;
}

//----------------------------------------------

QByteArray decodeFlate(const QByteArray &raw)
{
    QByteArray out(kMaxDecoded, Qt::Uninitialized);
    int written = 0;
    const int rc = inflateZlib(reinterpret_cast<const unsigned char *>(raw.constData()),
                               raw.size(),
                               reinterpret_cast<unsigned char *>(out.data()),
                               out.size(),
                               &written);
    if (rc != 0 || written < 0)
        return QByteArray();
    out.truncate(written);
    return out;
}

//----------------------------------------------

QString latinText(const QByteArray &bytes)
{
    if (bytes.size() >= 2
        && (unsigned char)bytes.at(0) == 0xfe
        && (unsigned char)bytes.at(1) == 0xff)
    {
        QString text;
        for (int i = 2; i + 1 < bytes.size(); i += 2)
        {
            const ushort unit = (ushort)(((unsigned char)bytes.at(i) << 8)
                                         | (unsigned char)bytes.at(i + 1));
            text.append(QChar(unit));
        }
        return text;
    }
    return QString::fromLatin1(bytes);
}

//----------------------------------------------

QString takeLiteral(const QByteArray &data, int *index)
{
    int i = *index + 1;
    QByteArray raw;
    int depth = 1;
    while (i < data.size() && depth > 0)
    {
        const char c = data.at(i++);
        if (c == '\\')
        {
            if (i >= data.size())
                break;
            const char e = data.at(i++);
            if (e >= '0' && e <= '7')
            {
                int value = e - '0';
                int digits = 1;
                while (digits < 3 && i < data.size()
                       && data.at(i) >= '0' && data.at(i) <= '7')
                {
                    value = (value << 3) + (data.at(i++) - '0');
                    digits += 1;
                }
                raw.append((char)(value & 0xff));
            }
            else if (e == 'n')
                raw.append('\n');
            else if (e == 'r')
                raw.append('\r');
            else if (e == 't')
                raw.append('\t');
            else if (e == 'b')
                raw.append('\b');
            else if (e == 'f')
                raw.append('\f');
            else if (e == '\n')
            {
            }
            else if (e == '\r')
            {
                if (i < data.size() && data.at(i) == '\n')
                    i += 1;
            }
            else
                raw.append(e);
        }
        else if (c == '(')
        {
            depth += 1;
            raw.append(c);
        }
        else if (c == ')')
        {
            depth -= 1;
            if (depth > 0)
                raw.append(c);
        }
        else
            raw.append(c);
    }
    *index = i;
    return latinText(raw);
}

//----------------------------------------------

QString takeHex(const QByteArray &data, int *index)
{
    int i = *index + 1;
    QByteArray hex;
    while (i < data.size() && data.at(i) != '>')
    {
        const char c = data.at(i++);
        if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))
            hex.append(c);
    }
    if (i < data.size() && data.at(i) == '>')
        i += 1;
    if (hex.size() % 2)
        hex.append('0');
    *index = i;
    return latinText(QByteArray::fromHex(hex));
}

//----------------------------------------------

bool isShowOp(const QByteArray &data, int index)
{
    while (index < data.size()
           && (data.at(index) == ' ' || data.at(index) == '\t'
               || data.at(index) == '\r' || data.at(index) == '\n'))
        index += 1;
    if (index >= data.size())
        return false;
    if (data.at(index) == '\'' || data.at(index) == '"')
        return true;
    return data.mid(index, 2) == "Tj" || data.mid(index, 2) == "TJ";
}

//----------------------------------------------

QString textFromContent(const QByteArray &data)
{
    QString out;
    int i = 0;
    while (i < data.size())
    {
        const char c = data.at(i);
        if (c == '<' && i + 1 < data.size() && data.at(i + 1) == '<')
        {
            i += 2;
            int depth = 1;
            while (i + 1 < data.size() && depth > 0)
            {
                if (data.at(i) == '<' && data.at(i + 1) == '<')
                {
                    depth += 1;
                    i += 2;
                }
                else if (data.at(i) == '>' && data.at(i + 1) == '>')
                {
                    depth -= 1;
                    i += 2;
                }
                else
                    i += 1;
            }
            continue;
        }
        if (c == '[')
        {
            i += 1;
            QString piece;
            int depth = 1;
            while (i < data.size() && depth > 0)
            {
                const char d = data.at(i);
                if (d == '(')
                    piece += takeLiteral(data, &i);
                else if (d == '<' && (i + 1 >= data.size() || data.at(i + 1) != '<'))
                    piece += takeHex(data, &i);
                else if (d == '[')
                {
                    depth += 1;
                    i += 1;
                }
                else if (d == ']')
                {
                    depth -= 1;
                    i += 1;
                }
                else
                    i += 1;
            }
            if (isShowOp(data, i))
            {
                out += piece;
                out += QChar('\n');
            }
            continue;
        }
        if (c == '(')
        {
            const QString piece = takeLiteral(data, &i);
            if (isShowOp(data, i))
            {
                out += piece;
                out += QChar('\n');
            }
            continue;
        }
        if (c == '<' )
        {
            const QString piece = takeHex(data, &i);
            if (isShowOp(data, i))
            {
                out += piece;
                out += QChar('\n');
            }
            continue;
        }
        if (c == 'E' && data.mid(i, 2) == "ET")
        {
            out += QChar('\n');
            i += 2;
            continue;
        }
        if (c == 'T' && i + 1 < data.size() && data.at(i + 1) == '*')
        {
            out += QChar('\n');
            i += 2;
            continue;
        }
        i += 1;
    }
    return out;
}

//----------------------------------------------

bool isDigitByte(char c)
{
    return c >= '0' && c <= '9';
}

//----------------------------------------------

QHash<int, int> lengthObjects(const QByteArray &data)
{
    // A whole-file QString regex stops being reliable once a stream contains NUL.
    QHash<int, int> map;
    int cursor = 0;
    while (cursor < data.size())
    {
        const int at = data.indexOf(" obj", cursor);
        if (at < 0)
            break;
        cursor = at + 4;

        int k = at - 1;
        while (k >= 0 && data.at(k) == ' ')
            k -= 1;
        while (k >= 0 && isDigitByte(data.at(k)))
            k -= 1;
        while (k >= 0 && data.at(k) == ' ')
            k -= 1;
        const int idEnd = k;
        while (k >= 0 && isDigitByte(data.at(k)))
            k -= 1;
        if (k < 0 || (data.at(k) != '\n' && data.at(k) != '\r'))
            continue;
        const int id = data.mid(k + 1, idEnd - k).toInt();

        int p = at + 4;
        while (p < data.size()
               && (data.at(p) == ' ' || data.at(p) == '\n' || data.at(p) == '\r'))
            p += 1;
        const int numStart = p;
        while (p < data.size() && isDigitByte(data.at(p)))
            p += 1;
        if (p == numStart)
            continue;
        const int number = data.mid(numStart, p - numStart).toInt();
        while (p < data.size()
               && (data.at(p) == ' ' || data.at(p) == '\n' || data.at(p) == '\r'))
            p += 1;
        if (data.mid(p, 6) == "endobj")
            map.insert(id, number);
    }
    return map;
}

//----------------------------------------------

int streamLength(const QByteArray &dict, const QHash<int, int> &lengths)
{
    const QRegularExpression refRe(QString::fromLatin1(
        "/Length\\s+(\\d+)\\s+\\d+\\s+R"));
    const QRegularExpressionMatch ref = refRe.match(QString::fromLatin1(dict));
    if (ref.hasMatch())
    {
        const int id = ref.captured(1).toInt();
        if (lengths.contains(id))
            return lengths.value(id);
        return -1;
    }
    const QRegularExpression numRe(QString::fromLatin1("/Length\\s+(\\d+)"));
    const QRegularExpressionMatch num = numRe.match(QString::fromLatin1(dict));
    if (!num.hasMatch())
        return -1;
    return num.captured(1).toInt();
}

//----------------------------------------------

QByteArray decodeStream(const QByteArray &dict, const QByteArray &payload)
{
    const QString header = QString::fromLatin1(dict);
    if (header.contains(QString::fromLatin1("/LZWDecode")))
        return decodeLzw(payload);
    if (header.contains(QString::fromLatin1("/FlateDecode")))
        return decodeFlate(payload);
    if (header.contains(QString::fromLatin1("/Filter")))
        return QByteArray();
    return payload;
}

//----------------------------------------------

QString tidy(const QString &raw)
{
    QString text = raw;
    text.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    text.replace(QChar('\r'), QChar('\n'));
    QStringList lines;
    foreach (QString line, text.split(QChar('\n')))
    {
        line = line.simplified();
        lines.append(line);
    }
    QString out;
    int blanks = 0;
    for (int i = 0; i < lines.size(); ++i)
    {
        if (lines.at(i).isEmpty())
        {
            blanks += 1;
            if (blanks <= 1 && !out.isEmpty())
                out += QChar('\n');
            continue;
        }
        blanks = 0;
        if (!out.isEmpty() && !out.endsWith(QChar('\n')))
            out += QChar('\n');
        out += lines.at(i);
        out += QChar('\n');
    }
    return out.trimmed();
}

} // namespace

//----------------------------------------------

QString extractPdfText(const QByteArray &data, QString *error)
{
    if (data.size() < 5 || !data.startsWith("%PDF"))
    {
        if (error)
            *error = QString::fromUtf8("Not a PDF.");
        return QString();
    }

    const QHash<int, int> lengths = lengthObjects(data);
    QString collected;
    int decoded = 0;
    int pos = 0;
    while (pos < data.size())
    {
        const int at = data.indexOf("stream", pos);
        if (at < 0)
            break;
        pos = at + 6;
        if (at >= 3 && data.mid(at - 3, 3) == "end")
            continue;

        int dictEnd = at;
        while (dictEnd > 0 && (data.at(dictEnd - 1) == ' ' || data.at(dictEnd - 1) == '\n'
                               || data.at(dictEnd - 1) == '\r' || data.at(dictEnd - 1) == '\t'))
            dictEnd -= 1;
        if (dictEnd < 2 || data.mid(dictEnd - 2, 2) != ">>")
            continue;

        int dictStart = data.lastIndexOf("<<", dictEnd - 2);
        if (dictStart < 0 || dictEnd - dictStart > 4000)
            continue;
        const QByteArray dict = data.mid(dictStart, dictEnd - dictStart);
        if (dict.contains("/Subtype /Image") || dict.contains("/Subtype/Image"))
            continue;

        const int length = streamLength(dict, lengths);
        if (length <= 0 || length > kMaxDecoded)
            continue;

        int start = at + 6;
        if (start + 1 < data.size() && data.at(start) == '\r' && data.at(start + 1) == '\n')
            start += 2;
        else if (start < data.size() && (data.at(start) == '\n' || data.at(start) == '\r'))
            start += 1;
        if (start + length > data.size())
            continue;

        const QByteArray payload = data.mid(start, length);
        const QByteArray plain = decodeStream(dict, payload);
        if (plain.isEmpty())
            continue;
        decoded += 1;
        const QString piece = textFromContent(plain);
        if (piece.trimmed().isEmpty())
            continue;
        if (!collected.isEmpty())
            collected += QString::fromUtf8("\n\n");
        collected += piece;
        pos = start + length;
    }

    const QString text = tidy(collected);
    if (text.isEmpty())
    {
        if (error)
        {
            *error = QString::fromUtf8(
                "No text in this PDF (%1 content streams decoded).").arg(decoded);
        }
        return QString();
    }
    return text;
}
