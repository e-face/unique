# -*- coding: utf-8 -*-
"""Append and replace tr() translations in the en and zh gui_by_source.ini files."""
import io
import os
import sys

ROOT = os.path.join(os.path.dirname(__file__), "..", "bin", "config", "strings")


def R(src, en, zh):
    ROWS.append((src, en, zh))


ROWS = []

R(u"Убрал из контекста файлы: %1",
  u"Removed files from context: %1",
  u"已从上下文移除文件：%1")
R(u"—", u"—", u"—")
R(u"Одобрять запросы подтверждения от моделей без вопроса",
  u"Approve model confirmation requests without asking",
  u"自动批准模型的确认请求，不再询问")
R(u"Разрешить действие?\n%1",
  u"Allow this action?\n%1",
  u"允许此操作？\n%1")
R(u"Команда ждёт ввод в консоли:\n%1",
  u"The command is waiting for console input:\n%1",
  u"命令正在等待控制台输入：\n%1")
R(u"Работа прервана. Можно продолжить или завершить этот воркер.\n%1",
  u"Work was interrupted. You can continue or finish this worker.\n%1",
  u"工作已中断。可以继续，或结束此工作者。\n%1")
R(u"Бюджет вызовов функций исчерпан (%1).\nСбросить лимиты и продолжить или завершить?",
  u"Tool-call budget is exhausted (%1).\nReset the limits and continue, or finish?",
  u"函数调用额度已用尽（%1）。\n重置限额并继续，还是结束？")
R(u"план", u"plan", u"计划")
R(u"список инструментов", u"tool list", u"工具列表")
R(u"список воркеров", u"worker list", u"工作者列表")
R(u"Родитель", u"Parent", u"父级")
R(u"Очистить проект до состояния нового?\nБудут удалены чаты, воркеры, журнал сеанса, база знаний, статистика токенов и настройки проекта в ___project___\n(пресеты и разрешения снова возьмутся из настроек программы).\nИсходники рабочей папки и ключи API не изменятся.",
  u"Clear the project back to a new state?\nChats, workers, the session journal, the knowledge base, token statistics and project settings in ___project___ will be deleted.\n(Presets and permissions will be taken from the application settings again.)\nWorkspace sources and API keys will not change.",
  u"把项目清回新建状态？\n将删除聊天、工作者、会话日志、知识库、令牌统计以及 ___project___ 中的项目设置。\n（预设和权限会重新取自程序设置。）\n工作目录中的源文件和 API 密钥不会改变。")
R(u"Не удалось убрать папку ___project___.\nЗакройте программы, которые держат файлы проекта, и повторите.",
  u"Could not remove the ___project___ folder.\nClose programs that keep project files open, and try again.",
  u"无法删除 ___project___ 文件夹。\n请关闭占用项目文件的程序后重试。")
R(u"Проект больше недоступен:\n%1\nПапка удалена или в ней нет ___project___.",
  u"The project is no longer available:\n%1\nThe folder was removed or it has no ___project___.",
  u"项目已不可用：\n%1\n文件夹已被删除，或其中没有 ___project___。")
R(u"В этой папке уже есть проект (___project___).\nУдалить его и начать заново? Чаты и настройки проекта будут потеряны.",
  u"This folder already contains a project (___project___).\nDelete it and start over? Project chats and settings will be lost.",
  u"此文件夹里已有项目（___project___）。\n删除并重新开始？项目聊天和设置将丢失。")
R(u"Обнулить счётчики токенов, запросов и времени работы этого проекта?\nОбщая статистика приложения не изменится.",
  u"Reset token, request and work-time counters for this project?\nThe application's overall statistics will not change.",
  u"清零此项目的令牌、请求和工作时间计数？\n应用程序的总统计不会改变。")
R(u"Обнулить общую статистику по всем проектам, включая время работы?\nСчётчики открытого проекта не изменятся.",
  u"Reset overall statistics for all projects, including work time?\nCounters of the open project will not change.",
  u"清零所有项目的总统计，包括工作时间？\n当前打开项目的计数不会改变。")
R(u"Проверка", u"Check", u"检查")
R(u"\n\nИнструмент сейчас занят или к нему есть очередь: ожидающие воркеры получат отказ.",
  u"\n\nThe tool is busy or has a queue: waiting workers will be rejected.",
  u"\n\n该工具正忙或已有队列：等待中的工作者会被拒绝。")
