#include "conversationwidget.h"
//----------------------------------------------
#include "agent.h"
#include "appsettings.h"
#include "markdownformatter.h"
//----------------------------------------------
#include <QAbstractTextDocumentLayout>
#include <QColor>
#include <QEvent>
#include <QFont>
#include <QCursor>
#include <QHBoxLayout>
#include <QLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRegExp>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QSizePolicy>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>
#include <QtGlobal>
#include <QtMath>

//----------------------------------------------

// Сколько пузырей держим в виджете и сколько записей ленты храним на диске.
static const int kMaxVisibleBubbles  = 250;
static const int kMaxDisplayLogItems = 1500;

//----------------------------------------------

static QString clipDisplay(QString text, int maxChars)
{
    if (text.size() <= maxChars)
        return text;
    return text.left(maxChars) + QString::fromUtf8("\n…");
}

//----------------------------------------------

static QString firstPhrase(QString text)
{
    QString t = text.trimmed();
    t.replace(QChar('\r'), QChar('\n'));
    const int nl = t.indexOf(QChar('\n'));
    if (nl > 0)
        t = t.left(nl);
    t.replace(QRegExp(QString::fromUtf8("\\s+")), QString::fromUtf8(" "));
    t = t.trimmed();
    if (t.size() > 88)
        t = t.left(85) + QString::fromUtf8("…");
    return t;
}

//----------------------------------------------

ChatMessageFrame::ChatMessageFrame(QString title, QString html, QString preview,
                                   QString objectName, bool alignRight, bool collapsed,
                                   QWidget *parent)
    : QFrame(parent)
    , _title(title)
    , _bodyHtml(html)
    , _preview(firstPhrase(preview))
    , _view(new QTextBrowser(this))
    , _toggle(new QPushButton(this))
    , _collapsed(collapsed)
    , _fitting(false)
    , _fitScheduled(false)
    , _lastFitWidth(-1)
    , _lastFitHeight(-1)
{
    setObjectName(objectName);
    setFrameShape(QFrame::NoFrame);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);

    _toggle->setFixedSize(22, 22);
    _toggle->setFlat(true);
    _toggle->setCursor(Qt::PointingHandCursor);
    _toggle->setToolTip(tr("Свернуть / развернуть"));
    connect(_toggle, SIGNAL(clicked()), this, SLOT(sltToggle()));

    _view->setFrameShape(QFrame::NoFrame);
    _view->setOpenExternalLinks(true);
    _view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    _view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    _view->setUndoRedoEnabled(false);
    _view->setLineWrapMode(QTextEdit::WidgetWidth);
    _view->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    _view->document()->setDocumentMargin(2);
    applyTheme();

    QHBoxLayout *inner = new QHBoxLayout();
    inner->setContentsMargins(10, 6, 10, 6);
    inner->setSpacing(6);
    inner->addWidget(_toggle, 0, Qt::AlignTop);
    inner->addWidget(_view, 1);

    QFrame *bubble = new QFrame(this);
    bubble->setObjectName(QString::fromUtf8("bubble"));
    bubble->setLayout(inner);
    bubble->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);

    QHBoxLayout *row = new QHBoxLayout(this);
    row->setContentsMargins(8, 2, 8, 2);
    row->setSpacing(0);
    if (alignRight)
        row->addStretch(1);
    row->addWidget(bubble, 3);
    if (!alignRight)
        row->addStretch(1);

    applyCollapsed();
}

//----------------------------------------------

void ChatMessageFrame::applyTheme()
{
    AppSettings *s = AppSettings::instance();
    const QString text = s->textColor().name();
    const QString accent = s->accentColor().name();
    const QString muted = s->mutedTextColor().name();
    if (_toggle)
    {
        _toggle->setStyleSheet(QString::fromUtf8(
            "QPushButton { background:transparent; border:none; color:%1; padding:0; }")
                                   .arg(muted));
    }
    if (_view)
    {
        _view->setStyleSheet(QString::fromUtf8(
            "QTextBrowser { background: transparent; border: none; padding: 0; color:%1; }")
                                 .arg(text));
        _view->document()->setDefaultStyleSheet(QString::fromUtf8(
            "p { margin: 0 0 8px 0; color: %1; }"
            "pre { margin: 6px 0; }"
            "ul, ol { margin: 4px 0 8px 18px; }"
            "h2, h3, h4 { margin: 10px 0 4px 0; color: %1; }"
            "a { color: %2; }")
                                                    .arg(text, accent));
    }
}

//----------------------------------------------

