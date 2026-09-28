#ifndef CONTEXTOPTIMIZER_H
#define CONTEXTOPTIMIZER_H

#include "llmtypes.h"
//----------------------------------------------
#include <QList>
#include <QString>
#include <QStringList>

//----------------------------------------------

/**
 * @brief Оценка токенов и подготовка истории к сжатию моделью.
 *
 * Сама выжимка делается языковой моделью, одинаково для всех воркеров:
 * системный промпт и закреплённая формулировка задачи остаются как есть,
 * свежий хвост (по бюджету токенов) сохраняется дословно, всё остальное
 * уходит модели порциями и возвращается одной сводкой. Текстовая выжимка
 * (extractiveSummary) — только запасной вариант, когда модель недоступна.
 */
class ContextOptimizer
{
public:
    ContextOptimizer();

    void calibrate(int actualPromptTokens, int estimatedPromptTokens);
    void reset();
    int  estimateText(QString text) const;
    int  estimateMessage(const LlmMessage &message) const;
    int  estimateMessages(const QList<LlmMessage> &messages) const;
    int  estimateTools(const QList<LlmTool> &tools) const;
    int  estimatePrompt(const QList<LlmMessage> &messages, const QList<LlmTool> &tools) const;

    /**
     * @brief Индекс первого сообщения хвоста, укладывающегося в @p tokenBudget.
     *
     * Сообщения считаются с конца; @p firstMovable — первый индекс, который
     * вообще можно сжимать (после системного промпта). Бюджет 0 — хвоста нет,
     * возвращается размер списка.
     */
    int tailKeepIndex(const QList<LlmMessage> &messages, int firstMovable, int tokenBudget) const;
    /**
     * @brief Сдвинуть границу назад, чтобы не разорвать пару tool_call/tool.
     */
    int expandKeepIndexForTools(const QList<LlmMessage> &messages, int keepFrom) const;
    void repairToolTurns(QList<LlmMessage> *messages) const;

    /**
     * @brief Разбить старую часть истории на текстовые порции для модели.
     *
     * Каждая порция не длиннее @p chunkTokenBudget токенов (по оценке);
     * очень длинные отдельные сообщения обрезаются, чтобы влезть.
     */
    QStringList splitForSummary(const QList<LlmMessage> &oldPart, int chunkTokenBudget) const;
    /**
     * @brief Запрос модели на сводку очередной порции.
     * @param previousSummary Сводка по предыдущим порциям (пусто для первой).
     * @param chunk           Текст порции.
     * @param index           Номер порции (с 1) и @p total — всего порций.
     * @param anchorKept      Формулировка задачи хранится отдельно и её не надо повторять.
     */
    QList<LlmMessage> buildSummarizeMessages(QString previousSummary, QString chunk,
                                             int index, int total, bool anchorKept) const;
    QString extractiveSummary(const QList<LlmMessage> &oldPart, int maxChars) const;
    LlmMessage makeSummaryMessage(QString summary) const;
    QList<LlmMessage> rebuildHistory(const LlmMessage &systemPrompt,
                                     const QList<LlmMessage> &anchors,
                                     QString summary,
                                     const QList<LlmMessage> &recent) const;

    static bool isContextOverflowError(QString error);

private:
    QString messageAsPlain(const LlmMessage &message, int maxChars) const;

    double _scale;
};

#endif // CONTEXTOPTIMIZER_H
