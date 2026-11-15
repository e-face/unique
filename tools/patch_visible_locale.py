# -*- coding: utf-8 -*-
import io


def unescape(text):
    return text.replace(u"\\\\", u"\x01").replace(u"\\n", u"\n").replace(u"\x01", u"\\")


def escape(text):
    return text.replace(u"\\", u"\\\\").replace(u"\n", u"\\n")


def load(path):
    text = io.open(path, "r", encoding="utf-8").read()
    if text.startswith(u"\ufeff"):
        text = text[1:]
    lines = text.split(u"\n")
    if lines and lines[-1] == u"":
        lines = lines[:-1]
    return lines


def save(path, lines):
    io.open(path, "w", encoding="utf-8").write(u"\n".join(lines) + u"\n")


def apply(path, index):
    lines = load(path)
    seen = {}
    out = []
    replaced = 0
    for line in lines:
        if (not line) or line.startswith(u"#") or line.startswith(u";") or u"|" not in line:
            out.append(line)
            continue
        sep = line.find(u"|")
        src = unescape(line[:sep])
        if src in index:
            out.append(escape(src) + u"|" + escape(index[src]))
            seen[src] = True
            replaced += 1
        else:
            out.append(line)
            seen[src] = True
    added = 0
    for src in index:
        if src not in seen:
            out.append(escape(src) + u"|" + escape(index[src]))
            added += 1
    save(path, out)
    return replaced, added


zh_fix = {
    u"Prompt": u"提示",
    u"Completion": u"补全",
    u"prompt %1  ·  completion %2  ·  кеш префикса %3%":
        u"提示 %1  ·  补全 %2  ·  前缀缓存 %3%",
    u"Токены ответа: prompt %1, completion %2, всего %3":
        u"回复令牌：提示 %1，补全 %2，合计 %3",
    u"Всего: запросов %1, prompt %2, completion %3, всего %4":
        u"总计：请求 %1，提示 %2，补全 %3，合计 %4",
    u"Проект: запросов %1, prompt %2, completion %3, всего %4":
        u"项目：请求 %1，提示 %2，补全 %3，合计 %4",
}

shared = [
    (u"Закрыть", u"Close", u"关闭"),
    (u"ОК", u"OK", u"确定"),
    (u"Да", u"Yes", u"是"),
    (u"Нет", u"No", u"否"),
    (u"не удалось распознать баланс в ответе API (задайте JSON путь баланса в профиле)",
     u"Could not read a balance from the API response (set the balance JSON path on the profile)",
     u"无法从 API 响应中识别余额（请在配置中设置余额 JSON 路径）"),
    (u"в JSON нет значения по пути «%1»",
     u"No value in JSON at path \"%1\"",
     u"JSON 中路径“%1”没有值"),
]

en = {}
zh = {}
for src, e, z in shared:
    en[src] = e
    zh[src] = z
for src, z in zh_fix.items():
    zh[src] = z

root = "bin/config/strings"
er, ea = apply(root + "/en/gui_by_source.ini", en)
zr, za = apply(root + "/zh/gui_by_source.ini", zh)
io.open("tools/_loc_apply.txt", "w", encoding="utf-8").write(
    u"en replaced %d added %d\nzh replaced %d added %d\n" % (er, ea, zr, za))