QString ChatMessageFrame::collapsedHtml() const
{
    QString line = QString::fromUtf8("<b>%1</b>")
                       .arg(MarkdownFormatter::escapeHtml(_title));
    if (!_preview.isEmpty())
    {
        line += QString::fromUtf8(" - ");
        line += MarkdownFormatter::escapeHtml(_preview);
    }
    return QString::fromUtf8("<p>%1</p>").arg(line);
}

//----------------------------------------------

QString ChatMessageFrame::expandedHtml() const
{
    QString html = QString::fromUtf8("<p><b>%1</b></p>")
                       .arg(MarkdownFormatter::escapeHtml(_title));
    html += _bodyHtml;
    return html;
}

//----------------------------------------------

void ChatMessageFrame::applyCollapsed()
{
    _toggle->setText(_collapsed ? QString::fromUtf8("▸") : QString::fromUtf8("▾"));
    _view->setHtml(_collapsed ? collapsedHtml() : expandedHtml());
    _lastFitWidth = -1;
    _lastFitHeight = -1;
    fitDocumentHeight();
    updateGeometry();
}

//----------------------------------------------

void ChatMessageFrame::fitDocumentHeight()
{
    if (_fitting || !_view)
        return;
    _fitting = true;

    int inner = 0;
    if (_view->viewport())
        inner = _view->viewport()->width();
    if (inner < 40)
        inner = qMax(40, width() - 48);

    if (inner != _lastFitWidth)
    {
        _lastFitWidth = inner;
        _view->document()->setTextWidth(inner);
    }

    qreal docH = _view->document()->size().height();
    if (_view->document()->documentLayout())
        docH = _view->document()->documentLayout()->documentSize().height();
    const int h = qMax(_collapsed ? 22 : 28, qCeil(docH) + 4);
    if (h != _lastFitHeight)
    {
        _lastFitHeight = h;
        _view->setFixedHeight(h);
    }
    _fitting = false;
}

//----------------------------------------------

void ChatMessageFrame::sltToggle()
{
    _collapsed = !_collapsed;
    applyCollapsed();
}

//----------------------------------------------

void ChatMessageFrame::sltFitDocumentHeight()
{
    _fitScheduled = false;
    fitDocumentHeight();
}

//----------------------------------------------

void ChatMessageFrame::resizeEvent(QResizeEvent *event)
{
    QFrame::resizeEvent(event);
    if (_fitting || _fitScheduled)
        return;
    _fitScheduled = true;
    QTimer::singleShot(0, this, SLOT(sltFitDocumentHeight()));
}

//----------------------------------------------

