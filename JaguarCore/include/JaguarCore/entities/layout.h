#pragma once

#include "JaguarCore/Collision/CollisionEngine.h"
#include "JaguarCore/Entities/Container.h"
#include "JaguarCore/Entities/PlacedItem.h"
#include "JaguarCore/Export.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace jaguar
{
struct LayoutPlacementSnapshot
{
    PlacedItemHandle handle{};
    PlacedItem placedItem;
};

struct LayoutSnapshot
{
    Container container;
    std::vector<LayoutPlacementSnapshot> placements;
    CollisionSnapshot cdeSnapshot;
};

class Layout
{
public:
    explicit JAGUARCORE_API Layout(Container container);
    Layout(const Layout&) = delete;
    Layout& operator=(const Layout&) = delete;
    Layout(Layout&&) noexcept = default;
    Layout& operator=(Layout&&) noexcept = default;

    JAGUARCORE_API Result<PlacedItemHandle> PlaceItem(const Item& item,
                                                       Transform2d transform);
    JAGUARCORE_API Result<PlacedItem> RemoveItem(PlacedItemHandle handle);
    JAGUARCORE_API Result<void> SwapContainer(Container container);
    JAGUARCORE_API bool IsFeasible() const;
    JAGUARCORE_API double Density() const;
    JAGUARCORE_API LayoutSnapshot Save() const;
    JAGUARCORE_API Result<void> Restore(const LayoutSnapshot& snapshot);

    JAGUARCORE_API const Container& GetContainer() const noexcept;
    JAGUARCORE_API std::size_t PlacedItemCount() const noexcept;
    JAGUARCORE_API std::optional<double> RightmostCollisionX() const noexcept;

private:
    struct Slot
    {
        std::optional<PlacedItem> placedItem;
        HazardHandle cdeHandle{};
        std::uint32_t generation = 1;
    };

    Slot* Get(PlacedItemHandle handle) noexcept;
    const Slot* Get(PlacedItemHandle handle) const noexcept;
    PlacedItemHandle AllocateHandle();
    static Hazard MakeHazard(const PlacedItem& placedItem);

    Container m_Container;
    CollisionEnginePtr m_Cde;
    std::vector<Slot> m_Slots;
};
}
