#include "JaguarCore/geometry/original_shape.h"

namespace jaguar
{
JAGUARCORE_API Result<SimplePolygon> OriginalShape::convertToInternal() const
{
    auto internal = shape.transformed(preTransform);
    if (modifyConfig.offset.has_value() && *modifyConfig.offset != 0.0)
    {
        const auto offset = offsetShape(internal, modifyMode, *modifyConfig.offset);
        if (!offset.hasValue())
            return offset;
        internal = offset.value();
    }
    if (modifyConfig.simplifyTolerance.has_value())
    {
        const SimplePolygon preSimplified = internal;
        const auto simplified = simplifyShape(
            internal, modifyMode, *modifyConfig.simplifyTolerance);
        if (!simplified.hasValue())
            return simplified;
        internal = simplified.value();
        if (modifyConfig.narrowConcavityCutoff.has_value())
        {
            internal = closeNarrowConcavities(
                internal, modifyMode, *modifyConfig.narrowConcavityCutoff);
            const auto postConcavitySimplified = simplifyShape(
                internal, modifyMode, *modifyConfig.simplifyTolerance / 10.0);
            if (!postConcavitySimplified.hasValue())
                return postConcavitySimplified;
            internal = postConcavitySimplified.value();
        }

        if (!shapeModificationValid(preSimplified, internal, modifyMode))
        {
            return Result<SimplePolygon>::fromError(
                {ErrorCode::InvalidPolygon,
                 "shape modification does not preserve inflate/deflate relation"});
        }
    }
    return Result<SimplePolygon>::fromValue(std::move(internal));
}
}
