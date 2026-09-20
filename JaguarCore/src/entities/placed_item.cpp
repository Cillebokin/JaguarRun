#include "JaguarCore/entities/placed_item.h"

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

JAGUARCORE_API Result<PlacedItem> PlacedItem::create(const Item& item,
                                                     Transform2d transform)
{
    auto itemCopy = std::make_shared<const Item>(item);
    return Result<PlacedItem>::fromValue(PlacedItem{
        std::move(itemCopy), transform, item.collisionShape().transformed(transform)});
}

JAGUARCORE_API const Item& PlacedItem::item() const noexcept
{
    return *m_Item;
}

JAGUARCORE_API ItemIndex PlacedItem::itemIndex() const noexcept
{
    return m_Item->index();
}

JAGUARCORE_API const Transform2d& PlacedItem::transform() const noexcept
{
    return m_Transform;
}

JAGUARCORE_API const SimplePolygon& PlacedItem::collisionShape() const noexcept
{
    return m_CollisionShape;
}

JAGUARCORE_API double PlacedItem::area() const noexcept
{
    return m_Item->area();
}
}