ConversationWidget::ConversationWidget(QWidget *parent)
    : QWidget(parent)
    , _agent(0)
    , _contextLabel(0)
    , _activityLabel(0)
    , _activityTimer(0)
    , _contextBar(0)
    , _compactButton(0)
    , _scroll(0)
    , _messagesHost(0)
    , _messagesLayout(0)
    , _input(0)
    , _sendButton(0)
    , _stopButton(0)
    , _interruptButton(0)
    , _reportButton(0)
    , _inputHost(0)
    , _permissionHost(0)
    , _permissionLabel(0)
    , _approveButton(0)
    , _alwaysButton(0)
    , _rejectButton(0)
    , _promptMode(0)
    , _pinBottom(true)
    , _restoring(false)
    , _userInputEnabled(true)
    , _scrollScheduled(false)
{
    _contextLabel = new QLabel(tr("Контекст: —"), this);
    _contextBar = new QProgressBar(this);
    _contextBar->setRange(0, 1000);
    _contextBar->setValue(0);
    _contextBar->setTextVisible(false);
    _contextBar->setFixedHeight(8);
    _contextBar->setMaximumWidth(220);

    // Чем сейчас занят агент и как долго: ответ модели, функция, дети.
    _activityLabel = new QLabel(this);
    _activityLabel->setObjectName(QString::fromUtf8("activityLabel"));
    _activityLabel->hide();
    _activityTimer = new QTimer(this);
    _activityTimer->setInterval(1000);
    connect(_activityTimer, SIGNAL(timeout()), this, SLOT(sltActivityTick()));

    _compactButton = new QPushButton(tr("Сжать контекст"), this);
    _compactButton->setToolTip(tr("Свернуть старую историю в краткое содержание, "
                                  "чтобы продолжить беседу в окне модели."));
    connect(_compactButton, SIGNAL(clicked()), this, SIGNAL(compactRequested()));

    _interruptButton = new QPushButton(tr("Прервать"), this);
    _interruptButton->setToolTip(tr("Принудительно остановить этот воркер, "
                                    "включая зависшую команду."));
    _interruptButton->setEnabled(false);
    connect(_interruptButton, SIGNAL(clicked()),
            this, SIGNAL(workerInterruptRequested()));

    _reportButton = new QPushButton(tr("Отчёт родителю"), this);
    _reportButton->setToolTip(tr("Прервать работу и сразу отправить родителю "
                                 "текущий отчёт."));
    _reportButton->hide();
    connect(_reportButton, SIGNAL(clicked()),
            this, SIGNAL(reportToParentRequested()));

    QHBoxLayout *contextRow = new QHBoxLayout();
    contextRow->addWidget(_contextBar);
    contextRow->addWidget(_contextLabel, 1);
    contextRow->addWidget(_activityLabel);
    contextRow->addWidget(_compactButton);
    contextRow->addWidget(_interruptButton);
    contextRow->addWidget(_reportButton);

    _messagesHost = new QWidget(this);
    _messagesHost->setObjectName(QString::fromUtf8("messagesHost"));
    _messagesHost->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    _messagesLayout = new QVBoxLayout(_messagesHost);
    _messagesLayout->setContentsMargins(0, 0, 0, 0);
    _messagesLayout->setSpacing(2);
    _messagesLayout->addStretch(1);

    _scroll = new QScrollArea(this);
    _scroll->setWidgetResizable(true);
    _scroll->setFrameShape(QFrame::NoFrame);
    _scroll->setWidget(_messagesHost);
    _scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(_scroll->verticalScrollBar(), SIGNAL(rangeChanged(int,int)),
            this, SLOT(sltScrollRangeChanged(int,int)));

    _input = new QPlainTextEdit(this);
    _input->setPlaceholderText(tr("Сообщение…  Enter — отправить, Shift+Enter — новая строка"));
    _input->setMaximumHeight(110);
    _input->installEventFilter(this);

    _sendButton = new QPushButton(tr("Отправить"), this);
    _stopButton = new QPushButton(tr("Стоп"), this);
    _stopButton->setEnabled(false);
    _stopButton->setToolTip(tr("Остановить работу во всех чатах"));
    connect(_sendButton, SIGNAL(clicked()), this, SLOT(sltSend()));
    connect(_stopButton, SIGNAL(clicked()), this, SIGNAL(stopRequested()));

    QVBoxLayout *buttons = new QVBoxLayout();
    buttons->addWidget(_sendButton);
    buttons->addWidget(_stopButton);
    buttons->addStretch();

    QHBoxLayout *inputRow = new QHBoxLayout();
    inputRow->setContentsMargins(0, 0, 0, 0);
    inputRow->addWidget(_input, 1);
    inputRow->addLayout(buttons);

    _inputHost = new QWidget(this);
    _inputHost->setLayout(inputRow);
    _inputHost->hide();

    _permissionLabel = new QLabel(this);
    _permissionLabel->setWordWrap(true);
    _approveButton = new QPushButton(tr("Разрешить"), this);
    _alwaysButton = new QPushButton(tr("Всегда"), this);
    _alwaysButton->setToolTip(tr("Разрешать эту программу во всём приложении, "
                                  "пока пути и действие остаются в каталоге проекта"));
    _rejectButton = new QPushButton(tr("Отклонить"), this);
    connect(_approveButton, SIGNAL(clicked()), this, SLOT(sltPermissionApprove()));
    connect(_alwaysButton, SIGNAL(clicked()), this, SLOT(sltPermissionAlways()));
    connect(_rejectButton, SIGNAL(clicked()), this, SLOT(sltPermissionReject()));

    QHBoxLayout *permissionButtons = new QHBoxLayout();
    permissionButtons->addStretch();
    permissionButtons->addWidget(_approveButton);
    permissionButtons->addWidget(_alwaysButton);
    permissionButtons->addWidget(_rejectButton);

    QVBoxLayout *permissionLayout = new QVBoxLayout();
    permissionLayout->setContentsMargins(8, 8, 8, 8);
    permissionLayout->addWidget(_permissionLabel);
    permissionLayout->addLayout(permissionButtons);

    _permissionHost = new QWidget(this);
    _permissionHost->setObjectName(QString::fromUtf8("permissionHost"));
    _permissionHost->setLayout(permissionLayout);
    _permissionHost->hide();

    QVBoxLayout *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->addLayout(contextRow);
    root->addWidget(_scroll, 1);
    root->addWidget(_permissionHost);
    root->addWidget(_inputHost);

    applyTheme();
}

//----------------------------------------------

