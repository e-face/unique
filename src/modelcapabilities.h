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
 * Источник: OpenRouter /images/models или /models; иначе эвристика по modality+имени.
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
