#include "SparrowCore/Optimizer/SparrowConfig.h"

namespace sparrow
{
Result<void> SparrowConfig::Validate() const
{
    const auto constructorResult = constructor.Validate();
    if (!constructorResult.HasValue())
    {
        return constructorResult;
    }
    const auto explorationResult = exploration.Validate();
    if (!explorationResult.HasValue())
    {
        return explorationResult;
    }
    const auto compressionResult = compression.Validate();
    if (!compressionResult.HasValue())
    {
        return compressionResult;
    }
    return terminator.Validate();
}
}
