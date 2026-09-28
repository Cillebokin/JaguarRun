#include "JaguarCore/Problems/MSpp.h"

namespace jaguar
{
JAGUARCORE_API Result<MultiStrip> MultiStrip::Create(
    double maxWidth,
    double fixedHeight,
    CDEConfig cdeConfig,
    ShapeModifyConfig shapeModifyConfig,
    double width)
{
    if (!(maxWidth > 0.0) || !(fixedHeight > 0.0) || !(width > 0.0))
    {
        return Result<MultiStrip>::FromError(
            {ErrorCode::InvalidInput,
             "MSPP strip width, maximum width and height are invalid"});
    }
    const float rustMaxWidth = static_cast<float>(maxWidth);
    const float rustFixedHeight = static_cast<float>(fixedHeight);
    const float rustWidth = static_cast<float>(width);
    if (!(rustMaxWidth > 0.0F) || !(rustFixedHeight > 0.0F) ||
        !(rustWidth > 0.0F))
    {
        return Result<MultiStrip>::FromError(
            {ErrorCode::InvalidInput, "MSPP strip dimensions exceed f32 range"});
    }
    return Result<MultiStrip>::FromValue(
        MultiStrip{static_cast<double>(rustMaxWidth),
                   static_cast<double>(rustFixedHeight),
                   static_cast<double>(rustWidth),
                   cdeConfig, shapeModifyConfig});
}

JAGUARCORE_API Result<void> MultiStrip::SetWidth(double newWidth)
{
    const float rustWidth = static_cast<float>(newWidth);
    const float rustMaxWidth = static_cast<float>(maxWidth);
    if (!(rustWidth <= rustMaxWidth))
    {
        return Result<void>::FromError(
            {ErrorCode::InvalidInput, "MSPP strip width exceeds f32 range"});
    }
    if (!(rustWidth > 0.0F))
    {
        return Result<void>::FromError(
            {ErrorCode::InvalidInput, "MSPP strip width must be positive"});
    }
    width = static_cast<double>(rustWidth);
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
