#include "JaguarCore/Entities/PlacedItem.h"

#include <utility>

namespace jaguar
{
PlacedItem::PlacedItem(std::shared_ptr<const Item> item,
                       Transform2d transform,
                       SimplePolygon collisionShape)
    : m_Item(std::move(item)),
      m_Transform(transform),
      m_CollisionShape(std::move(collisionShape))
{
}

JAGUARCORE_API Result<PlacedItem> PlacedItem::Create(const Item& item,
                                                     Transform2d transform)
{
    const Transform2d rustTransform{
        static_cast<double>(static_cast<float>(transform.rotationRad)),
        {static_cast<double>(static_cast<float>(transform.translation.x)),
         static_cast<double>(static_cast<float>(transform.translation.y))}};
    const auto collisionShape =
        item.CollisionShape().TransformedRustF32(rustTransform);
    if (!collisionShape.HasValue())
        return Result<PlacedItem>::FromError(collisionShape.GetError());

    auto itemCopy = std::make_shared<const Item>(item);
    return Result<PlacedItem>::FromValue(PlacedItem{
        std::move(itemCopy), rustTransform, std::move(collisionShape.Value())});
}

JAGUARCORE_API const Item& PlacedItem::GetItem() const noexcept
{
    return *m_Item;
}

JAGUARCORE_API ItemIndex PlacedItem::GetItemIndex() const noexcept
{
    return m_Item->Index();
}

JAGUARCORE_API const Transform2d& PlacedItem::Transform() const noexcept
{
    return m_Transform;
}

JAGUARCORE_API const SimplePolygon& PlacedItem::CollisionShape() const noexcept
{
    return m_CollisionShape;
}

JAGUARCORE_API double PlacedItem::Area() const noexcept
{
    return m_Item->Area();
}
}
