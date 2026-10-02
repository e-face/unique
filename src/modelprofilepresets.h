#ifndef MODELPROFILEPRESETS_H
#define MODELPROFILEPRESETS_H

#include "workertypes.h"
//----------------------------------------------
#include <QList>
#include <QString>

//----------------------------------------------

/**
 * @brief Шаблон профиля модели (без токена): провайдер + endpoint + модель.
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
    QString        hint;
};

//----------------------------------------------

/**
 * @brief Каталог известных OpenAI-совместимых провайдеров (LLM / image / speech / embedding).
 */
QList<ModelProfilePreset> knownModelProfilePresets();

/**
 * @brief Подпись для списка: «Провайдер · имя · modality · model».
 */
QString modelProfilePresetLabel(const ModelProfilePreset &preset);

#endif // MODELPROFILEPRESETS_H
