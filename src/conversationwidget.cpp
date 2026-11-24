#include "conversationwidget.h"
//----------------------------------------------
#include "agent.h"
#include "applocale.h"
#include "appsettings.h"
#include "markdownformatter.h"
//----------------------------------------------
#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QCheckBox>
#include <QColor>
#include <QDesktopServices>
#include <QEvent>
#include <QFont>
#include <QCursor>
#include <QHBoxLayout>
#include <QLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRegExp>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QSizePolicy>
#include <QStringList>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTextEdit>
#include <QImage>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QtGlobal>
#include <QtMath>

//----------------------------------------------

// How many bubbles stay in the widget, and how many feed records are stored on disk.
static const int kMaxVisibleBubbles  = 250;
static const int kMaxDisplayLogItems = 1500;
static const int kLiveActionHeight   = 24;

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

//----------------------------------------------

QString AuthorBadge::normalizeKey(QString author)
{
    QString key = author.trimmed();
    if (key.isEmpty())
        return QString::fromUtf8("?");
    // Every variant that comes from the user uses one color.
    if (key == QString::fromUtf8("Вы")
        || key == QString::fromUtf8("Задача")
        || key.startsWith(QString::fromUtf8("Пользователь")))
        return QString::fromUtf8("Пользователь");
    // "Name · Created" / "Name · Error" takes its color from the name.
    const int sep = key.indexOf(QString::fromUtf8(" \xC2\xB7 "));
    if (sep > 0)
        key = key.left(sep).trimmed();
    return key;
}

//----------------------------------------------

QColor AuthorBadge::colorFor(QString authorKey)
{
    // Dark shades aligned with the blue accent and the worker-graph palette.
    static const int kHues[] = {
        210, 160, 355, 280, 190, 25, 240, 330, 145, 40, 200, 300
    };
    const QString key = normalizeKey(authorKey);
    const uint h = qHash(key);
    const int hue = kHues[h % 12];
    const int sat = 52 + int((h >> 4) % 18);   // 52..69 %
    const bool light = AppSettings::instance()->isLightTheme();
    // On a light theme they stay dark; on a dark theme they are a bit lighter so they stay readable.
    const int lit = light
        ? (26 + int((h >> 8) % 12))            // 26..37 %
        : (52 + int((h >> 8) % 12));           // 52..63 %
    return QColor::fromHsl(hue,
                           qBound(0, int(sat * 255 / 100), 255),
                           qBound(0, int(lit * 255 / 100), 255));
}

//----------------------------------------------

class ChatTextView : public QTextBrowser
{
public:
    explicit ChatTextView(QWidget *parent)
        : QTextBrowser(parent)
    {
    }

protected:
    QVariant loadResource(int type, const QUrl &name)
    {
        if (type == QTextDocument::ImageResource)
        {
            QString path;
            if (name.isLocalFile())
                path = name.toLocalFile();
            else if (name.scheme().isEmpty())
                path = name.toLocalFile();
            QImage image(path);
            if (!image.isNull())
            {
                const int maxWidth = 720;
                if (image.width() > maxWidth)
                    image = image.scaledToWidth(maxWidth, Qt::SmoothTransformation);
                return image;
            }
        }
        return QTextBrowser::loadResource(type, name);
    }
};

//----------------------------------------------

static QString imageBaseFor(const Agent *agent)
{
    if (!agent)
        return QString();
    return agent->projectDirectory();
}

//----------------------------------------------

QString AuthorBadge::titleHtml(QString title, QString authorKey, QString linkUrl)
{
    const QString shown = title.trimmed().isEmpty() ? QString::fromUtf8("?") : title.trimmed();
    const QColor color = colorFor(authorKey.isEmpty() ? shown : authorKey);
    const QString escaped = MarkdownFormatter::escapeHtml(shown);
    if (linkUrl.trimmed().isEmpty())
    {
        return QString::fromUtf8(
                   "<span style=\"font-weight:700; color:%1;\">%2</span>")
            .arg(color.name(), escaped);
    }
    return QString::fromUtf8(
               "<a href=\"%1\" style=\"font-weight:700; color:%2; "
               "text-decoration:underline;\">%3</a>")
        .arg(MarkdownFormatter::escapeHtml(linkUrl.trimmed()), color.name(), escaped);
}

//----------------------------------------------

ChatMessageFrame::ChatMessageFrame(QString title, QString html, QString preview,
                                   QString objectName, bool alignRight, bool collapsed,
                                   QWidget *parent, QString authorKey)
    : QFrame(parent)
    , _title(title)
    , _authorKey(authorKey.trimmed().isEmpty() ? title : authorKey)
    , _titleLink()
    , _bodyHtml(html)
    , _preview(firstPhrase(preview))
    , _view(new ChatTextView(this))
    , _toggle(new QPushButton(this))
    , _collapsed(collapsed)
    , _fitting(false)
    , _fitScheduled(false)
    , _lastFitWidth(-1)
    , _lastFitHeight(-1)
{
    setObjectName(objectName);
    setFrameShape(QFrame::NoFrame);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);

    _toggle->setFixedSize(22, 22);
    _toggle->setFlat(true);
    _toggle->setCursor(Qt::PointingHandCursor);
    _toggle->setToolTip(tr("Свернуть / развернуть"));
    connect(_toggle, SIGNAL(clicked()), this, SLOT(sltToggle()));

    _view->setFrameShape(QFrame::NoFrame);
    _view->setOpenExternalLinks(false);
    _view->setOpenLinks(false);
    _view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    _view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    _view->setUndoRedoEnabled(false);
    _view->setLineWrapMode(QTextEdit::WidgetWidth);
    _view->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    _view->document()->setDocumentMargin(2);
    connect(_view, SIGNAL(anchorClicked(QUrl)), this, SLOT(sltAnchorClicked(QUrl)));
    if (_view->document()->documentLayout())
    {
        connect(_view->document()->documentLayout(),
                SIGNAL(documentSizeChanged(QSizeF)),
                this, SLOT(sltDocumentSizeChanged()));
    }
    applyTheme();

    QHBoxLayout *inner = new QHBoxLayout();
    inner->setContentsMargins(12, 8, 12, 8);
    inner->setSpacing(6);
    inner->addWidget(_toggle, 0, Qt::AlignTop);
    inner->addWidget(_view, 1);

    QFrame *bubble = new QFrame(this);
    bubble->setObjectName(QString::fromUtf8("bubble"));
    bubble->setLayout(inner);
    bubble->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);

    QHBoxLayout *row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
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
            "p { margin: 0 0 12px 0; color: %1; }"
            "pre { margin: 8px 0 12px 0; }"
            "ul, ol { margin: 4px 0 12px 18px; }"
            "h2, h3, h4 { margin: 16px 0 6px 0; color: %1; }"
            "a { color: %2; }")
                                                    .arg(text, accent));
    }
    // Rebuild the caption HTML: the author color depends on the light or dark theme.
    applyCollapsed();
}

//----------------------------------------------