void ConversationWidget::setAgent(Agent *agent)
{
    if (_agent == agent)
        return;
    if (_agent)
        disconnect(_agent, 0, this, 0);

    _agent = agent;
    if (!_agent)
        return;

    connect(_agent, SIGNAL(userMessage(QString)),
            this, SLOT(sltUserMessage(QString)));
    connect(_agent, SIGNAL(assistantMessage(QString)),
            this, SLOT(sltAssistantMessage(QString)));
    connect(_agent, SIGNAL(toolCallStarted(QString,QString)),
            this, SLOT(sltToolCallStarted(QString,QString)));
    connect(_agent, SIGNAL(toolCallFinished(QString,QString,bool)),
            this, SLOT(sltToolCallFinished(QString,QString,bool)));
    connect(_agent, SIGNAL(notice(QString)),
            this, SLOT(sltNotice(QString)));
    connect(_agent, SIGNAL(failed(QString)),
            this, SLOT(sltFailed(QString)));
    connect(_agent, SIGNAL(busyChanged(bool)),
            this, SLOT(sltBusyChanged(bool)));
    connect(_agent, SIGNAL(contextUsageChanged(int,int)),
            this, SLOT(sltContextUsageChanged(int,int)));
    connect(_agent, SIGNAL(compactingChanged(bool)),
            this, SLOT(sltCompactingChanged(bool)));
    _agent->refreshContextUsage();
}

//----------------------------------------------

void ConversationWidget::clearMessages()
{
    if (_messagesLayout)
    {
        for (int i = _messagesLayout->count() - 1; i >= 0; --i)
        {
            QLayoutItem *item = _messagesLayout->itemAt(i);
            if (!item || !item->widget())
                continue;
            _messagesLayout->takeAt(i);
            item->widget()->deleteLater();
            delete item;
        }
    }
    if (!_restoring)
        _displayLog.clear();
}

//----------------------------------------------

QList<ChatDisplayItem> ConversationWidget::displayLog() const
{
    return _displayLog;
}

//----------------------------------------------

void ConversationWidget::restoreDisplayLog(const QList<ChatDisplayItem> &items)
{
    _restoring = true;
    clearMessages();
    _displayLog = items;
    // На экран поднимаем только хвост ленты: сотни QTextBrowser
    // в скрытой вкладке — это память и бесконечные пересчёты layout.
    int first = 0;
    if (items.size() > kMaxVisibleBubbles)
        first = items.size() - kMaxVisibleBubbles;
    for (int i = first; i < items.size(); ++i)
        replayDisplayItem(items.at(i));
    _restoring = false;
    _pinBottom = true;
    scheduleScrollToBottom();
    QTimer::singleShot(50, this, SLOT(sltScrollToBottom()));
}

//----------------------------------------------

void ConversationWidget::restoreFromHistory(const QList<LlmMessage> &history)
{
    QList<ChatDisplayItem> items;
    bool skipFirstSystem = true;
    QList<LlmToolCall> pendingCalls;

    foreach (const LlmMessage &message, history)
    {
        if (message.role == LlmRoleSystem)
        {
            if (skipFirstSystem)
            {
                skipFirstSystem = false;
                continue;
            }
            ChatDisplayItem item;
            item.kind = QString::fromUtf8("notice");
            item.title = tr("Контекст");
            item.text = message.content;
            items.append(item);
            continue;
        }

        if (message.role == LlmRoleUser)
        {
            ChatDisplayItem item;
            item.kind = QString::fromUtf8("user");
            item.title = tr("Вы");
            item.text = message.content;
            items.append(item);
            continue;
        }

        if (message.role == LlmRoleAssistant)
        {
            pendingCalls = message.toolCalls;
            if (!message.content.trimmed().isEmpty())
            {
                ChatDisplayItem item;
                item.kind = QString::fromUtf8("assistant");
                item.title = tr("Ассистент");
                item.text = message.content;
                items.append(item);
            }
            continue;
        }

        if (message.role == LlmRoleTool)
        {
            QString args;
            QString name = message.name;
            for (int i = 0; i < pendingCalls.size(); ++i)
            {
                if (pendingCalls.at(i).id == message.toolCallId
                    || (message.toolCallId.isEmpty() && pendingCalls.at(i).name == name))
                {
                    if (name.isEmpty())
                        name = pendingCalls.at(i).name;
                    args = pendingCalls.at(i).arguments;
                    pendingCalls.removeAt(i);
                    break;
                }
            }
            ChatDisplayItem item;
            item.kind = QString::fromUtf8("tool");
            item.title = tr("Функция");
            if (!args.isEmpty())
                item.text = QString::fromUtf8("%1(%2)\n%3").arg(name, args, message.content);
            else
                item.text = QString::fromUtf8("%1\n%2").arg(name, message.content);
            items.append(item);
        }
    }

    restoreDisplayLog(items);
}