R(u"Удалить воркера «%1» с графа?\nДочерние воркеры будут удалены, чат закроется.",
  u"Remove worker \"%1\" from the graph?\nChild workers will be removed and the chat will close.",
  u"从关系图中移除工作者“%1”？\n子工作者会被删除，聊天将关闭。")
R(u"Команда ждёт ввод", u"Command is waiting for input", u"命令正在等待输入")
R(u"Время работы", u"Work time", u"工作时间")
R(u"Файлов изменено", u"Files changed", u"已改文件")
R(u"Строк добавлено", u"Lines added", u"新增行")
R(u"Строк удалено", u"Lines removed", u"删除行")
R(u"Время работы проекта: запросы к модели и вызовы функций",
  u"Project work time: model requests and tool calls",
  u"项目工作时间：模型请求和函数调用")
R(u"Изменённых файлов: %1", u"Changed files: %1", u"已改文件：%1")
R(u"Добавлено строк: %1", u"Lines added: %1", u"新增行：%1")
R(u"Удалено строк: %1", u"Lines removed: %1", u"删除行：%1")
R(u"В настройках проекта есть пресеты воркеров, ссылающиеся на профили моделей, которых нет в настройках программы:\n\n%1\n\nУдалить эти пресеты из настроек проекта?",
  u"Project settings contain worker presets that reference model profiles missing from the application settings:\n\n%1\n\nRemove these presets from the project settings?",
  u"项目设置中的工作者预设引用了程序设置里不存在的模型配置：\n\n%1\n\n从项目设置中删除这些预设？")
R(u"Отсутствующие профили моделей", u"Missing model profiles", u"缺失的模型配置")
R(u"Оставить", u"Keep", u"保留")
R(u"Модель: %1\nВходы: %2", u"Model: %1\nInputs: %2", u"模型：%1\n输入：%2")
R(u"\nНужна референс-картинка", u"\nA reference image is required", u"\n需要参考图片")
R(u"Копировать", u"Copy", u"复制")
R(u"Редактировать", u"Edit", u"编辑")
R(u"Пресет корневого исполнителя при создании проекта и очистке сеанса. Можно выбрать только пресет исполнителя.",
  u"Root executor preset used when creating a project and clearing a session. Only an executor preset can be selected.",
  u"创建项目和清空会话时使用的根执行者预设。只能选择执行者预设。")
R(u"Пресет корневого исполнителя:", u"Root executor preset:", u"根执行者预设：")
R(u"Главный чат — уровень 0. На максимальной глубине исполнитель уже не создаёт детей.",
  u"The main chat is level 0. At maximum depth an executor no longer creates children.",
  u"主聊天是第 0 层。达到最大深度后，执行者不再创建子级。")
R(u"Только просмотр. Изменение — двойной щелчок или «Редактировать».",
  u"View only. Edit with a double-click or Edit.",
  u"只读。双击或点“编辑”即可修改。")
R(u"Новый профиль с адресом, моделью, токеном и остальными параметрами выбранного.",
  u"A new profile with the address, model, token and other parameters of the selected one.",
  u"用所选配置的地址、模型、令牌和其他参数新建一个配置。")
R(u"Новый воркер с типом и профилем модели выбранного.",
  u"A new worker with the type and model profile of the selected one.",
  u"用所选工作者的类型和模型配置新建一个工作者。")
R(u"Удалить воркер «%1»?", u"Delete worker \"%1\"?", u"删除工作者“%1”？")
R(u"Удалить выбранные воркеры (%1)?\n\n%2",
  u"Delete the selected workers (%1)?\n\n%2",
  u"删除所选工作者（%1）？\n\n%2")
R(u"Удаление воркеров", u"Delete workers", u"删除工作者")
R(u" (копия)", u" (copy)", u" （副本）")
R(u"Пресеты моделей", u"Model presets", u"模型预设")
R(u"Провайдер: %1\nКласс: %2\nURL: %3\nМодель: %4",
  u"Provider: %1\nClass: %2\nURL: %3\nModel: %4",
  u"提供商：%1\n类别：%2\nURL：%3\n模型：%4")
