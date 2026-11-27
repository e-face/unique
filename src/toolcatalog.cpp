#include "toolcatalog.h"
//----------------------------------------------
#include "agent.h"
#include "agenttool.h"
#include "mcphub.h"
#include "toolplugin.h"
//----------------------------------------------
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibrary>
#include <QMutex>
#include <QMutexLocker>

//----------------------------------------------

namespace
{

const int kPluginOutputBytes = 256 * 1024;

struct PluginTool
{
    QString            id;
    QString            description;
    QJsonObject        schema;
    UniqueToolInvokeFn invoke;
};

struct LoadedLibrary
{
    QLibrary          *library;
    QList<PluginTool>  tools;
};

QList<LoadedLibrary> gLibraries;
QMutex gLoadMutex;

//----------------------------------------------

class PluginAgentTool : public AgentTool
{
public:
    PluginAgentTool(const QString &id, const QString &description,
                    const QJsonObject &schema, UniqueToolInvokeFn invoke,
                    QObject *parent)
        : AgentTool(parent)
        , _id(id)
        , _description(description)
        , _schema(schema)
        , _invoke(invoke)
    {
    }

    virtual QString name() const
    {
        return _id;
    }

    virtual QString description() const
    {
        return _description;
    }

    virtual QJsonObject parametersSchema() const
    {
        return _schema;
    }

