#include "JaguarCore/problems/mspp.h"

#include <cmath>

namespace jaguar
{
JAGUARCORE_API Result<MultiStrip> MultiStrip::create(
    double maxWidth,
    double fixedHeight,
    CDEConfig cdeConfig,
    ShapeModifyConfig shapeModifyConfig,
    double width)
{
    if (!std::isfinite(maxWidth) || maxWidth <= 0.0 ||
        !std::isfinite(fixedHeight) || fixedHeight <= 0.0 ||
        !std::isfinite(width) || width <= 0.0 || width > maxWidth)
    {
        return Result<MultiStrip>::fromError(
            {ErrorCode::InvalidInput,
             "MSPP strip width, maximum width and height are invalid"});
    }
    return Result<MultiStrip>::fromValue(
        MultiStrip{maxWidth, fixedHeight, width, cdeConfig, shapeModifyConfig});
}

JAGUARCORE_API Result<void> MultiStrip::setWidth(double newWidth)
{
    if (!std::isfinite(newWidth) || newWidth <= 0.0 || newWidth > maxWidth)
    {
        return Result<void>::fromError(
            {ErrorCode::InvalidInput, "MSPP strip width exceeds maximum width"});
    }
    width = newWidth;
    return Result<void>::fromValue();
}

JAGUARCORE_API Result<Container> MultiStrip::createContainer(ContainerId id) const
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