R(u"\nБаланс: %1", u"\nBalance: %1", u"\n余额：%1")
R(u"\nНазначение: %1", u"\nPurpose: %1", u"\n用途：%1")
R(u"Удалить профиль «%1»?", u"Delete profile \"%1\"?", u"删除配置“%1”？")
R(u"Удалить выбранные профили (%1)?\n\n%2",
  u"Delete the selected profiles (%1)?\n\n%2",
  u"删除所选配置（%1）？\n\n%2")
R(u"\n\nВместе с ними будут удалены пресеты воркеров, которые ссылаются на эти профили, и живые воркеры текущего проекта с этими моделями.",
  u"\n\nWorker presets that reference these profiles, and live workers in the current project that use these models, will be removed as well.",
  u"\n\n引用这些配置的工作者预设，以及当前项目中使用这些模型的在线工作者，也会一并删除。")
R(u"\n\nЖивых воркеров: %1.", u"\n\nLive workers: %1.", u"\n\n在线工作者：%1。")
R(u"\n\nПоследний профиль удалить нельзя.",
  u"\n\nThe last profile cannot be deleted.",
  u"\n\n不能删除最后一个配置。")
R(u"Удаление профиля модели", u"Delete model profile", u"删除模型配置")
R(u"пусто", u"empty", u"空")
R(u"задан", u"set", u"已设置")
R(u"%1 ток. (из API /models)", u"%1 tok. (from API /models)", u"%1 令牌（来自 API /models）")
R(u"%1 ток. (оценка: API не отдаёт размер окна)",
  u"%1 tok. (estimate: the API does not report the window size)",
  u"%1 令牌（估计：API 未返回窗口大小）")
R(u"Имя профиля", u"Profile name", u"配置名称")
R(u"Адрес API", u"API address", u"API 地址")
R(u"Модель", u"Model", u"模型")
R(u"Токен", u"Token", u"令牌")
R(u"Класс задач API", u"API task class", u"API 任务类别")
R(u"Назначение модели", u"Model purpose", u"模型用途")
R(u"URL баланса", u"Balance URL", u"余额 URL")
R(u"JSON путь баланса", u"Balance JSON path", u"余额 JSON 路径")
R(u"Путь запроса", u"Request path", u"请求路径")
R(u"Путь статуса задачи", u"Task status path", u"任务状态路径")
R(u"Каталог моделей", u"Model catalog", u"模型目录")
R(u"Доп. поля запроса", u"Extra request fields", u"附加请求字段")
R(u"Лимит вызовов функций", u"Tool-call limit", u"函数调用上限")
R(u"Потолок контекста", u"Context cap", u"上下文上限")
R(u"Окно %1", u"Window %1", u"窗口 %1")
R(u"Тип воркера", u"Worker type", u"工作者类型")
R(u"Профили не выделены.", u"No profiles selected.", u"未选择配置。")
R(u"Воркеры не выделены.", u"No workers selected.", u"未选择工作者。")
R(u"пусто = стандартный путь класса задач; иначе путь от адреса API или полный URL",
  u"empty = the standard path for the task class; otherwise a path from the API address, or a full URL",
  u"留空 = 该任务类别的标准路径；否则为相对 API 地址的路径，或完整 URL")
R(u"Куда слать запрос генерации. Нужно, если у провайдера нестандартный путь.",
  u"Where to send the generation request. Needed when the provider uses a non-standard path.",
  u"生成请求的发送地址。提供商路径不标准时需要填写。")
R(u"пусто = ссылка из ответа или /tasks/{id}; шаблон с {id}",
  u"empty = a link from the response or /tasks/{id}; a template containing {id}",
  u"留空 = 响应中的链接或 /tasks/{id}；含 {id} 的模板")
R(u"Опрос асинхронной задачи. {id} заменяется на идентификатор из ответа.",
  u"Polling an asynchronous task. {id} is replaced with the id from the response.",
  u"轮询异步任务。{id} 会替换为响应中的标识。")
R(u"пусто = /models; каталог со схемой параметров моделей",
  u"empty = /models; a catalog with the model parameter schema",
  u"留空 = /models；带模型参数结构的目录")
R(u"Откуда брать допустимые параметры модели и их значения для воркеров.",
  u"Where to read the model's allowed parameters and their values for workers.",
  u"工作者从这里读取模型允许的参数及其取值。")
R(u"JSON-объект, добавляется в каждый запрос генерации: {\"ключ\": значение}",
  u"A JSON object added to every generation request: {\"key\": value}",
  u"加到每次生成请求中的 JSON 对象：{\"key\": value}")
