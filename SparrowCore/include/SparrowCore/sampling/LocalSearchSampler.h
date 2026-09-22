#pragma once

#include "SparrowCore/Export.h"
#include "SparrowCore/LbfConfig.h"
#include "SparrowCore/Random/SparrowRng.h"
#include "SparrowCore/ResultAlias.h"

#include "JaguarCore/Entities/Item.h"
#include "JaguarCore/Geometry/Rect.h"
#include "JaguarCore/Geometry/Transformation.h"

namespace sparrow
{
class SPARROWCORE_API LocalSearchSampler
{
public:
    static Result<LocalSearchSampler> Create(
        const jaguar::Item& item,
        const jaguar::Transform2d& reference,
        const LbfConfig& config,
        const jaguar::Rect2d& containerBounds);

    jaguar::Transform2d Sample(SparrowRng& rng);
    void ShiftMean(const jaguar::Transform2d& transform);
    void DecayStddev(float progress);

private:
    LocalSearchSampler(jaguar::RotationMode rotationMode,
                       float discreteRotation,
                       float meanRotation,
                       float meanX,
                       float meanY,
                       float translationStddevInit,
                       float translationStddevLimit,
                       float rotationStddevInit,
                       float rotationStddevLimit);

    jaguar::RotationMode m_RotationMode = jaguar::RotationMode::None;
    float m_DiscreteRotation = 0.0F;
    float m_MeanRotation = 0.0F;
    float m_MeanX = 0.0F;
    float m_MeanY = 0.0F;
    float m_TranslationStddev = 0.0F;
    float m_RotationStddev = 0.0F;
    float m_TranslationStddevInit = 0.0F;
    float m_TranslationStddevLimit = 0.0F;
    float m_RotationStddevInit = 0.0F;
    float m_RotationStddevLimit = 0.0F;
};
}