//----------------------------------------------

void ConversationWidget::setUserInputEnabled(bool enabled)
{
    _userInputEnabled = enabled;
    if (_inputHost)
        _inputHost->hide();
}

//----------------------------------------------

bool ConversationWidget::userInputEnabled() const
{
    return _userInputEnabled;
}

//----------------------------------------------

void ConversationWidget::showPermissionRequest(QString action, bool offerAlways)
{
    Q_UNUSED(offerAlways);
    _promptMode = 1;
    if (_permissionLabel)
        _permissionLabel->setText(tr("Разрешить действие?\n%1").arg(action));
    if (_approveButton)
        _approveButton->setText(tr("Разрешить"));
    if (_alwaysButton)
    {
        _alwaysButton->setVisible(true);
        _alwaysButton->setText(tr("Всегда"));
    }
    if (_rejectButton)
        _rejectButton->setText(tr("Отклонить"));
    if (_permissionHost)
        _permissionHost->show();
}

//----------------------------------------------

void ConversationWidget::hidePermissionRequest()
{
    _promptMode = 0;
    if (_permissionHost)
        _permissionHost->hide();
}

//----------------------------------------------

void ConversationWidget::showContinuationRequest(QString error)
{
    _promptMode = 2;
    if (_permissionLabel)
        _permissionLabel->setText(
            tr("Работа прервана. Можно продолжить или завершить этот воркер.\n%1")
                .arg(error));
    if (_approveButton)
        _approveButton->setText(tr("Продолжить"));
    if (_alwaysButton)
    {
        _alwaysButton->setVisible(true);
        _alwaysButton->setText(tr("Всегда"));
    }
    if (_rejectButton)
        _rejectButton->setText(tr("Завершить"));
    if (_permissionHost)
        _permissionHost->show();
}

//----------------------------------------------

void ConversationWidget::sltPermissionApprove()
{
    const int mode = _promptMode;
    hidePermissionRequest();
    if (mode == 2)
        emit continuationAnswered(true);
    else
        emit permissionAnswered(true, false, QString());
}

//----------------------------------------------

void ConversationWidget::sltPermissionAlways()
{
    const int mode = _promptMode;
    hidePermissionRequest();
    if (mode == 2)
        emit continuationAnswered(true);
    else
        emit permissionAnswered(true, true, QString());
}

//----------------------------------------------

void ConversationWidget::sltPermissionReject()
{
    const int mode = _promptMode;
    hidePermissionRequest();
    if (mode == 2)
        emit continuationAnswered(false);
    else
        emit permissionAnswered(false, false, QString());
}

//----------------------------------------------

void ConversationWidget::recordDisplay(QString kind, QString title, QString text)
{
    if (_restoring)
        return;
    ChatDisplayItem item;
    item.kind = kind;
    item.title = title;
    item.text = text;
    _displayLog.append(item);
    while (_displayLog.size() > kMaxDisplayLogItems)
        _displayLog.removeFirst();
}

//----------------------------------------------

void ConversationWidget::replayDisplayItem(const ChatDisplayItem &item)
{
    const QString kind = item.kind;
    if (kind == QString::fromUtf8("user"))
        addPlainBubble(item.title, item.text, QString::fromUtf8("userMsg"), true);
    else if (kind == QString::fromUtf8("assistant"))
        addBubble(item.title, MarkdownFormatter::toHtml(item.text), item.text,
                  QString::fromUtf8("assistantMsg"), false, false);
    else if (kind == QString::fromUtf8("tool"))
        addPlainBubble(item.title, item.text, QString::fromUtf8("toolMsg"), false);
    else if (kind == QString::fromUtf8("error"))
        addPlainBubble(item.title, item.text, QString::fromUtf8("errorMsg"), false);
    else
        addPlainBubble(item.title, item.text, QString::fromUtf8("noticeMsg"), false);
}

//----------------------------------------------

void ConversationWidget::setStopEnabled(bool enabled)
{
    if (_stopButton)
        _stopButton->setEnabled(enabled);
    if (_interruptButton)
        _interruptButton->setEnabled(enabled);
}

//----------------------------------------------

void ConversationWidget::setReportToParentVisible(bool visible)
{
    if (_reportButton)
        _reportButton->setVisible(visible);
}

//----------------------------------------------

bool ConversationWidget::hasActivePrompt() const
{
    return _promptMode != 0 && _permissionHost && _permissionHost->isVisible();
}

//----------------------------------------------

