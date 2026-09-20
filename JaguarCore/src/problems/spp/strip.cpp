#include "JaguarCore/problems/spp.h"

#include <cmath>

namespace jaguar
{
JAGUARCORE_API Result<Strip> Strip::create(
    double fixedHeight,
    CDEConfig cdeConfig,
    ShapeModifyConfig shapeModifyConfig,
    double width)
{
    if (!std::isfinite(fixedHeight) || fixedHeight <= 0.0 ||
        !std::isfinite(width) || width <= 0.0)
    {
        return Result<Strip>::fromError(
            {ErrorCode::InvalidInput, "strip height and width must be positive"});
    }
    return Result<Strip>::fromValue(
        Strip{fixedHeight, width, cdeConfig, shapeModifyConfig});
}

JAGUARCORE_API Result<void> Strip::setWidth(double newWidth)
{
    if (!std::isfinite(newWidth) || newWidth <= 0.0)
    {
        return Result<void>::fromError(
            {ErrorCode::InvalidInput, "strip width must be positive"});
    }
    width = newWidth;
    return Result<void>::fromValue();
}

JAGUARCORE_API Result<Container> Strip::createContainer(ContainerId id) const
{
    const auto rectangle = SimplePolygon::create({
        {0.0, 0.0}, {width, 0.0}, {width, fixedHeight}, {0.0, fixedHeight}});
    if (!rectangle.hasValue())
        return Result<Container>::fromError(rectangle.error());

    OriginalShape outer{
        rectangle.value(), Transform2d::identity(),
        ShapeModifyMode::Deflate, shapeModifyConfig};
    return Container::create(id, std::move(outer), {}, cdeConfig);
}
}