R(u"Постоянные поля запроса для этой модели. Поля из задачи воркера имеют приоритет.",
  u"Permanent request fields for this model. Fields from the worker task take priority.",
  u"此模型的固定请求字段。工作者任务中的字段优先。")
R(u"0 — полное окно модели. Можно указать точное число, в том числе равное окну. Иначе история сжимается раньше, по этому потолку.",
  u"0 — the full model window. You can set an exact number, including one equal to the window. Otherwise history is compacted earlier, at this cap.",
  u"0 表示使用模型的完整窗口。也可以填写确切数字，包括与窗口相同的值。否则历史会按此上限提前压缩。")
R(u"Использовать размер окна, полученный из API /models.",
  u"Use the window size reported by the /models API.",
  u"使用 API /models 返回的窗口大小。")
R(u"API не сообщил размер окна. Укажите потолок контекста числом.",
  u"The API did not report a window size. Set the context cap as a number.",
  u"API 未返回窗口大小。请用数字填写上下文上限。")
R(u"Из пресета…", u"From preset…", u"从预设…")
R(u"Не копировать", u"Do not copy", u"不复制")
R(u"На основе:", u"Based on:", u"基于：")
R(u"Путь запроса:", u"Request path:", u"请求路径：")
R(u"Путь статуса задачи:", u"Task status path:", u"任务状态路径：")
R(u"Каталог моделей:", u"Model catalog:", u"模型目录：")
R(u"Доп. поля запроса:", u"Extra request fields:", u"附加请求字段：")
R(u"Нужен JSON-объект вида {\"ключ\": значение}.\n%1",
  u"A JSON object like {\"key\": value} is required.\n%1",
  u"需要形如 {\"key\": value} 的 JSON 对象。\n%1")
R(u"профиль не найден", u"profile not found", u"未找到配置")
R(u"настройки проекта", u"project settings", u"项目设置")
R(u"настройки программы", u"application settings", u"程序设置")
R(u"лимиты и контроль действий", u"limits and action control", u"限额与操作控制")
R(u"профили моделей (без ключей API)", u"model profiles (without API keys)", u"模型配置（不含 API 密钥）")
R(u"пресеты воркеров", u"worker presets", u"工作者预设")
R(u"все разделы (ключи API не раскрываются)",
  u"all sections (API keys are not revealed)",
  u"全部栏目（不显示 API 密钥）")
R(u"Прочитать %1: %2", u"Read %1: %2", u"读取 %1：%2")
R(u"\nЗачем: %1", u"\nWhy: %1", u"\n原因：%1")
R(u"имя=%1", u"name=%1", u"名称=%1")
R(u"адрес API=%1", u"API address=%1", u"API 地址=%1")
R(u"модель=%1", u"model=%1", u"模型=%1")
R(u"класс задач=%1", u"task class=%1", u"任务类别=%1")
R(u"назначение=%1", u"purpose=%1", u"用途=%1")
R(u"URL баланса=%1", u"balance URL=%1", u"余额 URL=%1")
R(u"JSON путь баланса=%1", u"balance JSON path=%1", u"余额 JSON 路径=%1")
R(u"путь запроса=%1", u"request path=%1", u"请求路径=%1")
R(u"путь статуса задачи=%1", u"task status path=%1", u"任务状态路径=%1")
R(u"каталог моделей=%1", u"model catalog=%1", u"模型目录=%1")
R(u"доп. поля запроса=%1", u"extra request fields=%1", u"附加请求字段=%1")
R(u"лимит вызовов функций=%1", u"tool-call limit=%1", u"函数调用上限=%1")
R(u"потолок контекста=%1", u"context cap=%1", u"上下文上限=%1")
R(u"Добавить профиль модели: %1 (ключ API останется пустым)",
  u"Add model profile: %1 (the API key stays empty)",
  u"添加模型配置：%1（API 密钥保持为空）")
R(u"Изменить профиль модели «%1»: %2",
  u"Edit model profile \"%1\": %2",
  u"修改模型配置“%1”：%2")
R(u"Удалить профиль модели «%1» (%2) и пресеты на нём",
  u"Delete model profile \"%1\" (%2) and presets that use it",
  u"删除模型配置“%1”（%2）以及使用它的预设")