void ConversationWidget::applyTheme()
{
    AppSettings *s = AppSettings::instance();
    const QString text = s->textColor().name();
    const QString panel = s->panelColor().name();
    const QString bubble = s->isLightTheme()
                           ? QString::fromUtf8("#f3f3f3")
                           : s->panelColor().lighter(114).name();
    const QString input = s->inputColor().name();
    const QString accent = s->accentColor().name();
    const QString user = s->chatUserColor().name();
    const QString assistant = s->chatAssistantColor().name();
    const QString other = s->chatOtherColor().name();
    const QString border = s->borderColor().name();
    const QString muted = s->mutedTextColor().name();
    const QString permBg = s->isLightTheme()
                           ? QString::fromUtf8("#fef2f2")
                           : QString::fromUtf8("#b44532");
    const QString permBorder = s->isLightTheme()
                               ? QString::fromUtf8("#fca5a5")
                               : QString::fromUtf8("#e06a4f");
    const QString permBtn = s->isLightTheme()
                            ? QString::fromUtf8("#ffffff")
                            : QString::fromUtf8("#8f3424");
    setStyleSheet(QString::fromUtf8(
        "ConversationWidget { background:%1; color:%2; }"
        "QProgressBar { background:%3; border:none; border-radius:4px; }"
        "QProgressBar::chunk { background:%4; border-radius:4px; }"
        "QScrollArea { background:%1; border:none; }"
        "QWidget#messagesHost { background:%1; }"
        "QFrame#userMsg QFrame#bubble { background:%6; border:1px solid %9;"
        "  border-radius:10px; }"
        "QFrame#assistantMsg QFrame#bubble { background:%7; border:none;"
        "  border-radius:8px; }"
        "QFrame#toolMsg QFrame#bubble { background:%8; border:1px solid %9;"
        "  border-radius:8px; }"
        "QFrame#noticeMsg QFrame#bubble { background:%8; border:1px solid %9;"
        "  border-radius:8px; }"
        "QFrame#errorMsg QFrame#bubble { background:%8; border:1px solid %9;"
        "  border-radius:8px; }"
        "QWidget#permissionHost { background:%PERMBG%; border-top:2px solid %PERMBORDER%; }"
        "QWidget#permissionHost QLabel { color:%2; }"
        "QWidget#permissionHost QPushButton { background:%PERMBTN%; color:%2;"
        "  border:1px solid %PERMBORDER%; }"
        "QLabel { color:%2; background:transparent; }"
        "QTextBrowser { color:%2; background:transparent; border:none; }"
        "QPlainTextEdit { background:%5; color:%2; border:1px solid %9;"
        "  border-radius:8px; padding:6px; }"
        "QPushButton { background:%3; color:%2; border:1px solid %9;"
        "  border-radius:4px; padding:5px 12px; }"
        "QPushButton:hover { background:%5; }"
        "QPushButton:disabled { color:%MUTED%; }")
                      .arg(panel, text, bubble, accent, input, user, assistant, other, border)
                      .replace(QString::fromUtf8("%MUTED%"), muted)
                      .replace(QString::fromUtf8("%PERMBG%"), permBg)
                      .replace(QString::fromUtf8("%PERMBORDER%"), permBorder)
                      .replace(QString::fromUtf8("%PERMBTN%"), permBtn));

    if (_messagesLayout)
    {
        for (int i = 0; i < _messagesLayout->count(); ++i)
        {
            QLayoutItem *item = _messagesLayout->itemAt(i);
            ChatMessageFrame *frame = item
                                      ? qobject_cast<ChatMessageFrame *>(item->widget())
                                      : 0;
            if (frame)
                frame->applyTheme();
        }
    }
}

//----------------------------------------------

void ConversationWidget::sltBusyChanged(bool busy)
{
    _sendButton->setEnabled(_userInputEnabled && !busy);
    _input->setReadOnly(!_userInputEnabled || busy);
    _compactButton->setEnabled(!busy);
    if (_interruptButton)
        _interruptButton->setEnabled(busy);
    if (_reportButton)
        _reportButton->setEnabled(true);
    if (busy)
    {
        if (_activityText.isEmpty())
            setActivity(tr("Ожидание ответа модели"));
    }
    else
        setActivity(QString());
}

//----------------------------------------------

void ConversationWidget::setExternalActivity(QString text)
{
    if (_externalActivity == text)
        return;
    _externalActivity = text;
    refreshActivity();
}

//----------------------------------------------

void ConversationWidget::setActivity(QString text)
{
    if (_activityText == text)
        return;
    _activityText = text;
    refreshActivity();
}

//----------------------------------------------

