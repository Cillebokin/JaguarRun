#include "JaguarCore/Problems/Spp.h"

#include <cmath>

namespace jaguar
{
JAGUARCORE_API Result<Strip> Strip::Create(
    double fixedHeight,
    CDEConfig cdeConfig,
    ShapeModifyConfig shapeModifyConfig,
    double width)
{
    if (!std::isfinite(fixedHeight) || fixedHeight <= 0.0 ||
        !std::isfinite(width) || width <= 0.0)
    {
        return Result<Strip>::FromError(
            {ErrorCode::InvalidInput, "strip height and width must be positive"});
    }
    return Result<Strip>::FromValue(
        Strip{fixedHeight, width, cdeConfig, shapeModifyConfig});
}

JAGUARCORE_API Result<void> Strip::SetWidth(double newWidth)
{
    if (!std::isfinite(newWidth) || newWidth <= 0.0)
    {
        return Result<void>::FromError(
            {ErrorCode::InvalidInput, "strip width must be positive"});
    }
    width = newWidth;
    return Result<void>::FromValue();
}

JAGUARCORE_API Result<Container> Strip::CreateContainer(ContainerId id) const
{
    const auto rectangle = SimplePolygon::Create({
        {0.0, 0.0}, {width, 0.0}, {width, fixedHeight}, {0.0, fixedHeight}});
    if (!rectangle.HasValue())
        return Result<Container>::FromError(rectangle.GetError());

    OriginalShape outer{
        rectangle.Value(), Transform2d::Identity(),
        ShapeModifyMode::Deflate, shapeModifyConfig};
    return Container::Create(id, std::move(outer), {}, cdeConfig);
}
}
