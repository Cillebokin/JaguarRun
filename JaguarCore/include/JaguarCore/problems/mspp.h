#pragma once

#include "JaguarCore/entities/layout.h"
#include "JaguarCore/export.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

namespace jaguar
{
struct MultiStrip
{
    double maxWidth = 0.0;
    double fixedHeight = 0.0;
    double width = 0.0;
    CDEConfig cdeConfig{};
    ShapeModifyConfig shapeModifyConfig{};

    static JAGUARCORE_API Result<MultiStrip> create(
        double maxWidth,
        double fixedHeight,
        CDEConfig cdeConfig,
        ShapeModifyConfig shapeModifyConfig,
        double width);
    JAGUARCORE_API Result<void> setWidth(double newWidth);
    JAGUARCORE_API Result<Container> createContainer(ContainerId id = 0) const;
};

class MSPInstance
{
public:
    static JAGUARCORE_API Result<MSPInstance> create(
        std::vector<Item> items,
        std::vector<std::size_t> demands,
        double maxWidth,
        double fixedHeight,
        CDEConfig cdeConfig,
        ShapeModifyConfig shapeModifyConfig);

    JAGUARCORE_API std::size_t itemCount() const noexcept;
    JAGUARCORE_API const Item& item(ItemIndex index) const noexcept;
    JAGUARCORE_API std::size_t itemDemand(ItemIndex index) const noexcept;
    JAGUARCORE_API const MultiStrip& baseStrip() const noexcept;

private:
    MSPInstance(std::vector<std::shared_ptr<const Item>> items,
                std::vector<std::size_t> demands,
                MultiStrip baseStrip);

    std::vector<std::shared_ptr<const Item>> m_Items;
    std::vector<std::size_t> m_Demands;
    MultiStrip m_BaseStrip;
};

struct MSPPlacement
{
    std::size_t layoutIndex = 0;
    ItemIndex itemIndex = 0;
    Transform2d transform = Transform2d::identity();
};

struct MSPPlacedHandle
{
    std::size_t layoutIndex = 0;
    PlacedItemHandle placedItemHandle{};
};

struct MSPLayoutSnapshot
{
    std::size_t layoutIndex = 0;
    MultiStrip strip{};
    LayoutSnapshot layoutSnapshot;
};

struct MSPSolution
{
    std::vector<MSPLayoutSnapshot> layouts;

    JAGUARCORE_API double density() const;
    JAGUARCORE_API double totalStripWidth() const noexcept;
};

class MSPProblem
{
public:
    static JAGUARCORE_API Result<MSPProblem> create(MSPInstance instance);

    MSPProblem(const MSPProblem&) = delete;
    MSPProblem& operator=(const MSPProblem&) = delete;
    MSPProblem(MSPProblem&&) noexcept = default;
    MSPProblem& operator=(MSPProblem&&) noexcept = default;

    JAGUARCORE_API Result<std::size_t> addLayout(MultiStrip strip);
    JAGUARCORE_API Result<void> removeLayout(std::size_t layoutIndex);
    JAGUARCORE_API Result<MSPPlacedHandle> placeItem(
        const MSPPlacement& placement);
    JAGUARCORE_API Result<MSPPlacement> removeItem(
        const MSPPlacedHandle& handle);
    JAGUARCORE_API Result<void> changeStripWidth(std::size_t layoutIndex,
                                                 double width);
    JAGUARCORE_API Result<void> fitStrip(std::size_t layoutIndex);
    JAGUARCORE_API MSPSolution save() const;
    JAGUARCORE_API Result<void> restore(const MSPSolution& solution);

    JAGUARCORE_API std::size_t remainingDemand(ItemIndex index) const noexcept;
    JAGUARCORE_API std::size_t placedItemCount() const noexcept;
    JAGUARCORE_API std::size_t layoutCount() const noexcept;
    JAGUARCORE_API double stripWidth(std::size_t layoutIndex) const noexcept;
    JAGUARCORE_API double totalStripWidth() const noexcept;
    JAGUARCORE_API double density() const;
    JAGUARCORE_API const MSPInstance& instance() const noexcept;

private:
    struct LayoutSlot
    {
        std::optional<Layout> layout;
        MultiStrip strip{};
    };

    MSPProblem(MSPInstance instance,
               std::vector<std::size_t> remainingDemand);
    LayoutSlot* getLayout(std::size_t layoutIndex) noexcept;
    const LayoutSlot* getLayout(std::size_t layoutIndex) const noexcept;

    MSPInstance m_Instance;
    std::vector<LayoutSlot> m_Layouts;
    std::vector<std::size_t> m_RemainingDemand;
};
}