    virtual QString invoke(QString argumentsJson, bool *ok)
    {
        if (!_invoke)
        {
            if (ok)
                *ok = false;
            return QString::fromUtf8("Plugin tool has no entry point.");
        }
        QByteArray output(kPluginOutputBytes, 0);
        int callOk = 0;
        QByteArray args = argumentsJson.toUtf8();
        // Plugins only see the arguments string. Pass the open project so a
        // relative path in the call resolves to a project file.
        QJsonParseError parseError;
        const QJsonDocument incoming = QJsonDocument::fromJson(args, &parseError);
        if (parseError.error == QJsonParseError::NoError && incoming.isObject())
        {
            QJsonObject obj = incoming.object();
            Agent *agent = qobject_cast<Agent *>(parent());
            if (agent)
            {
                const QString root = agent->projectDirectory().trimmed();
                if (!root.isEmpty())
                    obj.insert(QString::fromUtf8("project_root"), root);
            }
            args = QJsonDocument(obj).toJson(QJsonDocument::Compact);
        }
        const int rc = _invoke(args.constData(), output.data(), output.size(), &callOk);
        if (rc != 0)
        {
            if (ok)
                *ok = false;
            if (rc > 0)
                return QString::fromUtf8("Plugin tool reply does not fit the buffer.");
            const QString message = QString::fromUtf8(output.constData()).trimmed();
            return message.isEmpty()
                   ? QString::fromUtf8("Plugin tool failed.")
                   : message;
        }
        if (ok)
            *ok = (callOk != 0);
        return QString::fromUtf8(output.constData());
    }

private:
    QString            _id;
    QString            _description;
    QJsonObject        _schema;
    UniqueToolInvokeFn _invoke;
};

//----------------------------------------------

struct BuiltinRow
{
    const char *id;
    const char *title;
    const char *description;
    bool        decisionsOnly;
};

const BuiltinRow kBuiltins[] = {
    { "read_file", "Чтение файла",
      "Читает текстовые файлы проекта. За один вызов можно передать несколько путей. "
      "Для длинного файла задаются первая строка и число строк; ответ подсказывает, как читать дальше.",
      false },
    { "list_directory", "Список каталога",
      "Показывает файлы и папки одного каталога проекта, без входа во вложенные. "
      "Пустой путь или точка — корень проекта.",
      false },
    { "search_files", "Поиск в проекте",
      "Ищет точную подстроку по файлам проекта. Можно указать папку, маску имён "
      "(например *.cpp;*.h) и до восьми строк сразу. Возвращает до 200 совпадений "
      "с несколькими строками вокруг. PDF не просматривает.",
      false },
    { "find_in_files", "Поиск в файлах",
      "Ищет в явно названных файлах (не больше 32) точную подстроку или регулярное выражение. "
      "Выражение применяется к каждой строке отдельно. До 200 совпадений.",
      false },
    { "create_file", "Создание файла",
      "Создаёт новые текстовые файлы в проекте. За один вызов можно передать несколько пар "
      "«путь и содержимое». Путь вне проекта требует подтверждения. Черновики кладутся в ___project___/tmp.",
      false },
    { "write_file", "Запись файла",
      "Записывает или полностью перезаписывает текстовые файлы проекта. "
      "Несколько файлов за один вызов. Путь вне проекта требует подтверждения.",
      false },
    { "replace_in_file", "Замена в файле",
      "Заменяет точный фрагмент в одном файле проекта. Если фрагмент встречается больше одного раза, "
      "файл не меняется, пока не включена замена всех вхождений. Пустая новая строка удаляет фрагмент. "
      "Если фрагмента нет, файл остаётся как был.",
      false },
    { "delete_file", "Удаление файла",
      "Удаляет файл. Каталог не удаляет. Путь вне проекта требует подтверждения.",
      false },
    { "rename_file", "Переименование файла",
      "Переименовывает или перемещает файл. Существующий файл не перезаписывает. "
      "Каталоги не переименовывает.",
      false },
    { "read_pdf", "Текст PDF",
      "Достаёт текст из PDF в проекте или по адресу http(s). Понимает сжатие Flate и LZW. "
      "Можно взять кусок: с какого символа и сколько. Скачивание по адресу спрашивает разрешение "
      "так же, как поиск в сети.",
      false },
    { "pdf_page_image", "Картинка страницы PDF",
      "Рисует страницы PDF в PNG с разрешением от 36 до 300 точек на дюйм. "
      "Страницы задаются номером, диапазоном 1-3 или списком 1,4; за раз не больше 12. "
      "Файлы попадают в указанную папку или в ___project___/tmp, и их можно показать в чате картинкой.",
      false },
    { "run_command", "Команда",
      "Запускает команды оболочки. Несколько независимых команд можно передать за один вызов. "
      "Программу и пути вне проекта подтверждает пользователь. Если программа ждёт строку ввода, "
      "последние строки показываются пользователю, и его ответ уходит ей на вход. "
      "Для оконных программ и серверов включается фоновый режим.",
      false },
    { "request_permission", "Запрос разрешения",
      "Спрашивает у пользователя разрешение до действия: поиск в сети, если его выполняет не сам "
      "инструмент, а также удаление, переименование, перезапись и другие опасные изменения файлов. "
      "Ответ — разрешено или отклонено. Вариант «Всегда» запоминает программу.",
      false },
    { "web_search", "Поиск в сети",
      "Ищет в интернете и возвращает заголовки, адреса и короткие выдержки. "
      "Выдержка без числа, даты или факта страницу не заменяет: её читают отдельным вызовом. "
      "Поисковик задаётся вызовом: DuckDuckGo, Bing, Google или Яндекс. Результатов от 1 до 30. "
      "Первый вызов подтверждает пользователь. Если выдача не разобрана, это не значит, что в сети пусто.",
      false },
    { "read_page", "Текст страницы",
      "Читает одну страницу http(s) как текст. Адрес берут из выдачи поиска, а не саму страницу выдачи. "
      "Первый вызов подтверждает пользователь, и вариант «Всегда» затем покрывает поиск, это чтение и скачивание PDF. "
      "Можно указать, с какого символа читать и сколько. Заголовок и короткий отрывок фактом не являются.",
      false },
    { "plan_work", "План работы",
      "Записывает план текущей задачи, после чего план выполняется. Для каждой части указывается, "
      "делать её самому, отдать дочернему воркеру или оформить повторяющийся шаг инструментом. "
      "Если этого вызова нет в специализации, шаг плана пропускается.",
      false },
    { "delegate_task", "Делегирование",
      "Создаёт дочернего воркера для части, которую дешевле вести отдельным чатом: большой "
      "независимый кусок, нехватка контекста, другая модальность (картинка, речь, звук, видео) "
      "или независимая проверка, если её просил пользователь. Короткий поиск остаётся здесь. "
      "Уже завершённого дочернего можно вызвать снова.",
      false },
    { "list_child_workers", "Дочерние воркеры",
      "Список прямых дочерних воркеров: идентификатор, тип, состояние и задача. "
      "Идентификатор нужен, чтобы продолжить того же воркера, в том числе уже завершившего работу.",
      false },
    { "get_child_result", "Результат дочернего",
      "Полный отчёт одного прямого дочернего воркера. Краткий отчёт приходит и сам, "
      "но может быть обрезан; этот вызов читает его целиком.",
      false },
    { "dismiss_child_worker", "Закрытие дочернего",
      "Убирает завершённого или временного прямого дочернего воркера вместе с его потомками. "
      "Воркер с ещё идущим заданием не закрывается.",
      false },
    { "create_tool", "Создать инструмент",
      "Создаёт общий инструмент для узкой работы, которая будет повторяться: скачать, разобрать, "
      "собрать, проверить, преобразовать, массово поправить. Одноразовый шаг так не оформляется. "
      "Удалить созданный инструмент может только пользователь.",
      false },
    { "run_tool", "Запуск инструмента",
      "Запускает общий инструмент и ждёт его отчёт. Одновременно его вызывает один воркер, "
      "остальные ждут в очереди. Текстовому инструменту передаётся задача словами. "
      "Инструменту картинки, звука, видео, речи или эмбеддингов передаётся один объект JSON "
      "с его параметрами, а не инструкция чата.",
      false },
    { "complete_task", "Завершение задачи",
      "Дочерний воркер сообщает родителю, что его часть сделана. У корневого воркера этого вызова нет: "
      "пользователю он отвечает сообщением в чате. Для независимой проверки в отчёт входят команда, "
      "вывод и признак, что проект не менялся.",
      false },
    { "add_group_member", "Добавить в группу",
      "Добавляет собеседника в группу обсуждения и создаёт группу, если её ещё нет. "
      "У каждого участника должен быть свой профиль модели. Участники говорят по очереди "
      "и не создают дочерних воркеров.",
      false },
    { "list_group_members", "Участники группы",
      "Список участников своей группы: имя, тип, роль, идентификатор и состояние.",
      false },
    { "group_speak", "Реплика в группе",
      "Ведущий отвечает фактами из проекта или со страницы, открытой после поиска. "
      "Заголовок и короткий отрывок фактом не считаются. Не повторяй себя.",
      false },
    { "group_await", "Ожидание группы",
      "Участник, которому сейчас не нужно говорить или голосовать, ждёт своего хода. "
      "Остальных это не останавливает. Ведущий и тот, кто должен голосовать, этим вызовом не паркуются.",
      false },
    { "group_end_turn", "Конец хода",
      "Передаёт ход другой стороне. Обсуждение продолжается до согласия. "
      "После единогласного голосования новый круг не открывается.",
      false },
    { "group_propose", "Предложение группе",
      "Главный ставит общий ответ на голосование, когда согласие уже есть. "
      "Отказ продолжает обсуждение. После единогласного решения новый круг не открывается.",
      false },
    { "group_vote", "Голос в группе",
      "Голос по текущему предложению. Согласие всех завершает обсуждение. "
      "Отказ возвращает ход к спору, пока не будет согласия.",
      false },
    { "ask_user", "Вопрос пользователю",
      "Задаёт пользователю один вопрос и ждёт ответ. Несколько неизвестных перечисляются "
      "в этом же вопросе. Пустой ответ или отмена значит, что деталь всё ещё неизвестна: "
      "эту часть не заполняют догадкой.",
      false },
    { "read_settings", "Чтение настроек",
      "Читает настройки программы или проекта: лимиты воркеров, флаги контроля действий, "
      "всегда разрешённые команды, профили моделей, пресеты и серверы MCP. "
      "Ключ API не показывается, только факт, что он задан. "
      "Серверы MCP (их могут назвать NCP) — это серверы инструментов программы, не профили моделей. "
      "Каждое чтение подтверждает пользователь.",
      false },
    { "edit_settings", "Правка настроек",
      "Меняет настройки программы или проекта, если профиль или параметр не подходит задаче. "
      "Сначала настройки читаются. Пользователь видит каждое поле как старое значение и новое "
      "и подтверждает запись. Ключ API прочитать и записать нельзя. "
      "Серверы MCP правятся только в настройках программы: их не выдумывают и порты не сканируют.",
      false },
    { "propose_decision", "Предложение решения",
      "Записывает структурированное решение: какие варианты рассмотрены, какой выбран, "
      "почему и с какой уверенностью. Результат сохраняется в проекте. "
      "Вызов есть только у воркера класса Decisions; галочка на обычном текстовом воркере его не добавляет.",
      true }
};

//----------------------------------------------

bool validToolId(const QString &id)
{
    if (id.isEmpty() || id.size() > 64)
        return false;
    const QChar first = id.at(0);
    if (!(first.isLetter() || first == QChar('_')))
        return false;
    for (int i = 1; i < id.size(); ++i)
    {
        const QChar c = id.at(i);
        if (!(c.isLetterOrNumber() || c == QChar('_')))
            return false;
    }
    return true;
}

//----------------------------------------------

bool loadOneLibrary(const QString &path, QList<ToolOffer> *offers)
{
    if (!offers)
        return false;
    QLibrary *library = new QLibrary(path);
    library->setLoadHints(QLibrary::PreventUnloadHint);
    if (!library->load())
    {
        delete library;
        return false;
    }
    typedef int (*AbiFn)();
    typedef const char *(*TextFn)();
    typedef int (*CountFn)();
    typedef int (*ToolFn)(int, UniqueToolSpec *);
    AbiFn abi = (AbiFn)library->resolve("unique_plugin_abi");
    TextFn libraryId = (TextFn)library->resolve("unique_plugin_id");
    TextFn libraryTitle = (TextFn)library->resolve("unique_plugin_title");
    CountFn countFn = (CountFn)library->resolve("unique_plugin_tool_count");
    ToolFn toolFn = (ToolFn)library->resolve("unique_plugin_tool");
    if (!abi || abi() != UNIQUE_TOOL_PLUGIN_ABI || !libraryId || !countFn || !toolFn)
    {
        library->unload();
        delete library;
        return false;
    }
    const QString idText = QString::fromUtf8(libraryId()).trimmed();
    QString titleText;
    if (libraryTitle && libraryTitle())
        titleText = QString::fromUtf8(libraryTitle()).trimmed();
    if (titleText.isEmpty())
        titleText = idText;
    if (idText.isEmpty())
    {
        library->unload();
        delete library;
        return false;
    }

    LoadedLibrary loaded;
    loaded.library = library;
    const int count = countFn();
    for (int i = 0; i < count; ++i)
    {
        UniqueToolSpec spec;
        spec.id = 0;
        spec.title = 0;
        spec.description = 0;
        spec.schemaJson = 0;
        spec.invoke = 0;
        if (!toolFn(i, &spec) || !spec.id || !spec.invoke)
            continue;
        const QString toolId = QString::fromUtf8(spec.id).trimmed();
        if (!validToolId(toolId))
            continue;
        bool clash = false;
        foreach (const ToolOffer &have, *offers)
        {
            if (have.id == toolId)
            {
                clash = true;
                break;
            }
        }
        if (clash)
            continue;
        ToolOffer offer;
        offer.id = toolId;
        offer.title = spec.title ? QString::fromUtf8(spec.title).trimmed() : toolId;
        if (offer.title.isEmpty())
            offer.title = toolId;
        offer.description = spec.description
                            ? QString::fromUtf8(spec.description).trimmed()
                            : QString();
        offer.libraryId = idText;
        offer.libraryTitle = titleText;
        offer.builtin = false;
        offer.decisionsOnly = false;
        offers->append(offer);

        PluginTool tool;
        tool.id = toolId;
        tool.description = spec.description
                           ? QString::fromUtf8(spec.description).trimmed()
                           : offer.title;
        const QByteArray schemaBytes = spec.schemaJson
                                       ? QByteArray(spec.schemaJson)
                                       : QByteArray("{}");
        const QJsonDocument doc = QJsonDocument::fromJson(schemaBytes);
        tool.schema = doc.isObject() ? doc.object() : QJsonObject();
        tool.invoke = spec.invoke;
        loaded.tools.append(tool);
    }
    if (loaded.tools.isEmpty())
    {
        library->unload();
        delete library;
        return false;
    }
    gLibraries.append(loaded);
    return true;
}

}