R(u"инструмент", u"tool", u"工具")
R(u"исполнитель", u"executor", u"执行者")
R(u"Добавить пресет воркера: %1", u"Add worker preset: %1", u"添加工作者预设：%1")
R(u"Изменить пресет воркера %1 → %2",
  u"Edit worker preset %1 → %2",
  u"修改工作者预设 %1 → %2")
R(u"Удалить пресет воркера %1", u"Delete worker preset %1", u"删除工作者预设 %1")
R(u"%1: %2 → %3", u"%1: %2 → %3", u"%1：%2 → %3")
R(u"да", u"yes", u"是")
R(u"нет", u"no", u"否")
R(u"Корневой пресет: %1", u"Root preset: %1", u"根预设：%1")
R(u"Разрешить без подтверждения команды: %1",
  u"Allow commands without confirmation: %1",
  u"允许以下命令无需确认：%1")
R(u"Снова требовать подтверждение для команд: %1",
  u"Require confirmation again for commands: %1",
  u"再次要求确认以下命令：%1")
R(u"Изменить %1:", u"Change %1:", u"更改 %1：")
R(u"Схема команды", u"Team schema", u"团队结构")
R(u"Получает запрос и делает работу сам. Детей вызывает, только когда нужен другой контекст или другая модальность.",
  u"Receives the request and does the work. Calls children only when another context or another modality is needed.",
  u"接收请求并自己完成工作。仅在需要另一段上下文或另一种模态时才调用子级。")
R(u"Из файла…", u"From file…", u"从文件…")
R(u"Что должен делать этот воркер", u"What this worker should do", u"此工作者应做什么")
R(u"Воркер:", u"Worker:", u"工作者：")
R(u"Назначение:", u"Purpose:", u"用途：")
R(u"Вставить в запрос", u"Insert into the query", u"插入到请求")
R(u"Загрузить схему", u"Load schema", u"加载结构")
R(u"Схема команды (*.json);;Все файлы (*.*)",
  u"Team schema (*.json);;All files (*.*)",
  u"团队结构 (*.json);;所有文件 (*.*)")
R(u"Не удалось прочитать схему из файла.",
  u"Could not read the schema from the file.",
  u"无法从文件读取结构。")
R(u"Сохранить", u"Save", u"保存")
R(u"В настройках проекта нет воркеров",
  u"The project settings have no workers",
  u"项目设置中没有工作者")
R(u"нет в настройках проекта", u"not in the project settings", u"不在项目设置中")
R(u"Выберите воркера", u"Select a worker", u"选择工作者")
R(u"корневой исполнитель", u"root executor", u"根执行者")
R(u"Схема команды.", u"Team schema.", u"团队结构。")
R(u"Инструменты создавать через create_tool. Остальных — через delegate_task, родитель указан ниже. В каждый вызов передавай указанный preset_id.",
  u"Create tools with create_tool. Create everyone else with delegate_task; the parent is given below. Pass the given preset_id in every call.",
  u"工具用 create_tool 创建。其余用 delegate_task 创建，父级见下方。每次调用都传入给定的 preset_id。")
R(u"Комментарий корневому исполнителю: %1",
  u"Comment for the root executor: %1",
  u"给根执行者的说明：%1")
R(u"не указано", u"not specified", u"未指定")
R(u"Инструмент «%1». Общий, вне дерева связей. Назначение: %2.",
  u"Tool \"%1\". Shared, outside the link tree. Purpose: %2.",
  u"工具“%1”。共享，不在关系树中。用途：%2。")
R(u"Инструмент «%1» (preset_id=%2). Общий, вне дерева связей. Назначение: %3.",
  u"Tool \"%1\" (preset_id=%2). Shared, outside the link tree. Purpose: %3.",
  u"工具“%1”（preset_id=%2）。共享，不在关系树中。用途：%3。")
R(u"%1 «%2». Родитель: %3. Назначение: %4.",
  u"%1 \"%2\". Parent: %3. Purpose: %4.",
  u"%1“%2”。父级：%3。用途：%4。")
R(u"%1 «%2» (preset_id=%3). Родитель: %4. Назначение: %5.",
  u"%1 \"%2\" (preset_id=%3). Parent: %4. Purpose: %5.",
  u"%1“%2”（preset_id=%3）。父级：%4。用途：%5。")
