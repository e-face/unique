#ifndef MODELCAPABILITIES_H
#define MODELCAPABILITIES_H

#include "appsettings.h"
//----------------------------------------------
#include <QHash>
#include <QString>
#include <QStringList>

//----------------------------------------------

/**
 * @brief Что модель умеет принимать/отдавать и какие параметры API допустимы.
 *
 * Источник: provider /models (в т.ч. DeepSeek input_modalities),
 * OpenRouter /images/models|/models; иначе эвристика по modality+имени.
 */
struct ModelCapabilities
{
    ModelCapabilities()
        : known(false)
        , inputReferencesMin(0)
        , inputReferencesMax(0)
        , nMin(-1)
        , nMax(-1)
        , supportsSize(false)
        , supportsN(false)
    {
    }

    bool        known;
    QStringList inputModalities;  // text, image, audio, video, file
    QStringList outputModalities;
    int         inputReferencesMin;
    int         inputReferencesMax;
    int         nMin;
    int         nMax;
    bool        supportsSize;
    bool        supportsN;
    QStringList outputFormats;
    QString     source; // "openrouter_images" | "openrouter_models" | "heuristic"
    // Имена и допустимые значения из каталога провайдера. Пусто, если API их не отдал.
    QString     providerParams;
    // Путь запроса генерации, если каталог его сообщил. Пусто — стандартный.
    QString     requestEndpoint;
    // Категории референсных файлов, которые каталог (или эвристика) разрешил: image, audio, video.
    QStringList referenceModalities;
    QHash<QString, int> referenceMin;
    QHash<QString, int> referenceMax;
    // Имя поля запроса из каталога для этой категории. Пусто — общее поле инструмента.
    QHash<QString, QString> referenceField;

    bool acceptsText() const;
    bool acceptsImage() const;
    bool acceptsAudio() const;
    bool acceptsVideo() const;
    bool acceptsFile() const;
    bool requiresImageReference() const;
    bool allowsImageReference() const;
    QString summary() const;
};

//----------------------------------------------

/**
 * @brief Какие поля JSON исполнитель должен уметь заполнить для этой модальности.
 *
 * Пустая строка — это не медиа-модель (обычный LLM).
 */
QString mediaParamsLine(WorkerTaskKind kind, const ModelCapabilities &caps);

/**
 * @brief Какие референсные файлы можно передать этой медиа-модели: image, audio, video.
 *
 * Пустой список — референсы не принимаются. Для обычной LLM всегда пусто.
 */
QStringList referenceCategoriesFor(WorkerTaskKind kind, const ModelCapabilities &caps);

//----------------------------------------------

/**
 * @brief Кэш и загрузка capabilities по профилю модели.
 */
class ModelCapabilityService
{
public:
    static ModelCapabilityService *instance();

    /**
     * @brief Вернуть известные capabilities (сеть при miss / forceRefresh).
     */
    ModelCapabilities capabilitiesFor(const LlmProfile &profile,
                                      bool forceRefresh = false);

    /**
     * @brief Кэш или эвристика — без сети (для частого UI).
     */
    ModelCapabilities cachedOrHeuristic(const LlmProfile &profile) const;

    /**
     * @brief Только эвристика без сети (быстрый UI до fetch).
     */
    static ModelCapabilities heuristicFor(const LlmProfile &profile);

private:
    ModelCapabilityService();

    ModelCapabilities fetchOpenRouterImages(const LlmProfile &profile) const;
    ModelCapabilities fetchOpenRouterModels(const LlmProfile &profile) const;
    ModelCapabilities fetchOpenRouterPublicByModel(QString modelId) const;

    QHash<QString, ModelCapabilities> _cache;
};

#endif // MODELCAPABILITIES_H
