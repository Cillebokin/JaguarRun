#pragma once

#include "JaguarCore/entities/layout.h"
#include "JaguarCore/export.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace jaguar
{
struct Bin
{
    std::size_t index = 0;
    ContainerId externalId = 0;
    Container container;
    std::size_t stock = 0;
    std::uint64_t cost = 0;
};

class BPInstance
{
public:
    static JAGUARCORE_API Result<BPInstance> create(
        std::vector<Item> items,
        std::vector<std::size_t> demands,
        std::vector<Bin> bins);

    JAGUARCORE_API std::size_t itemCount() const noexcept;
    JAGUARCORE_API const Item& item(ItemIndex index) const noexcept;
    JAGUARCORE_API std::size_t itemDemand(ItemIndex index) const noexcept;
    JAGUARCORE_API std::size_t binCount() const noexcept;
    JAGUARCORE_API const Bin& bin(std::size_t index) const noexcept;
    JAGUARCORE_API std::size_t totalDemand() const noexcept;

private:
    BPInstance(std::vector<std::shared_ptr<const Item>> items,
               std::vector<std::size_t> demands,
               std::vector<Bin> bins);

    std::vector<std::shared_ptr<const Item>> m_Items;
    std::vector<std::size_t> m_Demands;
    std::vector<Bin> m_Bins;
};

enum class LayoutTargetKind
{
    Open,
    Closed,
};

struct BPPlacement
{
    LayoutTargetKind targetKind = LayoutTargetKind::Closed;
    std::size_t layoutIndex = 0;
    std::size_t binIndex = 0;
    ItemIndex itemIndex = 0;
    Transform2d transform = Transform2d::identity();
};

struct BPPlacedHandle
{
    std::size_t layoutIndex = 0;
    PlacedItemHandle placedItemHandle{};
};

struct BPLayoutSnapshot
{
    std::size_t layoutIndex = 0;
    std::size_t binIndex = 0;
    LayoutSnapshot layoutSnapshot;
};

struct BPSolution
{
    std::vector<BPLayoutSnapshot> layouts;

    JAGUARCORE_API double density() const;
    JAGUARCORE_API std::uint64_t cost(const BPInstance& instance) const;
};

class BPProblem
{
public:
    static JAGUARCORE_API Result<BPProblem> create(BPInstance instance);

    BPProblem(const BPProblem&) = delete;
    BPProblem& operator=(const BPProblem&) = delete;
    BPProblem(BPProblem&&) noexcept = default;
    BPProblem& operator=(BPProblem&&) noexcept = default;

    JAGUARCORE_API Result<BPPlacedHandle> placeItem(
        const BPPlacement& placement);
    JAGUARCORE_API Result<BPPlacement> removeItem(const BPPlacedHandle& handle);
    JAGUARCORE_API BPSolution save() const;
    JAGUARCORE_API Result<void> restore(const BPSolution& solution);

    JAGUARCORE_API std::size_t remainingDemand(ItemIndex index) const noexcept;
    JAGUARCORE_API std::size_t remainingBinStock(std::size_t index) const noexcept;
    JAGUARCORE_API std::size_t placedItemCount() const noexcept;
    JAGUARCORE_API std::size_t openLayoutCount() const noexcept;
    JAGUARCORE_API double density() const;
    JAGUARCORE_API std::uint64_t binCost() const noexcept;
    JAGUARCORE_API const BPInstance& instance() const noexcept;

private:
    struct LayoutSlot
    {
        std::optional<Layout> layout;
        std::size_t binIndex = 0;
    };

    BPProblem(BPInstance instance,
              std::vector<std::size_t> remainingDemand,
              std::vector<std::size_t> remainingBinStock);
    Result<std::size_t> openLayout(std::size_t binIndex);
    LayoutSlot* getLayout(std::size_t layoutIndex) noexcept;
    const LayoutSlot* getLayout(std::size_t layoutIndex) const noexcept;

    BPInstance m_Instance;
    std::vector<LayoutSlot> m_Layouts;
    std::vector<std::size_t> m_RemainingDemand;
    std::vector<std::size_t> m_RemainingBinStock;
};
}
