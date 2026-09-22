#include "JaguarCore/Geometry/OriginalShape.h"

namespace jaguar
{
JAGUARCORE_API Result<SimplePolygon> OriginalShape::ConvertToInternal() const
{
    const auto transformed = shape.Transformed(preTransform);
    if (!transformed.HasValue())
        return Result<SimplePolygon>::FromError(transformed.GetError());

    SimplePolygon internal = transformed.Value();
    if (modifyConfig.offset.has_value() && *modifyConfig.offset != 0.0)
    {
        const auto offset = OffsetShape(internal, modifyMode, *modifyConfig.offset);
        if (!offset.HasValue())
            return offset;
        internal = offset.Value();
    }
    if (modifyConfig.simplifyTolerance.has_value())
    {
        const SimplePolygon preSimplified = internal;
        const auto simplified = SimplifyShape(
            internal, modifyMode, *modifyConfig.simplifyTolerance);
        if (!simplified.HasValue())
            return simplified;
        internal = simplified.Value();
        if (modifyConfig.narrowConcavityCutoff.has_value())
        {
            internal = CloseNarrowConcavities(
                internal, modifyMode, *modifyConfig.narrowConcavityCutoff);
            const auto postConcavitySimplified = SimplifyShape(
                internal, modifyMode, *modifyConfig.simplifyTolerance / 10.0);
            if (!postConcavitySimplified.HasValue())
                return postConcavitySimplified;
            internal = postConcavitySimplified.Value();
        }

        if (!ShapeModificationValid(preSimplified, internal, modifyMode))
        {
            return Result<SimplePolygon>::FromError(
                {ErrorCode::InvalidPolygon,
                 "shape modification does not preserve inflate/deflate relation"});
        }
    }
    return Result<SimplePolygon>::FromValue(std::move(internal));
}
}
