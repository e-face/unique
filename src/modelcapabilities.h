#ifndef MODELCAPABILITIES_H
#define MODELCAPABILITIES_H

#include "appsettings.h"
//----------------------------------------------
#include <QHash>
#include <QString>
#include <QStringList>

//----------------------------------------------

/**
 * @brief What the model can accept and return, and which API parameters are allowed.
 *
 * Source: the provider /models catalog (including DeepSeek input_modalities),
 * OpenRouter /images/models or /models; otherwise a heuristic by modality and name.
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
    // Names and allowed values from the provider catalog. Empty if the API did not return them.
    QString     providerParams;
    // Generation request path if the catalog reported it. Empty means the standard path.
    QString     requestEndpoint;
    // Reference-file categories the catalog or the heuristic allowed: image, audio, video.
    QStringList referenceModalities;
    QHash<QString, int> referenceMin;
    QHash<QString, int> referenceMax;
    // Request field name from the catalog for this category. Empty means the tool's generic field.
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
 * @brief Which JSON fields an executor must be able to fill for this modality.
 *
 * An empty string means this is not a media model (an ordinary LLM).
 */
QString mediaParamsLine(WorkerTaskKind kind, const ModelCapabilities &caps);

/**
 * @brief Which reference files this media model accepts: image, audio, video.
 *
 * An empty list means references are not accepted. Always empty for an ordinary LLM.
 */
QStringList referenceCategoriesFor(WorkerTaskKind kind, const ModelCapabilities &caps);

//----------------------------------------------

/**
 * @brief Cache and loading of capabilities by model profile.
 */
class ModelCapabilityService
{
public:
    static ModelCapabilityService *instance();

    /**
     * @brief Return known capabilities (network on a miss, or when forceRefresh is set).
     */
    ModelCapabilities capabilitiesFor(const LlmProfile &profile,
                                      bool forceRefresh = false);

    /**
     * @brief Cache or heuristic, with no network, for frequent UI use.
     */
    ModelCapabilities cachedOrHeuristic(const LlmProfile &profile) const;

    /**
     * @brief Heuristic only, with no network (a fast UI before fetch).
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
