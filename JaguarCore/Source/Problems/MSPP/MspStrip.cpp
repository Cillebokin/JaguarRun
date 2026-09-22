#include "JaguarCore/Problems/MSpp.h"

#include <cmath>

namespace jaguar
{
JAGUARCORE_API Result<MultiStrip> MultiStrip::Create(
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
        return Result<MultiStrip>::FromError(
            {ErrorCode::InvalidInput,
             "MSPP strip width, maximum width and height are invalid"});
    }
    return Result<MultiStrip>::FromValue(
        MultiStrip{maxWidth, fixedHeight, width, cdeConfig, shapeModifyConfig});
}

JAGUARCORE_API Result<void> MultiStrip::SetWidth(double newWidth)
{
    if (!std::isfinite(newWidth) || newWidth <= 0.0 || newWidth > maxWidth)
    {
        return Result<void>::FromError(
            {ErrorCode::InvalidInput, "MSPP strip width exceeds maximum width"});
    }
    width = newWidth;
    return Result<void>::FromValue();
}

JAGUARCORE_API Result<Container> MultiStrip::CreateContainer(ContainerId id) const
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
