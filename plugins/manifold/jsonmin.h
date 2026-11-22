#ifndef JSONMIN_H
#define JSONMIN_H

//----------------------------------------------

#include <charconv>
#include <cmath>
#include <string>
#include <vector>

//----------------------------------------------

struct Json
{
    enum Kind
    {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object
    };

    Kind        kind;
    bool        boolean;
    double      number;
    std::string text;
    std::vector<Json> items;
    std::vector<std::pair<std::string, Json> > fields;

    Json()
        : kind(Null)
        , boolean(false)
        , number(0)
    {
    }

    const Json *field(const char *key) const
    {
        if (!key)
            return 0;
        for (size_t i = 0; i < fields.size(); ++i)
        {
            if (fields[i].first == key)
                return &fields[i].second;
        }
        return 0;
    }
};

//----------------------------------------------

class JsonParser
{
 public:
    JsonParser(const char *begin, const char *end)
        : _s(begin)
        , _end(end)
    {
    }

    bool parse(Json *out)
    {
        if (!out)
            return fail("Missing JSON value.");
        skip();
        if (!value(out))
            return false;
        skip();
        if (_s != _end)
            return fail("Unexpected data after JSON.");
        return true;
    }

    const std::string &error() const
    {
        return _error;
    }

 private:
    const char *_s;
    const char *_end;
    std::string _error;

    bool fail(const char *text)
    {
        if (_error.empty())
            _error = text ? text : "Invalid JSON.";
        return false;
    }

    void skip()
    {
        while (_s < _end)
        {
            const char c = *_s;
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
                break;
            ++_s;
        }
    }

    bool value(Json *out)
    {
        skip();
        if (_s >= _end)
            return fail("Unexpected end of JSON.");
        const char c = *_s;
        if (c == '{')
            return object(out);
        if (c == '[')
            return array(out);
        if (c == '"')
            return string(out);
        if (c == 't' || c == 'f')
            return boolean(out);
        if (c == 'n')
            return nullValue(out);
        return number(out);
    }

    bool literal(const char *text)
    {
        const char *p = text;
        while (*p)
        {
            if (_s >= _end || *_s != *p)
                return false;
            ++_s;
            ++p;
        }
        return true;
    }

    bool nullValue(Json *out)
    {
        if (!literal("null"))
            return fail("Invalid JSON value.");
        *out = Json();
        return true;
    }

    bool boolean(Json *out)
    {
        if (literal("true"))
        {
            *out = Json();
            out->kind = Json::Bool;
            out->boolean = true;
            return true;
        }
        if (literal("false"))
        {
            *out = Json();
            out->kind = Json::Bool;
            out->boolean = false;
            return true;
        }
        return fail("Invalid JSON value.");
    }

    bool number(Json *out)
    {
        const char *start = _s;
        if (_s < _end && *_s == '-')
            ++_s;
        if (_s >= _end || *_s < '0' || *_s > '9')
            return fail("Invalid JSON number.");
        while (_s < _end && *_s >= '0' && *_s <= '9')
            ++_s;
        if (_s < _end && *_s == '.')
        {
            ++_s;
            if (_s >= _end || *_s < '0' || *_s > '9')
                return fail("Invalid JSON number.");
            while (_s < _end && *_s >= '0' && *_s <= '9')
                ++_s;
        }
        if (_s < _end && (*_s == 'e' || *_s == 'E'))
        {
            ++_s;
            if (_s < _end && (*_s == '+' || *_s == '-'))
                ++_s;
            if (_s >= _end || *_s < '0' || *_s > '9')
                return fail("Invalid JSON number.");
            while (_s < _end && *_s >= '0' && *_s <= '9')
                ++_s;
        }
        double number = 0;
        const std::from_chars_result parsed =
            std::from_chars(start, _s, number);
        if (parsed.ec != std::errc() || parsed.ptr != _s)
            return fail("Invalid JSON number.");
        *out = Json();
        out->kind = Json::Number;
        out->number = number;
        return true;
    }

    bool string(Json *out)
    {
        if (_s >= _end || *_s != '"')
            return fail("Expected a string.");
        ++_s;
        std::string text;
        while (_s < _end)
        {
            const unsigned char c = (unsigned char)*_s;
            if (c == '"')
            {
                ++_s;
                *out = Json();
                out->kind = Json::String;
                out->text.swap(text);
                return true;
            }
            if (c == '\\')
            {
                ++_s;
                if (_s >= _end)
                    return fail("Bad string escape.");
                const char e = *_s++;
                if (e == '"' || e == '\\' || e == '/')
                    text.push_back(e);
                else if (e == 'b')
                    text.push_back('\b');
                else if (e == 'f')
                    text.push_back('\f');
                else if (e == 'n')
                    text.push_back('\n');
                else if (e == 'r')
                    text.push_back('\r');
                else if (e == 't')
                    text.push_back('\t');
                else if (e == 'u')
                {
                    if (!appendUnicode(&text))
                        return false;
                }
                else
                    return fail("Bad string escape.");
                continue;
            }
            if (c < 0x20)
                return fail("Unescaped control character in string.");
            text.push_back((char)c);
            ++_s;
        }
        return fail("Unterminated string.");
    }

