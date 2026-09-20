#pragma once

#include "JaguarCore/collision/collision_engine.h"
#include "JaguarCore/entities/container.h"
#include "JaguarCore/entities/placed_item.h"
#include "JaguarCore/export.h"

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

    JAGUARCORE_API Result<PlacedItemHandle> placeItem(const Item& item,
                                                       Transform2d transform);
    JAGUARCORE_API Result<PlacedItem> removeItem(PlacedItemHandle handle);
    JAGUARCORE_API Result<void> swapContainer(Container container);
    JAGUARCORE_API bool isFeasible() const;
    JAGUARCORE_API double density() const;
    JAGUARCORE_API LayoutSnapshot save() const;
    JAGUARCORE_API Result<void> restore(const LayoutSnapshot& snapshot);

    JAGUARCORE_API const Container& container() const noexcept;
    JAGUARCORE_API std::size_t placedItemCount() const noexcept;
    JAGUARCORE_API std::optional<double> rightmostCollisionX() const noexcept;

private:
    struct Slot
    {
        std::optional<PlacedItem> placedItem;
        HazardHandle cdeHandle{};
        std::uint32_t generation = 1;
    };

    Slot* get(PlacedItemHandle handle) noexcept;
    const Slot* get(PlacedItemHandle handle) const noexcept;
    PlacedItemHandle allocateHandle();
    static Hazard makeHazard(const PlacedItem& placedItem);

    Container m_Container;
    std::unique_ptr<CollisionEngine> m_Cde;
    std::vector<Slot> m_Slots;
};
}