void ConversationWidget::refreshActivity()
{
    if (!_activityLabel || !_activityTimer)
        return;
    const QString shown = _externalActivity.isEmpty() ? _activityText : _externalActivity;
    if (shown != _shownActivity)
    {
        _shownActivity = shown;
        _activityClock.restart();
    }
    if (shown.isEmpty())
    {
        _activityTimer->stop();
        _activityLabel->hide();
        _activityLabel->clear();
        return;
    }
    if (!_activityTimer->isActive())
        _activityTimer->start();
    _activityLabel->show();
    sltActivityTick();
}

//----------------------------------------------

void ConversationWidget::sltActivityTick()
{
    if (!_activityLabel || _shownActivity.isEmpty())
        return;
    const qint64 seconds = _activityClock.isValid() ? _activityClock.elapsed() / 1000 : 0;
    QString elapsed;
    if (seconds >= 60)
        elapsed = tr("%1 мин %2 с").arg(seconds / 60).arg(seconds % 60, 2, 10, QChar('0'));
    else
        elapsed = tr("%1 с").arg(seconds);
    _activityLabel->setText(QString::fromUtf8("%1 \xC2\xB7 %2").arg(_shownActivity, elapsed));
}

//----------------------------------------------

void ConversationWidget::sltContextUsageChanged(int usedTokens, int limitTokens)
{
    if (limitTokens <= 0)
    {
        _contextBar->setValue(0);
        _contextLabel->setText(tr("Контекст: %1 ток. (лимит неизвестен)").arg(usedTokens));
        return;
    }

    const int permille = (int)qBound(qint64(0),
                                    qint64(usedTokens) * 1000 / qint64(limitTokens),
                                    qint64(1000));
    _contextBar->setValue(permille);
    const int percent = (permille + 5) / 10;
    const int reserve = qMax(0, limitTokens - usedTokens);
    _contextLabel->setText(tr("Контекст: %1 / %2 ток. (%3%) · запас %4")
                           .arg(usedTokens)
                           .arg(limitTokens)
                           .arg(percent)
                           .arg(reserve));

    QString chunk = QString::fromUtf8("#26a69a");
    if (percent >= 90)
        chunk = QString::fromUtf8("#e53935");
    else if (percent >= 75)
        chunk = QString::fromUtf8("#fb8c00");
    else if (percent >= 60)
        chunk = QString::fromUtf8("#fdd835");
    const QString track = AppSettings::instance()->isLightTheme()
                          ? QString::fromUtf8("#e5e5e5")
                          : QString::fromUtf8("#5a5a5a");
    _contextBar->setStyleSheet(
        QString::fromUtf8("QProgressBar { background:%2; border:none; border-radius:4px; }"
                            "QProgressBar::chunk { background:%1; border-radius:4px; }")
        .arg(chunk, track));
}

//----------------------------------------------

void ConversationWidget::sltCompactingChanged(bool compacting)
{
    if (compacting)
    {
        _contextLabel->setText(tr("Сжатие контекста — сохраняем суть беседы…"));
        setActivity(tr("Сжатие контекста"));
    }
    else if (_agent && _agent->isBusy())
        setActivity(tr("Ожидание ответа модели"));
}

//----------------------------------------------