    bool appendUnicode(std::string *text)
    {
        unsigned code = 0;
        if (!hex4(&code))
            return false;
        if (code >= 0xD800 && code <= 0xDBFF)
        {
            if (_s + 2 > _end || _s[0] != '\\' || _s[1] != 'u')
                return fail("Bad unicode escape.");
            _s += 2;
            unsigned low = 0;
            if (!hex4(&low) || low < 0xDC00 || low > 0xDFFF)
                return fail("Bad unicode escape.");
            code = 0x10000 + (((code - 0xD800) << 10) | (low - 0xDC00));
        }
        else if (code >= 0xDC00 && code <= 0xDFFF)
            return fail("Bad unicode escape.");
        appendUtf8(text, code);
        return true;
    }

    bool hex4(unsigned *code)
    {
        unsigned value = 0;
        for (int i = 0; i < 4; ++i)
        {
            if (_s >= _end)
                return fail("Bad unicode escape.");
            const char c = *_s++;
            value <<= 4;
            if (c >= '0' && c <= '9')
                value += (unsigned)(c - '0');
            else if (c >= 'a' && c <= 'f')
                value += (unsigned)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F')
                value += (unsigned)(c - 'A' + 10);
            else
                return fail("Bad unicode escape.");
        }
        *code = value;
        return true;
    }

    void appendUtf8(std::string *text, unsigned code)
    {
        if (code < 0x80)
            text->push_back((char)code);
        else if (code < 0x800)
        {
            text->push_back((char)(0xC0 | (code >> 6)));
            text->push_back((char)(0x80 | (code & 0x3F)));
        }
        else if (code < 0x10000)
        {
            text->push_back((char)(0xE0 | (code >> 12)));
            text->push_back((char)(0x80 | ((code >> 6) & 0x3F)));
            text->push_back((char)(0x80 | (code & 0x3F)));
        }
        else
        {
            text->push_back((char)(0xF0 | (code >> 18)));
            text->push_back((char)(0x80 | ((code >> 12) & 0x3F)));
            text->push_back((char)(0x80 | ((code >> 6) & 0x3F)));
            text->push_back((char)(0x80 | (code & 0x3F)));
        }
    }

    bool array(Json *out)
    {
        if (_s >= _end || *_s != '[')
            return fail("Expected an array.");
        ++_s;
        *out = Json();
        out->kind = Json::Array;
        skip();
        if (_s < _end && *_s == ']')
        {
            ++_s;
            return true;
        }
        while (_s < _end)
        {
            Json item;
            if (!value(&item))
                return false;
            out->items.push_back(item);
            skip();
            if (_s < _end && *_s == ',')
            {
                ++_s;
                continue;
            }
            if (_s < _end && *_s == ']')
            {
                ++_s;
                return true;
            }
            return fail("Expected ',' or ']'.");
        }
        return fail("Unterminated array.");
    }

    bool object(Json *out)
    {
        if (_s >= _end || *_s != '{')
            return fail("Expected an object.");
        ++_s;
        *out = Json();
        out->kind = Json::Object;
        skip();
        if (_s < _end && *_s == '}')
        {
            ++_s;
            return true;
        }
        while (_s < _end)
        {
            skip();
            Json key;
            if (!string(&key))
                return false;
            skip();
            if (_s >= _end || *_s != ':')
                return fail("Expected ':'.");
            ++_s;
            Json item;
            if (!value(&item))
                return false;
            out->fields.push_back(std::make_pair(key.text, item));
            skip();
            if (_s < _end && *_s == ',')
            {
                ++_s;
                continue;
            }
            if (_s < _end && *_s == '}')
            {
                ++_s;
                return true;
            }
            return fail("Expected ',' or '}'.");
        }
        return fail("Unterminated object.");
    }
};

//----------------------------------------------

inline bool parseJson(const char *text, Json *out, std::string *error)
{
    const char *begin = text ? text : "";
    JsonParser parser(begin, begin + std::char_traits<char>::length(begin));
    if (!parser.parse(out) || out->kind != Json::Object)
    {
        if (error)
            *error = parser.error().empty()
                         ? "Arguments must be a JSON object."
                         : parser.error();
        return false;
    }
    return true;
}

//----------------------------------------------

#endif // JSONMIN_H