R(u"Конец схемы команды.", u"End of the team schema.", u"团队结构结束。")
R(u"Обязательная схема команды (не менять).",
  u"Mandatory team schema (do not change).",
  u"必须遵守的团队结构（不要更改）。")
R(u"Конец обязательной схемы.", u"End of the mandatory schema.", u"必须遵守的结构结束。")
R(u"Бюджет поддерева исчерпан.\nTool-пакеты: %1 / %2. Время: ≈%3 / %4 мин.\nСбросить лимиты (+%5 пакетов, +%6 мин) и продолжить создание детей?",
  u"Subtree budget is exhausted.\nTool batches: %1 / %2. Time: ≈%3 / %4 min.\nReset the limits (+%5 batches, +%6 min) and continue creating children?",
  u"子树额度已用尽。\n工具批次：%1 / %2。时间：约 %3 / %4 分钟。\n重置限额（+%5 批次，+%6 分钟）并继续创建子级？")
R(u"Окно контекста заполнено. Сжимаю историю и повторяю запрос, не теряя суть беседы.",
  u"The context window is full. Compacting history and repeating the request without losing the point of the conversation.",
  u"上下文窗口已满。正在压缩历史并重试请求，同时保留对话要点。")
R(u"Контекст сжат: в модель уходят формулировка задачи, сводка предыдущей работы и последние сообщения.",
  u"Context compacted: the model receives the task statement, a summary of earlier work, and the latest messages.",
  u"上下文已压缩：模型收到任务表述、此前工作的摘要以及最近的消息。")
R(u"Свернуть старую историю в краткое содержание, чтобы продолжить беседу в окне модели.",
  u"Fold older history into a short summary so the conversation can continue inside the model window.",
  u"把较早的历史收成简短摘要，以便在模型窗口内继续对话。")
R(u"Принудительно остановить этот воркер, включая зависшую команду.",
  u"Force-stop this worker, including a stuck command.",
  u"强制停止此工作者，包括卡住的命令。")
R(u"Прервать работу и сразу отправить родителю текущий отчёт.",
  u"Stop the work and send the parent the current report right away.",
  u"中断工作并立即把当前报告发给父级。")
R(u"HTTP 402: на счёте DeepSeek нет средств. Пополните баланс: https://platform.deepseek.com/top_up",
  u"HTTP 402: the DeepSeek account has no funds. Top up: https://platform.deepseek.com/top_up",
  u"HTTP 402：DeepSeek 账户没有余额。请充值：https://platform.deepseek.com/top_up")
R(u"Открыть схему воркеров", u"Open the worker schema", u"打开工作者结构")
R(u"Схема", u"Schema", u"结构")
R(u"HTTP 429: лимит Groq исчерпан, подождите и повторите. Лимиты: https://console.groq.com/settings/limits",
  u"HTTP 429: the Groq limit is exhausted, wait and retry. Limits: https://console.groq.com/settings/limits",
  u"HTTP 429：Groq 限额已用尽，请稍后再试。限额：https://console.groq.com/settings/limits")
R(u"Профиль модели: %1 → %2 (%3)",
  u"Model profile: %1 → %2 (%3)",
  u"模型配置：%1 → %2（%3）")
R(u"Картинка слишком большая для inline: %1 — только путь.",
  u"Image is too large to inline: %1 — path only.",
  u"图片太大，无法内嵌：%1 — 仅路径。")
R(u"Аудио слишком большое для inline: %1 — только путь.",
  u"Audio is too large to inline: %1 — path only.",
  u"音频太大，无法内嵌：%1 — 仅路径。")
R(u"Проанализируй прикреплённое аудио и выполни задачу.",
  u"Analyze the attached audio and carry out the task.",
  u"分析所附音频并完成任务。")
R(u"Проанализируй прикреплённое изображение и выполни задачу.",
  u"Analyze the attached image and carry out the task.",
  u"分析所附图片并完成任务。")
R(u"Учти содержимое прикреплённых файлов и выполни задачу.",
  u"Take the attached files into account and carry out the task.",
  u"结合所附文件的内容并完成任务。")
R(u"Изображения (*.png *.jpg *.jpeg *.webp *.gif *.bmp)",
  u"Images (*.png *.jpg *.jpeg *.webp *.gif *.bmp)",
  u"图片 (*.png *.jpg *.jpeg *.webp *.gif *.bmp)")