QString ChatMessageFrame::titleBadgeHtml() const
{
    return AuthorBadge::titleHtml(_title, _authorKey, _titleLink);
}

//----------------------------------------------

void ChatMessageFrame::setTitleLink(QString url)
{
    const QString next = url.trimmed();
    if (_titleLink == next)
        return;
    _titleLink = next;
    applyCollapsed();
}

//----------------------------------------------

void ChatMessageFrame::setCaption(QString title, QString authorKey)
{
    const QString key = authorKey.trimmed().isEmpty() ? title : authorKey.trimmed();
    if (_title == title && _authorKey == key)
        return;
    _title = title;
    _authorKey = key;
    applyCollapsed();
}

//----------------------------------------------

void ChatMessageFrame::setPreview(QString preview)
{
    _preview = preview.trimmed();
    applyCollapsed();
}

//----------------------------------------------

void ChatMessageFrame::sltAnchorClicked(const QUrl &url)
{
    if (!url.isValid())
        return;
    if (url.scheme() == QString::fromUtf8("unique-worker"))
    {
        emit linkActivated(url);
        return;
    }
    QDesktopServices::openUrl(url);
}

//----------------------------------------------

void ChatMessageFrame::appendEscapedParagraph(QString text)
{
    QString chunk = QString::fromUtf8("<p style=\"white-space:pre-wrap;\">");
    chunk += MarkdownFormatter::escapeHtml(text);
    chunk += QString::fromUtf8("</p>");
    _bodyHtml += chunk;
    applyCollapsed();
}

//----------------------------------------------

void ChatMessageFrame::appendHtmlParagraph(QString html, QString previewSource)
{
    Q_UNUSED(previewSource);
    _bodyHtml += html;
    applyCollapsed();
}

//----------------------------------------------

