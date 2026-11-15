# -*- coding: utf-8 -*-
from __future__ import unicode_literals
"""Extract tr("...") from src and build gui_by_source.ini (pipe-separated)."""
import re
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "src")

# One literal, or several adjacent "..." / '...' strings inside tr(...).
TR_CALL_RE = re.compile(
    r'\btr\s*\(\s*((?:"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\')(?:\s*(?:"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'))*)',
    re.UNICODE,
)
STR_LIT_RE = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', re.UNICODE)

# Manual EN/ZH for high-traffic UI (Russian source -> translation)
EN_MAP = {
    "Файл": "File",
    "Окно": "Window",
    "Проект": "Project",
    "Справка": "Help",
    "Открыть…": "Open…",
    "Новый проект…": "New project…",
    "Открыть последний проект": "Open last project",
    "Недавние проекты": "Recent projects",
    "Очистить всё": "Clear all",
    "Баланс…": "Balance…",
    "Выход": "Exit",
    "Главный чат": "Main chat",
    "Команда": "Team",
    "Журнал": "Journal",
    "Настройки": "Settings",
    "Настройки проекта…": "Project settings…",
    "Статистика": "Statistics",
    "Панель": "Toolbar",
    "Отправить": "Send",
    "Стоп": "Stop",
    "Контекст": "Context",
    "Сжать контекст": "Compact context",
    "Прервать": "Abort",
    "Отчёт родителю": "Report to parent",
    "Разрешить": "Allow",
    "Всегда": "Always",
    "Отклонить": "Reject",
    "Удалить воркера": "Remove worker",
    "Удалить инструмент": "Remove tool",
    "Открыть чат": "Open chat",
    "Профиль модели": "Model profile",
    "Прервать воркер": "Stop worker",
    "Отправить отчёт родителю": "Send report to parent",
    "Отмена": "Cancel",
    "Удалить": "Delete",
    "Готово": "Done",
    "Продолжить": "Continue",
    "Завершить": "Finish",
    "Язык": "Language",
    "Русский": "Russian",
    "English": "English",
    "中文": "Chinese",
}

ZH_MAP = {
    "Файл": "文件",
    "Окно": "窗口",
    "Проект": "项目",
    "Справка": "帮助",
    "Открыть…": "打开…",
    "Новый проект…": "新建项目…",
    "Открыть последний проект": "打开上次项目",
    "Недавние проекты": "最近的项目",
    "Очистить всё": "全部清除",
    "Баланс…": "余额…",
    "Выход": "退出",
    "Главный чат": "主聊天",
    "Команда": "团队",
    "Журнал": "日志",
    "Настройки": "设置",
    "Настройки проекта…": "项目设置…",
    "Статистика": "统计",
    "Панель": "工具栏",
    "Отправить": "发送",
    "Стоп": "停止",
    "Контекст": "上下文",
    "Сжать контекст": "压缩上下文",
    "Прервать": "中断",
    "Отчёт родителю": "向父级报告",
    "Разрешить": "允许",
    "Всегда": "始终",
    "Отклонить": "拒绝",
    "Удалить воркера": "删除工作者",
    "Удалить инструмент": "删除工具",
    "Открыть чат": "打开聊天",
    "Профиль модели": "模型配置",
    "Прервать воркер": "停止工作者",
    "Отправить отчёт родителю": "向父级发送报告",
    "Отмена": "取消",
    "Удалить": "删除",
    "Готово": "完成",
    "Продолжить": "继续",
    "Завершить": "结束",
    "Язык": "语言",
    "Русский": "俄语",
    "English": "英语",
    "中文": "中文",
}

try:
    from gui_by_source_translations import EN_EXTRA, ZH_EXTRA
    EN_MAP.update(EN_EXTRA)
    ZH_MAP.update(ZH_EXTRA)
except ImportError:
    pass


def decode_c(s):
    if "\\" not in s:
        return s
    out = []
    i = 0
    while i < len(s):
        if s[i] == "\\" and i + 1 < len(s):
            n = s[i + 1]
            if n == "n":
                out.append("\n")
                i += 2
                continue
            if n == "t":
                out.append("\t")
                i += 2
                continue
            if n == "r":
                out.append("\r")
                i += 2
                continue
            if n == "\\":
                out.append("\\")
                i += 2
                continue
            if n == '"':
                out.append('"')
                i += 2
                continue
        out.append(s[i])
        i += 1
    return "".join(out)


def join_c_string_literals(blob):
    """Join adjacent C string literals the way the compiler does."""
    parts = []
    for lit in STR_LIT_RE.findall(blob):
        inner = lit[1:-1]
        parts.append(decode_c(inner))
    return "".join(parts)


def collect_tr_strings():
    found = set()
    names = [n for n in os.listdir(SRC) if n.endswith(".cpp") or n.endswith(".h")]
    for name in sorted(names):
        path = os.path.join(SRC, name)
        with open(path, "rb") as f:
            text = f.read().decode("utf-8")
        for m in TR_CALL_RE.finditer(text):
            joined = join_c_string_literals(m.group(1))
            if joined:
                found.add(joined)
    return sorted(found, key=lambda x: (len(x), x))


def write_by_source(path, mapping, strings):
    lines = ["# source|translation — legacy tr() map", ""]
    for src in strings:
        if src == "\u2014" or "\n" in src or "\r" in src:
            continue
        if src in mapping:
            dst = mapping[src]
        elif mapping is EN_MAP:
            dst = src
        else:
            dst = ZH_MAP.get(src, src)
        line = src.replace("|", "\\|") + "|" + dst.replace("|", "\\|")
        if "\n" in line:
            continue
        lines.append(line)
    d = os.path.dirname(path)
    if not os.path.isdir(d):
        os.makedirs(d)
    with open(path, "wb") as f:
        f.write(("\n".join(lines) + "\n").encode("utf-8"))


def main():
    strings = collect_tr_strings()
    out_en = os.path.join(ROOT, "bin", "config", "strings", "en", "gui_by_source.ini")
    out_zh = os.path.join(ROOT, "bin", "config", "strings", "zh", "gui_by_source.ini")
    write_by_source(out_en, EN_MAP, strings)
    write_by_source(out_zh, ZH_MAP, strings)
    ru_stub = os.path.join(ROOT, "bin", "config", "strings", "ru", "gui_by_source.ini")
    d = os.path.dirname(ru_stub)
    if not os.path.isdir(d):
        os.makedirs(d)
    with open(ru_stub, "wb") as f:
        f.write("# Russian UI: tr() sources are identity; map unused.\n".encode("utf-8"))
    print("tr strings:", len(strings))
    print("wrote", out_en, out_zh)


if __name__ == "__main__":
    main()