R(u"Аудио (*.wav *.mp3 *.m4a *.ogg *.flac *.aac *.webm)",
  u"Audio (*.wav *.mp3 *.m4a *.ogg *.flac *.aac *.webm)",
  u"音频 (*.wav *.mp3 *.m4a *.ogg *.flac *.aac *.webm)")
R(u"Видео (*.mp4 *.webm *.mov *.mkv *.avi)",
  u"Video (*.mp4 *.webm *.mov *.mkv *.avi)",
  u"视频 (*.mp4 *.webm *.mov *.mkv *.avi)")
R(u"Все файлы (*.*)", u"All files (*.*)", u"所有文件 (*.*)")
R(u"Проект не открыт.", u"No project is open.", u"没有打开的项目。")
R(u"Файл не найден: %1", u"File not found: %1", u"未找到文件：%1")
R(u"Не удалось прочитать %1: %2", u"Could not read %1: %2", u"无法读取 %1：%2")
R(u"Пустой аудиофайл: %1", u"Empty audio file: %1", u"空音频文件：%1")
R(u"Пустой файл: %1", u"Empty file: %1", u"空文件：%1")
R(u"Бинарный файл, не встраивается как текст: %1",
  u"Binary file, not embedded as text: %1",
  u"二进制文件，不能作为文本嵌入：%1")
R(u"Пустое изображение: %1", u"Empty image: %1", u"空图片：%1")
R(u"Не удалось создать tmp проекта.",
  u"Could not create the project tmp folder.",
  u"无法创建项目的 tmp 目录。")
R(u"Не удалось скопировать файл в проект.",
  u"Could not copy the file into the project.",
  u"无法把文件复制到项目中。")
R(u"(не задана)", u"(not set)", u"（未设置）")
R(u"Модель не ответила за %1 с — запрос прерван по таймауту. Повторите запрос или увеличьте таймаут в настройках.",
  u"The model did not answer within %1 s — the request was aborted by timeout. Retry, or increase the timeout in settings.",
  u"模型在 %1 秒内没有回答 — 请求因超时中断。请重试，或在设置中增大超时。")
R(u"Аудио", u"Audio", u"音频")
R(u"Видео", u"Video", u"视频")
R(u"Выберите пресет провайдера (URL, модель, класс задач). Токен API нужно ввести отдельно после создания.",
  u"Choose a provider preset (URL, model, task class). Enter the API token separately after it is created.",
  u"选择提供商预设（URL、模型、任务类别）。创建后再单独填写 API 令牌。")
R(u"%1 ч %2 мин %3 с", u"%1 h %2 min %3 s", u"%1 小时 %2 分 %3 秒")
R(u"%1 мин %2 с", u"%1 min %2 s", u"%1 分 %2 秒")
R(u"Не выбран профиль модели.", u"No model profile is selected.", u"未选择模型配置。")
R(u"Такая комбинация тип+профиль уже есть.",
  u"This type and profile combination already exists.",
  u"这种类型与配置的组合已经存在。")
R(u"Пресет не найден.", u"Preset not found.", u"未找到预设。")
R(u"Создать файл вне проекта:\n%1",
  u"Create a file outside the project:\n%1",
  u"在项目外创建文件：\n%1")
R(u"Изменить файл вне проекта:\n%1",
  u"Edit a file outside the project:\n%1",
  u"修改项目外的文件：\n%1")
R(u"Удалить файл вне проекта:\n%1",
  u"Delete a file outside the project:\n%1",
  u"删除项目外的文件：\n%1")
R(u"Переименовать или переместить вне проекта:\n%1\n→ %2",
  u"Rename or move outside the project:\n%1\n→ %2",
  u"在项目外重命名或移动：\n%1\n→ %2")
R(u"Подставьте свой resource и имя deployment.",
  u"Substitute your resource and deployment name.",
  u"请替换为自己的 resource 和 deployment 名称。")
R(u"Подставьте ACCOUNT_ID.", u"Substitute ACCOUNT_ID.", u"请替换 ACCOUNT_ID。")
R(u"Проверьте endpoint /audio/generations у провайдера.",
  u"Check the provider's /audio/generations endpoint.",
  u"请核对提供商的 /audio/generations 端点。")
R(u"Зависит от поддержки провайдера на OpenRouter.",
  u"Depends on the provider's support on OpenRouter.",
  u"取决于该提供商在 OpenRouter 上是否支持。")