QString ChatMessageFrame::collapsedHtml() const
{
    QString line = titleBadgeHtml();
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
    QString html = QString::fromUtf8("<p>%1</p>").arg(titleBadgeHtml());
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

    // documentSize() finishes lazy layout; otherwise the height is only
    // the already visible piece, and expanded text stays clipped.
    qreal docH = _view->document()->documentLayout()
                     ? _view->document()->documentLayout()->documentSize().height()
                     : _view->document()->size().height();
    int chrome = 0;
    if (_view->viewport())
        chrome = _view->height() - _view->viewport()->height();
    if (chrome < 0)
        chrome = 0;
    const int h = qMax(_collapsed ? 22 : 28, qCeil(docH) + chrome + 2);
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

void ChatMessageFrame::sltDocumentSizeChanged()
{
    if (_fitting || _fitScheduled)
        return;
    _fitScheduled = true;
    QTimer::singleShot(0, this, SLOT(sltFitDocumentHeight()));
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
    , _liveActionLine(0)
    , _activityTimer(0)
    , _contextBar(0)
    , _compactButton(0)
    , _scroll(0)
    , _messagesHost(0)
    , _messagesLayout(0)
    , _input(0)
    , _sendButton(0)
    , _autoApproveBox(0)
    , _stopButton(0)
    , _interruptButton(0)
    , _reportButton(0)
    , _inputHost(0)
    , _permissionHost(0)
    , _permissionLabel(0)
    , _consoleEdit(0)
    , _approveButton(0)
    , _alwaysButton(0)
    , _rejectButton(0)
    , _promptMode(0)
    , _pinBottom(true)
    , _restoring(false)
    , _userInputEnabled(true)
    , _scrollScheduled(false)
    , _contextUsedTokens(0)
    , _contextLimitTokens(0)
    , _contextCompacting(false)
{
    _contextLabel = new QLabel(this);
    _contextLabel->hide();
    _contextBar = new QProgressBar(this);
    _contextBar->setRange(0, 1000);
    _contextBar->setValue(0);
    _contextBar->setTextVisible(true);
    _contextBar->setAlignment(Qt::AlignCenter);
    _contextBar->setFixedHeight(18);
    _contextBar->setMinimumWidth(72);
    _contextBar->setMaximumWidth(160);
    _contextBar->setFormat(QString::fromUtf8("—"));

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
    contextRow->setContentsMargins(12, 10, 12, 8);
    contextRow->setSpacing(8);
    contextRow->addWidget(_contextBar);
    contextRow->addStretch(1);
    contextRow->addWidget(_compactButton);
    contextRow->addWidget(_interruptButton);
    contextRow->addWidget(_reportButton);

    _messagesHost = new QWidget(this);
    _messagesHost->setObjectName(QString::fromUtf8("messagesHost"));
    _messagesHost->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    _messagesLayout = new QVBoxLayout(_messagesHost);
    _messagesLayout->setContentsMargins(12, 4, 12, 8);
    _messagesLayout->setSpacing(8);
    _messagesLayout->addStretch(1);

    _liveActionLine = new QLabel(_messagesHost);
    _liveActionLine->setObjectName(QString::fromUtf8("chatLiveAction"));
    _liveActionLine->setWordWrap(false);
    _liveActionLine->setTextInteractionFlags(Qt::NoTextInteraction);
    _liveActionLine->setFixedHeight(kLiveActionHeight);
    _liveActionLine->setMinimumHeight(kLiveActionHeight);
    _liveActionLine->setMaximumHeight(kLiveActionHeight);
    _liveActionLine->setContentsMargins(12, 0, 12, 0);
    _liveActionLine->setText(QString::fromUtf8(" "));
    _liveActionLine->show();
    _messagesLayout->addWidget(_liveActionLine, 0, Qt::AlignTop);

    _scroll = new QScrollArea(this);
    _scroll->setWidgetResizable(true);
    _scroll->setFrameShape(QFrame::NoFrame);
    _scroll->setWidget(_messagesHost);
    _scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(_scroll->verticalScrollBar(), SIGNAL(rangeChanged(int,int)),
            this, SLOT(sltScrollRangeChanged(int,int)));
    connect(_scroll->verticalScrollBar(), SIGNAL(valueChanged(int)),
            this, SLOT(sltScrollValueChanged(int)));

    _input = new QPlainTextEdit(this);
    _input->setPlaceholderText(tr("Сообщение…  Enter — отправить, Shift+Enter — новая строка"));
    _input->setMaximumHeight(110);
    _input->installEventFilter(this);

    _sendButton = new QPushButton(tr("Отправить"), this);
    _autoApproveBox = new QCheckBox(tr("Автоподтверждение"), this);
    _autoApproveBox->setToolTip(
        tr("Одобрять запросы подтверждения от моделей без вопроса"));
    _autoApproveBox->setChecked(AppSettings::instance()->autoApprovePermissions());
    _stopButton = new QPushButton(tr("Стоп"), this);
    _stopButton->setEnabled(false);
    _stopButton->setToolTip(tr("Остановить работу во всех чатах"));
    connect(_sendButton, SIGNAL(clicked()), this, SLOT(sltSend()));
    connect(_stopButton, SIGNAL(clicked()), this, SIGNAL(stopRequested()));
    connect(_autoApproveBox, SIGNAL(toggled(bool)),
            this, SLOT(sltAutoApproveToggled(bool)));
    connect(AppSettings::instance(), SIGNAL(displayOptionsChanged()),
            this, SLOT(sltDisplayOptionsChanged()));

    QHBoxLayout *stopRow = new QHBoxLayout();
    stopRow->setContentsMargins(0, 0, 0, 0);
    stopRow->setSpacing(8);
    stopRow->addWidget(_autoApproveBox);
    stopRow->addWidget(_stopButton);

    QVBoxLayout *buttons = new QVBoxLayout();
    buttons->addWidget(_sendButton);
    buttons->addLayout(stopRow);
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
    _consoleEdit = new QLineEdit(this);
    _consoleEdit->setPlaceholderText(tr("Ввод для консоли…"));
    _consoleEdit->hide();
    connect(_consoleEdit, SIGNAL(returnPressed()),
            this, SLOT(sltPermissionApprove()));
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
    permissionLayout->setContentsMargins(12, 10, 12, 10);
    permissionLayout->setSpacing(8);
    permissionLayout->addWidget(_permissionLabel);
    permissionLayout->addWidget(_consoleEdit);
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

void ConversationWidget::retranslateUi()
{
    if (_compactButton)
    {
        _compactButton->setText(tr("Сжать контекст"));
        _compactButton->setToolTip(tr("Свернуть старую историю в краткое содержание, "
                                      "чтобы продолжить беседу в окне модели."));
    }
    if (_interruptButton)
    {
        _interruptButton->setText(tr("Прервать"));
        _interruptButton->setToolTip(tr("Принудительно остановить этот воркер, "
                                        "включая зависшую команду."));
    }
    if (_reportButton)
    {
        _reportButton->setText(tr("Отчёт родителю"));
        _reportButton->setToolTip(tr("Прервать работу и сразу отправить родителю "
                                     "текущий отчёт."));
    }
    if (_sendButton)
        _sendButton->setText(tr("Отправить"));
    if (_autoApproveBox)
    {
        _autoApproveBox->setText(tr("Автоподтверждение"));
        _autoApproveBox->setToolTip(
            tr("Одобрять запросы подтверждения от моделей без вопроса"));
    }
    if (_stopButton)
    {
        _stopButton->setText(tr("Стоп"));
        _stopButton->setToolTip(tr("Остановить работу во всех чатах"));
    }
    if (_input)
        _input->setPlaceholderText(
            tr("Сообщение…  Enter — отправить, Shift+Enter — новая строка"));
    if (_approveButton)
        _approveButton->setText(tr("Разрешить"));
    if (_alwaysButton)
    {
        _alwaysButton->setText(tr("Всегда"));
        _alwaysButton->setToolTip(tr("Разрешать эту программу во всём приложении, "
                                      "пока пути и действие остаются в каталоге проекта"));
    }
    if (_rejectButton)
        _rejectButton->setText(tr("Отклонить"));
    if (_consoleEdit)
        _consoleEdit->setPlaceholderText(tr("Ввод для консоли…"));
    if (_promptMode == 5)
    {
        if (_approveButton)
            _approveButton->setText(tr("Отправить"));
        if (_rejectButton)
            _rejectButton->setText(tr("Пропустить"));
    }

    if (_contextBar)
        refreshContextBar();

    // Bubble captions follow kind, not the language saved with the record.
    refreshDisplayTitles();
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
    connect(_agent, SIGNAL(serviceNotice(QString)),
            this, SLOT(sltNotice(QString)));
    connect(_agent, SIGNAL(forwardedContext(QString)),
            this, SLOT(sltForwardedContext(QString)));
    connect(_agent, SIGNAL(failed(QString)),
            this, SLOT(sltFailed(QString)));
    connect(_agent, SIGNAL(busyChanged(bool)),
            this, SLOT(sltBusyChanged(bool)));
    connect(_agent, SIGNAL(contextUsageChanged(int,int)),
            this, SLOT(sltContextUsageChanged(int,int)));
    connect(_agent, SIGNAL(compactingChanged(bool)),
            this, SLOT(sltCompactingChanged(bool)));
    connect(_agent, SIGNAL(toolBudgetExhausted(int)),
            this, SLOT(sltToolBudgetExhausted(int)));
    _agent->refreshContextUsage();
    if (_agent->isWaitingToolBudget())
        showToolBudgetRequest(_agent->toolBudgetChunk());
    syncRunningState();
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
            // Only message bubbles: service rows (chat and journal)
            // and the layout stretch must not be touched.
            if (!qobject_cast<ChatMessageFrame *>(item->widget()))
                continue;
            _messagesLayout->takeAt(i);
            item->widget()->deleteLater();
            delete item;
        }
    }
    ensureLiveActionAtEnd();
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
    // Only the tail of the feed is put on screen: hundreds of QTextBrowser
    // widgets in a hidden tab cost memory and endless layout recalculations.
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

static bool looksLikeInjectedStartupContext(const QString &text)
{
    // Old chats without hide_in_ui: the worker's service prefix.
    return text.startsWith(QString::fromUtf8("Worker tree limits:"))
           || text.startsWith(QString::fromUtf8("You are a shared tool outside"))
           || text.contains(QString::fromUtf8("Available worker presets (pick preset_id"))
           || text.startsWith(QString::fromUtf8("Before any other action call"))
           || text.startsWith(QString::fromUtf8("You stopped in the middle"))
           || text.startsWith(QString::fromUtf8("Previous model request failed"))
           || text.startsWith(QString::fromUtf8("You still have not called complete_task"));
}

//----------------------------------------------

void ConversationWidget::restoreFromHistory(const QList<LlmMessage> &history)
{
    QList<ChatDisplayItem> items;
    bool skipFirstSystem = true;
    QList<LlmToolCall> pendingCalls;

    foreach (const LlmMessage &message, history)
    {
        if (message.hideInUi)
            continue;

        if (message.role == LlmRoleSystem)
        {
            if (skipFirstSystem)
            {
                skipFirstSystem = false;
                continue;
            }
            ChatDisplayItem item;
            item.kind = QString::fromUtf8("forwarded");
            item.title = titleForKind(item.kind);
            item.authorKey = item.kind;
            item.text = message.content;
            items.append(item);
            continue;
        }

        if (message.role == LlmRoleUser)
        {
            if (looksLikeInjectedStartupContext(message.content.trimmed()))
                continue;
            ChatDisplayItem item;
            item.kind = QString::fromUtf8("user");
            item.title = titleForKind(item.kind);
            item.authorKey = item.kind;
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
                item.title = titleForKind(item.kind);
                item.authorKey = item.title;
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
            item.title = titleForKind(item.kind);
            item.authorKey = item.kind;
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

void ConversationWidget::setAuthorLabels(QString userAuthor, QString assistantAuthor)
{
    _userAuthor = userAuthor.trimmed();
    _assistantAuthor = assistantAuthor.trimmed();
}

//----------------------------------------------

QString ConversationWidget::userAuthorLabel() const
{
    if (!_userAuthor.isEmpty())
        return _userAuthor;
    return _userInputEnabled ? tr("Вы") : tr("Пользователь");
}

//----------------------------------------------

QString ConversationWidget::assistantAuthorLabel() const
{
    if (!_assistantAuthor.isEmpty())
        return _assistantAuthor;
    return tr("Ассистент");
}

//----------------------------------------------

QString ConversationWidget::titleForKind(const QString &kind) const
{
    if (kind == QString::fromUtf8("assignment"))
        return tr("Задание");
    if (kind == QString::fromUtf8("followup"))
        return tr("Уточнение");
    if (kind == QString::fromUtf8("group"))
        return tr("Группа");
    if (kind == QString::fromUtf8("user"))
        return userAuthorLabel();
    if (kind == QString::fromUtf8("assistant"))
        return assistantAuthorLabel();
    if (kind == QString::fromUtf8("tool"))
        return tr("Функция");
    if (kind == QString::fromUtf8("tool_error"))
        return tr("Функция (ошибка)");
    if (kind == QString::fromUtf8("notice"))
        return tr("Заметка");
    if (kind == QString::fromUtf8("forwarded"))
        return tr("Контекст");
    if (kind == QString::fromUtf8("error"))
        return tr("Ошибка");
    return QString();
}

//----------------------------------------------

QString ConversationWidget::normalizeDisplayKind(const ChatDisplayItem &item) const
{
    const QString kind = item.kind.trimmed();
    if (kind == QString::fromUtf8("assignment")
        || kind == QString::fromUtf8("followup")
        || kind == QString::fromUtf8("group")
        || kind == QString::fromUtf8("user")
        || kind == QString::fromUtf8("assistant")
        || kind == QString::fromUtf8("tool")
        || kind == QString::fromUtf8("tool_error")
        || kind == QString::fromUtf8("notice")
        || kind == QString::fromUtf8("forwarded")
        || kind == QString::fromUtf8("error"))
        return kind;

    const QString title = item.title.trimmed();
    const QString key = item.authorKey.trimmed();
    const QString probe = key.isEmpty() ? title : key;
    if (probe == QString::fromUtf8("Задание") || probe == QString::fromUtf8("Task")
        || probe == QString::fromUtf8("任务") || title == tr("Задание"))
        return QString::fromUtf8("assignment");
    if (probe == QString::fromUtf8("Уточнение") || probe == QString::fromUtf8("Clarification")
        || probe == QString::fromUtf8("澄清") || title == tr("Уточнение"))
        return QString::fromUtf8("followup");
    if (probe == QString::fromUtf8("Группа") || probe == QString::fromUtf8("Group")
        || probe == QString::fromUtf8("群组") || title == tr("Группа"))
        return QString::fromUtf8("group");
    if (title == QString::fromUtf8("Функция (ошибка)")
        || title == QString::fromUtf8("Function (error)")
        || title == QString::fromUtf8("函数（错误）")
        || title == tr("Функция (ошибка)"))
        return QString::fromUtf8("tool_error");
    if (title == QString::fromUtf8("Функция") || title == QString::fromUtf8("Function")
        || title == QString::fromUtf8("函数") || title == tr("Функция")
        || kind == QString::fromUtf8("tool"))
        return QString::fromUtf8("tool");
    if (title == QString::fromUtf8("Заметка") || title == QString::fromUtf8("Note")
        || title == QString::fromUtf8("备注") || title == tr("Заметка"))
        return QString::fromUtf8("notice");
    if (title == QString::fromUtf8("Контекст") || title == QString::fromUtf8("Context")
        || title == QString::fromUtf8("上下文") || title == tr("Контекст"))
        return QString::fromUtf8("forwarded");
    if (title == QString::fromUtf8("Ошибка") || title == QString::fromUtf8("Error")
        || title == QString::fromUtf8("错误") || title == tr("Ошибка")
        || kind == QString::fromUtf8("error"))
        return QString::fromUtf8("error");
    if (kind == QString::fromUtf8("assistant"))
        return QString::fromUtf8("assistant");
    if (kind == QString::fromUtf8("user"))
        return QString::fromUtf8("user");
    return kind;
}

//----------------------------------------------

void ConversationWidget::refreshDisplayTitles()
{
    if (_displayLog.isEmpty())
        return;
    for (int i = 0; i < _displayLog.size(); ++i)
    {
        ChatDisplayItem &item = _displayLog[i];
        const QString kind = normalizeDisplayKind(item);
        item.kind = kind;
        const QString fresh = titleForKind(kind);
        if (!fresh.isEmpty())
            item.title = fresh;
        if (kind == QString::fromUtf8("assignment")
            || kind == QString::fromUtf8("followup")
            || kind == QString::fromUtf8("group")
            || kind == QString::fromUtf8("tool")
            || kind == QString::fromUtf8("tool_error")
            || kind == QString::fromUtf8("notice")
            || kind == QString::fromUtf8("forwarded")
            || kind == QString::fromUtf8("error"))
            item.authorKey = kind;
        else if (kind == QString::fromUtf8("assistant"))
            item.authorKey = assistantAuthorLabel();
        else if (kind == QString::fromUtf8("user"))
            item.authorKey = userAuthorLabel();
    }

    QList<ChatMessageFrame *> frames;
    if (_messagesLayout)
    {
        for (int i = 0; i < _messagesLayout->count(); ++i)
        {
            QLayoutItem *layoutItem = _messagesLayout->itemAt(i);
            if (!layoutItem)
                continue;
            ChatMessageFrame *frame = qobject_cast<ChatMessageFrame *>(layoutItem->widget());
            if (frame)
                frames.append(frame);
        }
    }
    int first = 0;
    if (_displayLog.size() > kMaxVisibleBubbles)
        first = _displayLog.size() - kMaxVisibleBubbles;
    if (frames.size() != _displayLog.size() - first)
    {
        restoreDisplayLog(_displayLog);
        return;
    }
    for (int i = 0; i < frames.size(); ++i)
        frames.at(i)->setCaption(_displayLog.at(first + i).title,
                                 _displayLog.at(first + i).authorKey);
}

//----------------------------------------------

void ConversationWidget::showPermissionRequest(QString action, bool offerAlways)
{
    _promptMode = 1;
    if (_permissionLabel)
        _permissionLabel->setText(tr("Разрешить действие?\n%1").arg(action));
    if (_approveButton)
        _approveButton->setText(tr("Разрешить"));
    if (_alwaysButton)
    {
        _alwaysButton->setVisible(offerAlways);
        _alwaysButton->setText(tr("Всегда"));
    }
    if (_rejectButton)
        _rejectButton->setText(tr("Отклонить"));
    if (_consoleEdit)
        _consoleEdit->hide();
    if (_permissionHost)
        _permissionHost->show();
}

//----------------------------------------------

void ConversationWidget::hidePermissionRequest()
{
    _promptMode = 0;
    if (_consoleEdit)
        _consoleEdit->hide();
    if (_permissionHost)
        _permissionHost->hide();
}

//----------------------------------------------

void ConversationWidget::showConsoleInput(QString transcript)
{
    _promptMode = 5;
    QString tail = transcript;
    if (tail.size() > 1200)
        tail = tail.right(1200);
    if (_permissionLabel)
        _permissionLabel->setText(tr("Команда ждёт ввод в консоли:\n%1").arg(tail));
    if (_consoleEdit)
    {
        _consoleEdit->clear();
        _consoleEdit->setPlaceholderText(tr("Ввод для консоли…"));
        _consoleEdit->show();
        _consoleEdit->setFocus();
    }
    if (_approveButton)
        _approveButton->setText(tr("Отправить"));
    if (_alwaysButton)
        _alwaysButton->setVisible(false);
    if (_rejectButton)
        _rejectButton->setText(tr("Пропустить"));
    if (_permissionHost)
        _permissionHost->show();
}

//----------------------------------------------

void ConversationWidget::showUserQuestion(QString question)
{
    _promptMode = 6;
    if (_permissionLabel)
        _permissionLabel->setText(tr("Уточнение:\n%1").arg(question));
    if (_consoleEdit)
    {
        _consoleEdit->clear();
        _consoleEdit->setPlaceholderText(tr("Ответ…"));
        _consoleEdit->show();
        _consoleEdit->setFocus();
    }
    if (_approveButton)
        _approveButton->setText(tr("Ответить"));
    if (_alwaysButton)
        _alwaysButton->setVisible(false);
    if (_rejectButton)
        _rejectButton->setText(tr("Отмена"));
    if (_permissionHost)
        _permissionHost->show();
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
    if (_consoleEdit)
        _consoleEdit->hide();
    if (_permissionHost)
        _permissionHost->show();
}

//----------------------------------------------

void ConversationWidget::showToolBudgetRequest(int chunk)
{
    _promptMode = 3;
    const int n = chunk > 0 ? chunk : 0;
    if (_permissionLabel)
        _permissionLabel->setText(
            tr("Бюджет вызовов функций исчерпан (%1).\n"
               "Сбросить лимиты и продолжить или завершить?")
                .arg(n));
    if (_approveButton)
        _approveButton->setText(tr("Сбросить лимиты"));
    if (_alwaysButton)
        _alwaysButton->setVisible(false);
    if (_rejectButton)
        _rejectButton->setText(tr("Завершить"));
    if (_consoleEdit)
        _consoleEdit->hide();
    if (_permissionHost)
        _permissionHost->show();
}

//----------------------------------------------

void ConversationWidget::showSubtreeBudgetRequest(QString text)
{
    _promptMode = 4;
    if (_permissionLabel)
        _permissionLabel->setText(text);
    if (_approveButton)
        _approveButton->setText(tr("Сбросить лимиты"));
    if (_alwaysButton)
        _alwaysButton->setVisible(false);
    if (_rejectButton)
        _rejectButton->setText(tr("Запретить"));
    if (_consoleEdit)
        _consoleEdit->hide();
    if (_permissionHost)
        _permissionHost->show();
}

//----------------------------------------------

void ConversationWidget::sltToolBudgetExhausted(int chunk)
{
    showToolBudgetRequest(chunk);
}

//----------------------------------------------

void ConversationWidget::sltPermissionApprove()
{
    const int mode = _promptMode;
    QString line;
    if ((mode == 5 || mode == 6) && _consoleEdit)
        line = _consoleEdit->text();
    hidePermissionRequest();
    if (mode == 5 || mode == 6)
    {
        emit consoleInputAnswered(line, true);
        return;
    }
    if (mode == 4)
    {
        emit subtreeBudgetAnswered(true);
        return;
    }
    if (mode == 3)
    {
        if (_agent)
            _agent->resolveToolBudget(true);
        return;
    }
    if (mode == 2)
        emit continuationAnswered(true);
    else
        emit permissionAnswered(true, false, QString());
}

//----------------------------------------------

void ConversationWidget::sltPermissionAlways()
{
    const int mode = _promptMode;
    QString line;
    if ((mode == 5 || mode == 6) && _consoleEdit)
        line = _consoleEdit->text();
    hidePermissionRequest();
    if (mode == 5 || mode == 6)
    {
        emit consoleInputAnswered(line, true);
        return;
    }
    if (mode == 4)
    {
        emit subtreeBudgetAnswered(true);
        return;
    }
    if (mode == 3)
    {
        if (_agent)
            _agent->resolveToolBudget(true);
        return;
    }
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
    if (mode == 5 || mode == 6)
    {
        emit consoleInputAnswered(QString(), false);
        return;
    }
    if (mode == 4)
    {
        emit subtreeBudgetAnswered(false);
        return;
    }
    if (mode == 3)
    {
        if (_agent)
            _agent->resolveToolBudget(false);
        return;
    }
    if (mode == 2)
        emit continuationAnswered(false);
    else
        emit permissionAnswered(false, false, QString());
}

//----------------------------------------------

void ConversationWidget::recordDisplay(QString kind, QString title, QString text,
                                        QString authorKey)
{
    if (_restoring)
        return;
    ChatDisplayItem item;
    item.kind = kind;
    item.title = title;
    item.text = text;
    item.authorKey = authorKey.trimmed().isEmpty() ? title : authorKey;
    _displayLog.append(item);
    while (_displayLog.size() > kMaxDisplayLogItems)
        _displayLog.removeFirst();
}

//----------------------------------------------

void ConversationWidget::replayDisplayItem(const ChatDisplayItem &item)
{
    const QString kind = normalizeDisplayKind(item);
    const QString localized = titleForKind(kind);
    const QString title = localized.isEmpty() ? item.title : localized;
    const QString key = item.authorKey.isEmpty() ? title : item.authorKey;
    if (kind == QString::fromUtf8("user")
        || kind == QString::fromUtf8("assignment")
        || kind == QString::fromUtf8("followup")
        || kind == QString::fromUtf8("group"))
        addPlainBubble(title, item.text, QString::fromUtf8("userMsg"), true,
                       false, false, key);
    else if (kind == QString::fromUtf8("assistant"))
        addBubble(title, MarkdownFormatter::toHtml(item.text, imageBaseFor(_agent)), item.text,
                  QString::fromUtf8("assistantMsg"), false, false, key);
    else if (kind == QString::fromUtf8("tool")
             || kind == QString::fromUtf8("tool_error"))
    {
        const bool report = kind == QString::fromUtf8("tool")
                            && (item.text.startsWith(QString::fromUtf8("# "))
                                || item.text.startsWith(QString::fromUtf8("## ")));
        if (report)
            addBubble(title, MarkdownFormatter::toHtml(item.text, imageBaseFor(_agent)),
                      item.text, QString::fromUtf8("toolMsg"), false, false, key);
        else
            addPlainBubble(title, item.text, QString::fromUtf8("toolMsg"), false,
                           false, false, key);
    }
    else if (kind == QString::fromUtf8("error"))
        addPlainBubble(title, item.text, QString::fromUtf8("errorMsg"), false,
                       false, false, key);
    else if (kind == QString::fromUtf8("forwarded"))
    {
        if (!AppSettings::instance()->showForwardedContext())
            return;
        addPlainBubble(title, item.text, QString::fromUtf8("noticeMsg"), false,
                       false, false, key);
    }
    else
        addPlainBubble(title, item.text, QString::fromUtf8("noticeMsg"), false,
                       false, false, key);
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
    const QString chatBg = s->chatBackgroundColor().name();
    const QString bubble = s->isLightTheme()
                           ? QString::fromUtf8("#f3f4f6")
                           : s->panelColor().lighter(114).name();
    const QString input = s->inputColor().name();
    const QString accent = s->accentColor().name();
    const QString user = s->chatUserColor().name();
    const QString assistant = s->chatAssistantColor().name();
    const QString other = s->chatOtherColor().name();
    const QString border = s->borderColor().name();
    const QString muted = s->mutedTextColor().name();
    const QString hover = s->isLightTheme()
                          ? QColor(s->windowColor()).darker(104).name()
                          : bubble;
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
        "QProgressBar { background:%3; color:%2; border:none; border-radius:6px;"
        "  text-align:center; min-height:16px; max-height:18px; }"
        "QProgressBar::chunk { background:%4; border-radius:6px; }"
        "QScrollArea { background:%1; border:none; }"
        "QWidget#messagesHost { background:%1; }"
        "QFrame#userMsg QFrame#bubble { background:%6; border:1px solid %9;"
        "  border-radius:12px; }"
        "QFrame#assistantMsg QFrame#bubble { background:%7; border:1px solid %9;"
        "  border-radius:12px; }"
        "QFrame#toolMsg QFrame#bubble { background:%8; border:1px solid %9;"
        "  border-radius:10px; }"
        "QFrame#noticeMsg QFrame#bubble { background:%8; border:1px solid %9;"
        "  border-radius:10px; }"
        "QFrame#errorMsg QFrame#bubble { background:%8; border:1px solid %9;"
        "  border-radius:10px; }"
        "QWidget#permissionHost { background:%PERMBG%; border-top:1px solid %PERMBORDER%; }"
        "QWidget#permissionHost QLabel { color:%2; }"
        "QWidget#permissionHost QPushButton { background:%PERMBTN%; color:%2;"
        "  border:1px solid %PERMBORDER%; border-radius:8px; }"
        "QLabel { color:%2; background:transparent; }"
        "QTextBrowser { color:%2; background:transparent; border:none;"
        "  padding:0; margin:0; }"
        "QPlainTextEdit { background:%5; color:%2; border:1px solid %9;"
        "  border-radius:10px; padding:8px; }"
        "QPushButton { background:%PANEL%; color:%2; border:1px solid %9;"
        "  border-radius:8px; padding:6px 14px; }"
        "QPushButton:hover { background:%HOVER%; }"
        "QPushButton:disabled { color:%MUTED%; }")
                      .arg(chatBg, text, bubble, accent, input, user, assistant, other, border)
                      .replace(QString::fromUtf8("%MUTED%"), muted)
                      .replace(QString::fromUtf8("%HOVER%"), hover)
                      .replace(QString::fromUtf8("%PANEL%"), s->panelColor().name())
                      .replace(QString::fromUtf8("%PERMBG%"), permBg)
                      .replace(QString::fromUtf8("%PERMBORDER%"), permBorder)
                      .replace(QString::fromUtf8("%PERMBTN%"), permBtn));

    applyChatLiveActionStyle();
    if (_contextBar)
        refreshContextBar();

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

void ConversationWidget::syncRunningState()
{
    if (!_agent)
        return;
    sltBusyChanged(_agent->isBusy());
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
    if (!_liveActionLine || !_activityTimer)
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
        _liveActionLine->setText(QString::fromUtf8(" "));
        applyChatLiveActionStyle();
        return;
    }
    if (!_activityTimer->isActive())
        _activityTimer->start();
    sltActivityTick();
}

//----------------------------------------------

void ConversationWidget::sltActivityTick()
{
    if (!_liveActionLine || _shownActivity.isEmpty())
        return;
    const qint64 seconds = _activityClock.isValid() ? _activityClock.elapsed() / 1000 : 0;
    QString elapsed;
    if (seconds >= 60)
        elapsed = tr("%1 мин %2 с").arg(seconds / 60).arg(seconds % 60, 2, 10, QChar('0'));
    else
        elapsed = tr("%1 с").arg(seconds);
    _liveActionLine->setText(QString::fromUtf8("%1 \xC2\xB7 %2").arg(_shownActivity, elapsed));
    applyChatLiveActionStyle();
}

//----------------------------------------------

void ConversationWidget::ensureLiveActionAtEnd()
{
    if (!_liveActionLine || !_messagesLayout)
        return;
    _messagesLayout->removeWidget(_liveActionLine);
    _messagesLayout->addWidget(_liveActionLine, 0, Qt::AlignTop);
    _liveActionLine->setFixedHeight(kLiveActionHeight);
    _liveActionLine->show();
}

//----------------------------------------------

void ConversationWidget::applyChatLiveActionStyle()
{
    if (!_liveActionLine)
        return;
    AppSettings *s = AppSettings::instance();
    QColor c = s->mutedTextColor();
    if (_shownActivity.isEmpty())
        c.setAlpha(0);
    _liveActionLine->setStyleSheet(
        QString::fromUtf8(
            "QLabel#chatLiveAction { color:rgba(%1,%2,%3,%4); font-style:italic; "
            "background:transparent; border:none; min-height:%5px; max-height:%5px; }")
            .arg(c.red())
            .arg(c.green())
            .arg(c.blue())
            .arg(c.alpha())
            .arg(kLiveActionHeight));
}

//----------------------------------------------

void ConversationWidget::setChatLiveActionVisible(bool visible)
{
    if (!_liveActionLine)
        return;
    if (visible)
    {
        _liveActionLine->setFixedHeight(kLiveActionHeight);
        _liveActionLine->show();
    }
    else
        _liveActionLine->hide();
}

//----------------------------------------------

void ConversationWidget::sltContextUsageChanged(int usedTokens, int limitTokens)
{
    _contextUsedTokens = usedTokens;
    _contextLimitTokens = limitTokens;
    _contextCompacting = false;
    refreshContextBar();
}

//----------------------------------------------

static QFont contextBarFont()
{
    QFont font = QApplication::font();
    if (font.pointSize() > 0)
        font.setPointSize(qMax(6, (font.pointSize() * 2) / 3));
    else if (font.pixelSize() > 0)
        font.setPixelSize(qMax(8, (font.pixelSize() * 2) / 3));
    return font;
}

//----------------------------------------------

void ConversationWidget::refreshContextBar()
{
    if (!_contextBar)
        return;

    const QFont font = contextBarFont();
    _contextBar->setFont(font);
    QString fontCss;
    if (font.pointSize() > 0)
        fontCss = QString::number(font.pointSize()) + QString::fromUtf8("pt");
    else if (font.pixelSize() > 0)
        fontCss = QString::number(font.pixelSize()) + QString::fromUtf8("px");
    else
        fontCss = QString::fromUtf8("6pt");

    const int used = qMax(0, _contextUsedTokens);
    const int limit = qMax(0, _contextLimitTokens);
    const int usedK = (used + 500) / 1000;
    QString text;
    int percent = 0;
    if (limit <= 0)
    {
        _contextBar->setValue(0);
        text = used > 0
               ? QString::number(usedK) + QString::fromUtf8("k/—")
               : QString::fromUtf8("—");
    }
    else
    {
        const int limitK = (limit + 500) / 1000;
        const int permille = (int)qBound(qint64(0),
                                        qint64(used) * 1000 / qint64(limit),
                                        qint64(1000));
        percent = (permille + 5) / 10;
        _contextBar->setValue(permille);
        text = QString::number(usedK) + QString::fromUtf8("k/")
               + QString::number(limitK) + QString::fromUtf8("k");
    }
    _contextBar->setFormat(text);

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
    const QString ink = AppSettings::instance()->textColor().name();
    _contextBar->setStyleSheet(
        QString::fromUtf8("QProgressBar { background:%2; color:%3; border:none;"
                          " border-radius:4px; text-align:center; font-size:%4;"
                          " min-height:16px; max-height:18px; }"
                          "QProgressBar::chunk { background:%1; border-radius:4px; }")
        .arg(chunk, track, ink, fontCss));
}

//----------------------------------------------

void ConversationWidget::sltCompactingChanged(bool compacting)
{
    _contextCompacting = compacting;
    if (compacting)
        setActivity(tr("Сжатие контекста"));
    else
    {
        if (_agent && _agent->isBusy())
            setActivity(tr("Ожидание ответа модели"));
        else if (_contextLimitTokens > 0)
            sltContextUsageChanged(_contextUsedTokens, _contextLimitTokens);
        else if (_contextUsedTokens > 0)
            sltContextUsageChanged(_contextUsedTokens, _contextLimitTokens);
        else if (_contextLabel)
            refreshContextBar();
    }
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
    if (_promptMode == 3)
        hidePermissionRequest();
    _input->clear();
    emit sendRequested(text);
}

//----------------------------------------------

void ConversationWidget::sltUserMessage(const QString &text)
{
    const int ingress = _agent ? _agent->lastUserIngress() : AgentUserIngressHuman;
    if (ingress == AgentUserIngressSystem)
        return;

    QString kind = QString::fromUtf8("user");
    QString body = text;
    if (ingress == AgentUserIngressAssignment)
        kind = QString::fromUtf8("assignment");
    else if (ingress == AgentUserIngressFollowUp)
    {
        kind = QString::fromUtf8("followup");
        if (body.startsWith(QString::fromUtf8("Follow-up from your parent worker.")))
        {
            const int sep = body.indexOf(QString::fromUtf8("\n\n"));
            if (sep >= 0)
                body = body.mid(sep + 2).trimmed();
        }
    }
    else if (ingress == AgentUserIngressGroup)
    {
        kind = QString::fromUtf8("group");
        if (body.startsWith(QString::fromUtf8("[Your turn as narrator]")))
            body = tr("Ваш ход в обсуждении");
        else if (body.startsWith(QString::fromUtf8("[Group VOTE required]")))
            body = tr("Нужно проголосовать");
        else if (body.startsWith(QString::fromUtf8("[Group dialogue DONE]")))
            body = tr("Группа пришла к решению");
        else if (body.startsWith(QString::fromUtf8("[Waiting]"))
                 || body.startsWith(QString::fromUtf8("[Waiting for other votes]")))
            body = tr("Ожидание хода");
    }

    const QString title = titleForKind(kind);
    const QString colorKey = kind;
    recordDisplay(kind, title, body, colorKey);
    addPlainBubble(title, body, QString::fromUtf8("userMsg"), true, false, false, colorKey);
}

//----------------------------------------------

void ConversationWidget::sltAssistantMessage(const QString &text)
{
    const QString kind = QString::fromUtf8("assistant");
    const QString title = titleForKind(kind);
    recordDisplay(kind, title, text, title);
    const QString html = MarkdownFormatter::toHtml(text, imageBaseFor(_agent));
    addBubble(title, html, text, QString::fromUtf8("assistantMsg"), false, false, title);
}

//----------------------------------------------

void ConversationWidget::sltToolCallStarted(const QString &name, const QString &arguments)
{
    _pendingToolName = name;
    _pendingToolArgs = arguments;
    setActivity(tr("Выполняется функция %1").arg(name));
}

//----------------------------------------------

static QString planHowLabel(const QString &how)
{
    if (how == QString::fromUtf8("delegate"))
        return QObject::tr("поручить");
    if (how == QString::fromUtf8("tool"))
        return QObject::tr("инструмент");
    return QObject::tr("сам");
}

//----------------------------------------------

static QString formatPlanWorkBody(const QString &arguments)
{
    const QJsonDocument doc = QJsonDocument::fromJson(arguments.toUtf8());
    if (!doc.isObject())
        return QString();
    const QJsonObject object = doc.object();
    const QString title = object.value(QString::fromUtf8("title")).toString().trimmed();
    const QString architecture = object.value(QString::fromUtf8("architecture")).toString().trimmed();
    const QJsonArray subtasks = object.value(QString::fromUtf8("subtasks")).toArray();
    if (title.isEmpty() && architecture.isEmpty() && subtasks.isEmpty())
        return QString();

    QStringList lines;
    lines << QString::fromUtf8("# ")
                 + (title.isEmpty() ? QObject::tr("План действий") : title);
    if (!architecture.isEmpty())
    {
        lines << QString();
        lines << architecture;
    }
    if (!subtasks.isEmpty())
    {
        lines << QString();
        lines << QString::fromUtf8("## ") + QObject::tr("Шаги");
    }
    for (int i = 0; i < subtasks.size(); ++i)
    {
        const QJsonObject item = subtasks.at(i).toObject();
        const QString name = item.value(QString::fromUtf8("name")).toString().trimmed();
        const QString task = item.value(QString::fromUtf8("task")).toString().trimmed();
        const QString how = item.value(QString::fromUtf8("how")).toString().trimmed();
        const QString reason = item.value(QString::fromUtf8("reason")).toString().trimmed();
        QString line = QString::fromUtf8("- ");
        if (!name.isEmpty())
            line += QString::fromUtf8("**") + name + QString::fromUtf8("**");
        else
            line += task;
        if (!how.isEmpty())
            line += QString::fromUtf8(" — ") + planHowLabel(how);
        if (!name.isEmpty() && !task.isEmpty())
            line += QString::fromUtf8(". ") + task;
        if (!reason.isEmpty())
            line += QString::fromUtf8(" ") + QObject::tr("Зачем: %1").arg(reason);
        lines << line;
    }
    return lines.join(QString::fromUtf8("\n"));
}

//----------------------------------------------

void ConversationWidget::sltToolCallFinished(const QString &name, const QString &result, bool ok)
{
    QString body;
    if (ok && name == QString::fromUtf8("plan_work"))
        body = formatPlanWorkBody(_pendingToolArgs);
    if (body.isEmpty())
    {
        body = QString::fromUtf8("%1(%2)").arg(name, _pendingToolArgs);
        body += QChar('\n');
        body += result;
    }
    const QString kind = ok ? QString::fromUtf8("tool")
                            : QString::fromUtf8("tool_error");
    const QString title = titleForKind(kind);
    recordDisplay(kind, title, body, kind);
    const bool report = ok && name == QString::fromUtf8("plan_work")
                        && body.startsWith(QString::fromUtf8("# "));
    if (report)
        addBubble(title, MarkdownFormatter::toHtml(body, imageBaseFor(_agent)),
                  body, QString::fromUtf8("toolMsg"), false, false, kind);
    else
        addPlainBubble(title,
                       body,
                       QString::fromUtf8("toolMsg"),
                       false, false, false, kind);
    _pendingToolName.clear();
    _pendingToolArgs.clear();
    // A media executor calls invokeTool without setBusy; otherwise the
    // running-function timer keeps ticking after completion.
    if (_agent && _agent->isBusy())
        setActivity(tr("Ожидание ответа модели"));
    else
        setActivity(QString());
}

//----------------------------------------------

void ConversationWidget::sltNotice(const QString &text)
{
    const QString kind = QString::fromUtf8("notice");
    if (!_displayLog.isEmpty())
    {
        const ChatDisplayItem &last = _displayLog.last();
        if (last.kind == kind && last.text == text)
            return;
    }
    const QString title = titleForKind(kind);
    recordDisplay(kind, title, text, kind);
    addPlainBubble(title, text, QString::fromUtf8("noticeMsg"), false,
                   false, false, kind);
}

//----------------------------------------------

void ConversationWidget::sltAutoApproveToggled(bool checked)
{
    AppSettings::instance()->setAutoApprovePermissions(checked);
}

//----------------------------------------------

void ConversationWidget::sltDisplayOptionsChanged()
{
    if (!_autoApproveBox)
        return;
    const bool on = AppSettings::instance()->autoApprovePermissions();
    if (_autoApproveBox->isChecked() == on)
        return;
    _autoApproveBox->blockSignals(true);
    _autoApproveBox->setChecked(on);
    _autoApproveBox->blockSignals(false);
}

//----------------------------------------------

void ConversationWidget::sltForwardedContext(const QString &text)
{
    const QString kind = QString::fromUtf8("forwarded");
    const QString title = titleForKind(kind);
    recordDisplay(kind, title, text, kind);
    if (!AppSettings::instance()->showForwardedContext())
        return;
    addPlainBubble(title, text, QString::fromUtf8("noticeMsg"), false,
                   false, false, kind);
}

//----------------------------------------------

void ConversationWidget::sltFailed(const QString &error)
{
    const QString kind = QString::fromUtf8("error");
    const QString title = titleForKind(kind);
    recordDisplay(kind, title, error, kind);
    addPlainBubble(title, error, QString::fromUtf8("errorMsg"), false,
                   false, false, kind);
}

//----------------------------------------------

void ConversationWidget::sltScrollToBottom()
{
    _scrollScheduled = false;
    if (!_scroll || !_pinBottom)
        return;
    QScrollBar *bar = _scroll->verticalScrollBar();
    bar->setValue(bar->maximum());
}

//----------------------------------------------

bool ConversationWidget::isNearBottom() const
{
    if (!_scroll)
        return true;
    QScrollBar *bar = _scroll->verticalScrollBar();
    if (!bar)
        return true;
    // A small threshold: a slight lag from the edge still counts as being at the end.
    return bar->value() >= bar->maximum() - 48;
}

//----------------------------------------------

void ConversationWidget::scheduleScrollToBottom()
{
    if (!_pinBottom || _scrollScheduled)
        return;
    _scrollScheduled = true;
    QTimer::singleShot(0, this, SLOT(sltScrollToBottom()));
}

//----------------------------------------------

void ConversationWidget::trimOldBubbles()
{
    if (!_messagesLayout)
        return;
    // The layout has a stretch and service rows; remove only
    // the oldest ChatMessageFrame bubble.
    while (_messagesLayout->count() > kMaxVisibleBubbles + 2)
    {
        int index = -1;
        for (int i = 0; i < _messagesLayout->count(); ++i)
        {
            QLayoutItem *probe = _messagesLayout->itemAt(i);
            if (probe && qobject_cast<ChatMessageFrame *>(probe->widget()))
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

void ConversationWidget::sltScrollValueChanged(int)
{
    // While a deferred scroll-to-bottom is pending, value is not yet at the edge.
    // Do not clear _pinBottom, or the feed will unstick.
    if (_scrollScheduled)
        return;
    _pinBottom = isNearBottom();
}

//----------------------------------------------

QWidget *ConversationWidget::addBubble(QString title, QString html, QString preview,
                                       QString objectName, bool alignRight, bool collapsed,
                                       QString authorKey)
{
    // Capture the stuck-to-bottom flag BEFORE the layout change: after an insert
    // maximum is already greater than value and the feed would always look not-at-end.
    if (!_restoring)
        _pinBottom = isNearBottom();

    ChatMessageFrame *frame = new ChatMessageFrame(title, html, preview, objectName,
                                                   alignRight, collapsed, _messagesHost,
                                                   authorKey);
    connect(frame, SIGNAL(linkActivated(QUrl)), this, SIGNAL(messageLinkActivated(QUrl)));
    _messagesLayout->addWidget(frame, 0, Qt::AlignTop);
    ensureLiveActionAtEnd();
    trimOldBubbles();
    if (!_restoring && _pinBottom)
        scheduleScrollToBottom();
    return frame;
}

//----------------------------------------------

QWidget *ConversationWidget::addPlainBubble(QString title, QString text, QString objectName,
                                            bool alignRight, bool forceCollapsed,
                                            bool forceExpanded, QString authorKey)
{
    bool collapsed = (objectName != QString::fromUtf8("userMsg")
                      && objectName != QString::fromUtf8("assistantMsg"));
    if (forceCollapsed)
        collapsed = true;
    if (forceExpanded)
        collapsed = false;
    QString html = QString::fromUtf8("<p style=\"white-space:pre-wrap;\">");
    html += MarkdownFormatter::escapeHtml(text);
    html += QString::fromUtf8("</p>");
    return addBubble(title, html, text, objectName, alignRight, collapsed, authorKey);
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
        _contextLabel->hide();
    if (_contextBar)
        _contextBar->setVisible(visible);
    if (_compactButton)
        _compactButton->setVisible(visible);
    if (_interruptButton)
        _interruptButton->setVisible(visible);
}

//----------------------------------------------

void ConversationWidget::setContextMeterVisible(bool visible)
{
    if (_contextBar)
        _contextBar->setVisible(visible);
    if (_compactButton)
        _compactButton->setVisible(visible);
}
