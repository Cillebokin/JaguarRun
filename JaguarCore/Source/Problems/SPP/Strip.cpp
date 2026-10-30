#include "JaguarCore/Problems/Spp.h"

namespace jaguar
{
JAGUARCORE_API Result<Strip> Strip::Create(
    double fixedHeight,
    CDEConfig cdeConfig,
    ShapeModifyConfig shapeModifyConfig,
    double width)
{
    if (!(fixedHeight > 0.0) || !(width > 0.0))
    {
        return Result<Strip>::FromError(
            {ErrorCode::InvalidInput, "strip height and width must be positive"});
    }
    const float rustFixedHeight = static_cast<float>(fixedHeight);
    const float rustWidth = static_cast<float>(width);
    if (!(rustFixedHeight > 0.0F) || !(rustWidth > 0.0F))
    {
        return Result<Strip>::FromError(
            {ErrorCode::InvalidInput, "strip dimensions exceed f32 range"});
    }
    return Result<Strip>::FromValue(
        Strip{static_cast<double>(rustFixedHeight),
              static_cast<double>(rustWidth), cdeConfig, shapeModifyConfig});
}

JAGUARCORE_API Result<void> Strip::SetWidth(double newWidth)
{
    const float rustWidth = static_cast<float>(newWidth);
    if (!(rustWidth > 0.0F))
    {
        return Result<void>::FromError(
            {ErrorCode::InvalidInput, "strip width must be positive"});
    }
    width = static_cast<double>(rustWidth);
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