R(u"Часто async job; ответ может быть id/status.",
  u"Often an async job; the response may be an id/status.",
  u"通常是异步任务；响应可能是 id/status。")
R(u"Уточните model id в каталоге OpenRouter.",
  u"Check the model id in the OpenRouter catalog.",
  u"请在 OpenRouter 目录中确认 model id。")
R(u"ответ HTML (нужен API баланса, не страница кабинета). Для DashScope/Qwen: /api/v1/quotas",
  u"HTML response (a balance API is required, not the account page). For DashScope/Qwen: /api/v1/quotas",
  u"返回了 HTML（需要余额 API，而不是账户页面）。DashScope/Qwen 用：/api/v1/quotas")
R(u"ответ не JSON: %1 (%2)", u"response is not JSON: %1 (%2)", u"响应不是 JSON：%1（%2）")
R(u"Инструмент: ", u"Tool: ", u"工具：")


def unescape(text):
    text = text.replace(u"\\\\", u"\x01")
    text = text.replace(u"\\n", u"\n")
    text = text.replace(u"\x01", u"\\")
    return text


def escape(text):
    return text.replace(u"\\", u"\\\\").replace(u"\n", u"\\n")


def has_cyr(text):
    for ch in text:
        if u"\u0400" <= ch <= u"\u04ff":
            return True
    return False


def load_needed():
    path = os.path.join(os.path.dirname(__file__), "_untranslated.txt")
    needed = []
    mode = None
    with io.open(path, "r", encoding="utf-8") as handle:
        for line in handle:
            line = line.rstrip(u"\n").rstrip(u"\r")
            if line.startswith(u"--- MISSING"):
                mode = "m"
                continue
            if line.startswith(u"--- SAME"):
                mode = "s"
                continue
            if mode and line:
                needed.append(unescape(line))
    return needed


def apply_file(path, index, lang_index):
    with io.open(path, "r", encoding="utf-8") as handle:
        lines = handle.read().split(u"\n")
    if lines and lines[-1] == u"":
        trailing = True
        lines = lines[:-1]
    else:
        trailing = True
    seen = {}
    out = []
    replaced = 0
    for line in lines:
        stripped = line.strip()
        if not stripped or stripped.startswith(u"#") or stripped.startswith(u";") or u"|" not in line:
            out.append(line)
            continue
        sep = line.find(u"|")
        source = unescape(line[:sep])
        if source in index:
            out.append(escape(source) + u"|" + escape(index[source][lang_index]))
            seen[source] = True
            replaced += 1
        else:
            out.append(line)
    added = 0
    for src, pair in index.items():
        if src not in seen:
            out.append(escape(src) + u"|" + escape(pair[lang_index]))
            added += 1
    text = u"\n".join(out) + (u"\n" if trailing else u"")
    with io.open(path, "w", encoding="utf-8") as handle:
        handle.write(text)
    return replaced, added


def main():
    index = {}
    for src, en, zh in ROWS:
        index[src] = (en, zh)
    needed = load_needed()
    missing_rows = []
    for src in needed:
        if src not in index:
            missing_rows.append(src)
    if missing_rows:
        sys.stdout.write("dict misses %d sources\n" % len(missing_rows))
        for src in missing_rows:
            sys.stdout.write(src.replace(u"\n", u"\\n").encode("utf-8") + "\n")
        return 1
    for lang, name in ((0, "en"), (1, "zh")):
        path = os.path.join(ROOT, name, "gui_by_source.ini")
        replaced, added = apply_file(path, index, lang)
        sys.stdout.write("%s replaced %d added %d\n" % (name, replaced, added))
    leftover = 0
    path = os.path.join(ROOT, "en", "gui_by_source.ini")
    with io.open(path, "r", encoding="utf-8") as handle:
        for line in handle:
            line = line.rstrip(u"\n").rstrip(u"\r")
            if not line or line.startswith(u"#") or u"|" not in line:
                continue
            sep = line.find(u"|")
            translation = unescape(line[sep + 1:])
            if has_cyr(translation):
                leftover += 1
                sys.stdout.write("CYR " + translation.replace(u"\n", u"\\n").encode("utf-8") + "\n")
    sys.stdout.write("cyrillic left %d\n" % leftover)
    return 0


if __name__ == "__main__":
    sys.exit(main())