bool ConversationWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == _input && event->type() == QEvent::KeyPress)
    {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter)
        {
            if (!(keyEvent->modifiers() & Qt::ShiftModifier))
            {
                sltSend();
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

//----------------------------------------------

void ConversationWidget::sltSend()
{
    if (!_userInputEnabled)
        return;
    const QString text = _input->toPlainText().trimmed();
    if (text.isEmpty())
        return;
    _input->clear();
    emit sendRequested(text);
}

//----------------------------------------------

void ConversationWidget::sltUserMessage(const QString &text)
{
    const QString title = _userInputEnabled ? tr("Вы") : tr("Задача");
    recordDisplay(QString::fromUtf8("user"), title, text);
    addPlainBubble(title, text, QString::fromUtf8("userMsg"), true);
}

//----------------------------------------------

void ConversationWidget::sltAssistantMessage(const QString &text)
{
    recordDisplay(QString::fromUtf8("assistant"), tr("Ассистент"), text);
    const QString html = MarkdownFormatter::toHtml(clipDisplay(text, 24000));
    addBubble(tr("Ассистент"), html, text, QString::fromUtf8("assistantMsg"), false, false);
}

//----------------------------------------------

void ConversationWidget::sltToolCallStarted(const QString &name, const QString &arguments)
{
    _pendingToolName = name;
    _pendingToolArgs = arguments;
    setActivity(tr("Выполняется функция %1").arg(name));
}

//----------------------------------------------

void ConversationWidget::sltToolCallFinished(const QString &name, const QString &result, bool ok)
{
    QString body = QString::fromUtf8("%1(%2)").arg(name, _pendingToolArgs);
    body += QChar('\n');
    body += result;
    const QString title = ok ? tr("Функция") : tr("Функция (ошибка)");
    recordDisplay(QString::fromUtf8("tool"), title, body);
    addPlainBubble(title,
                   body,
                   QString::fromUtf8("toolMsg"),
                   false);
    _pendingToolName.clear();
    _pendingToolArgs.clear();
    if (_agent && _agent->isBusy())
        setActivity(tr("Ожидание ответа модели"));
}

//----------------------------------------------

void ConversationWidget::sltNotice(const QString &text)
{
    recordDisplay(QString::fromUtf8("notice"), tr("Контекст"), text);
    addPlainBubble(tr("Контекст"), text, QString::fromUtf8("noticeMsg"), false);
}

//----------------------------------------------

void ConversationWidget::sltFailed(const QString &error)
{
    recordDisplay(QString::fromUtf8("error"), tr("Ошибка"), error);
    addPlainBubble(tr("Ошибка"), error, QString::fromUtf8("errorMsg"), false);
}

//----------------------------------------------

void ConversationWidget::sltScrollToBottom()
{
    _scrollScheduled = false;
    if (!_scroll)
        return;
    QScrollBar *bar = _scroll->verticalScrollBar();
    bar->setValue(bar->maximum());
}

//----------------------------------------------

void ConversationWidget::scheduleScrollToBottom()
{
    if (_scrollScheduled)
        return;
    _scrollScheduled = true;
    QTimer::singleShot(0, this, SLOT(sltScrollToBottom()));
}

//----------------------------------------------

void ConversationWidget::trimOldBubbles()
{
    if (!_messagesLayout)
        return;
    // В layout есть растяжка без виджета — её пропускаем, удаляем самый
    // старый пузырь (первый элемент с виджетом).
    while (_messagesLayout->count() > kMaxVisibleBubbles + 1)
    {
        int index = -1;
        for (int i = 0; i < _messagesLayout->count(); ++i)
        {
            QLayoutItem *probe = _messagesLayout->itemAt(i);
            if (probe && probe->widget())
            {
                index = i;
                break;
            }
        }
        if (index < 0)
            break;
        QLayoutItem *item = _messagesLayout->takeAt(index);
        if (!item)
            break;
        if (item->widget())
        {
            item->widget()->hide();
            item->widget()->deleteLater();
        }
        delete item;
    }
}

//----------------------------------------------

void ConversationWidget::sltScrollRangeChanged(int, int)
{
    if (_pinBottom)
        sltScrollToBottom();
}

//----------------------------------------------

QWidget *ConversationWidget::addBubble(QString title, QString html, QString preview,
                                       QString objectName, bool alignRight, bool collapsed)
{
    ChatMessageFrame *frame = new ChatMessageFrame(title, html, preview, objectName,
                                                   alignRight, collapsed, _messagesHost);
    _messagesLayout->addWidget(frame, 0, Qt::AlignTop);
    trimOldBubbles();
    _pinBottom = true;
    if (!_restoring)
        scheduleScrollToBottom();
    return frame;
}

//----------------------------------------------

QWidget *ConversationWidget::addPlainBubble(QString title, QString text, QString objectName,
                                            bool alignRight, bool forceCollapsed,
                                            bool forceExpanded)
{
    bool collapsed = (objectName != QString::fromUtf8("userMsg")
                      && objectName != QString::fromUtf8("assistantMsg"));
    if (forceCollapsed)
        collapsed = true;
    if (forceExpanded)
        collapsed = false;
    QString html = QString::fromUtf8("<p style=\"white-space:pre-wrap;\">");
    html += MarkdownFormatter::escapeHtml(clipDisplay(text, 12000));
    html += QString::fromUtf8("</p>");
    return addBubble(title, html, text, objectName, alignRight, collapsed);
}

//----------------------------------------------

void ConversationWidget::removeBubble(QWidget *bubble)
{
    if (!bubble || !_messagesLayout)
        return;
    for (int i = 0; i < _messagesLayout->count(); ++i)
    {
        QLayoutItem *item = _messagesLayout->itemAt(i);
        if (!item || item->widget() != bubble)
            continue;
        _messagesLayout->takeAt(i);
        delete item;
        break;
    }
    bubble->hide();
    bubble->deleteLater();
}

//----------------------------------------------

void ConversationWidget::setContextRowVisible(bool visible)
{
    if (_contextLabel)
        _contextLabel->setVisible(visible);
    if (_contextBar)
        _contextBar->setVisible(visible);
    if (_compactButton)
        _compactButton->setVisible(visible);
}
