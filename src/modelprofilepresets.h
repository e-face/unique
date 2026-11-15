#ifndef MODELPROFILEPRESETS_H
#define MODELPROFILEPRESETS_H

#include "workertypes.h"
//----------------------------------------------
#include <QList>
#include <QString>

//----------------------------------------------

/**
 * @brief Model-profile template with no token: provider, endpoint, and model.
 */
struct ModelProfilePreset
{
    ModelProfilePreset()
        : modality(WorkerTaskKindLlm)
    {
    }

    QString        provider;
    QString        name;
    WorkerTaskKind modality;
    QString        baseUrl;
    QString        model;
    QString        balanceUrl;
    QString        balanceJsonPath;
    /** Short purpose of the model. Copied into LlmProfile.purpose. */
    QString        purpose;
};

//----------------------------------------------

/**
 * @brief Catalog of known OpenAI-compatible providers (LLM, image, speech, embedding).
 */
QList<ModelProfilePreset> knownModelProfilePresets();

/**
 * @brief Label for the list: provider, name, modality, and model.
 */
QString modelProfilePresetLabel(const ModelProfilePreset &preset);

#endif // MODELPROFILEPRESETS_H