//----------------------------------------------

ToolCatalog *ToolCatalog::instance()
{
    static ToolCatalog *catalog = new ToolCatalog();
    return catalog;
}

//----------------------------------------------

ToolCatalog::ToolCatalog()
    : _loaded(false)
{
}

//----------------------------------------------

void ToolCatalog::ensureLoaded()
{
    QMutexLocker lock(&gLoadMutex);
    if (_loaded)
        return;
    _loaded = true;
    const int builtinCount = int(sizeof(kBuiltins) / sizeof(kBuiltins[0]));
    for (int i = 0; i < builtinCount; ++i)
    {
        ToolOffer offer;
        offer.id = QString::fromUtf8(kBuiltins[i].id);
        offer.title = QString::fromUtf8(kBuiltins[i].title);
        offer.description = QString::fromUtf8(kBuiltins[i].description);
        offer.builtin = true;
        offer.decisionsOnly = kBuiltins[i].decisionsOnly;
        _offers.append(offer);
    }

    const QDir dir(QCoreApplication::applicationDirPath()
                   + QString::fromUtf8("/plugins"));
    if (!dir.exists())
        return;
    const QStringList names = dir.entryList(
        QStringList() << QString::fromUtf8("*.dll")
                      << QString::fromUtf8("*.so")
                      << QString::fromUtf8("*.dylib"),
        QDir::Files);
    foreach (const QString &name, names)
        loadOneLibrary(QFileInfo(dir.filePath(name)).absoluteFilePath(), &_offers);
}

//----------------------------------------------

QList<ToolOffer> ToolCatalog::offers()
{
    ensureLoaded();
    QList<ToolOffer> all = _offers;
    all.append(McpHub::instance()->toolOffers());
    return all;
}

//----------------------------------------------

QStringList ToolCatalog::builtinIds()
{
    ensureLoaded();
    QStringList ids;
    foreach (const ToolOffer &offer, _offers)
    {
        if (offer.builtin)
            ids.append(offer.id);
    }
    return ids;
}

//----------------------------------------------

void ToolCatalog::addPlugins(Agent *agent, const QStringList &enabledIds)
{
    if (!agent)
        return;
    ensureLoaded();
    foreach (const QString &id, enabledIds)
    {
        if (agent->hasTool(id))
            continue;
        PluginTool found;
        bool have = false;
        foreach (const LoadedLibrary &library, gLibraries)
        {
            foreach (const PluginTool &tool, library.tools)
            {
                if (tool.id != id)
                    continue;
                found = tool;
                have = true;
                break;
            }
            if (have)
                break;
        }
        if (!have)
            continue;
        agent->addTool(new PluginAgentTool(found.id, found.description,
                                           found.schema, found.invoke, agent));
    }
}
