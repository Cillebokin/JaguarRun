#include "JaguarCore/Entities/Container.h"
#include "JaguarCore/Entities/Item.h"
#include "JaguarCore/Entities/Layout.h"

#include <cassert>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace
{
jaguar::SimplePolygon rectangle(double xMin,
                                double yMin,
                                double xMax,
                                double yMax)
{
    const auto shape = jaguar::SimplePolygon::Create({
        {xMin, yMin}, {xMax, yMin}, {xMax, yMax}, {xMin, yMax}});
    assert(shape.HasValue());
    return shape.Value();
}

jaguar::Container makeContainer(double width, double height)
{
    jaguar::OriginalShape outer{
        rectangle(0.0, 0.0, width, height),
        jaguar::Transform2d::Identity(),
        jaguar::ShapeModifyMode::Deflate,
        {}};
    jaguar::CDEConfig config{};
    config.quadtreeDepth = 4;
    config.cdThreshold = 8;
    const auto container = jaguar::Container::Create(
        1, std::move(outer), {}, config);
    assert(container.HasValue());
    return container.Value();
}

jaguar::Item makeItem(jaguar::ItemIndex index, jaguar::ItemId externalId)
{
    jaguar::OriginalShape original{
        rectangle(0.0, 0.0, 1.0, 1.0),
        jaguar::Transform2d::Identity(),
        jaguar::ShapeModifyMode::Inflate,
        {}};
    const auto item = jaguar::Item::Create(
        index, externalId, std::move(original), jaguar::SurrogateConfig{});
    assert(item.HasValue());
    return item.Value();
}

std::string loadReference(const char* fileName)
{
    for (const std::string& path : {
             std::string("..\\JaguarCore\\Tests\\Fixtures\\RustReference\\") + fileName,
             std::string("JaguarCore\\Tests\\Fixtures\\RustReference\\") + fileName,
             std::string("..\\JaguarRun\\JaguarCore\\Tests\\Fixtures\\RustReference\\")
                 + fileName})
    {
        std::ifstream input(path, std::ios::binary);
        if (input)
            return {std::istreambuf_iterator<char>(input),
                    std::istreambuf_iterator<char>()};
    }
    return {};
}

std::map<std::string, std::string> parseFixture(const std::string& content)
{
    std::map<std::string, std::string> result;
    std::istringstream input(content);
    std::string line;
    while (std::getline(input, line))
    {
        if (line.empty() || line.front() == '#')
            continue;
        const std::size_t separator = line.find('=');
        assert(separator != std::string::npos);
        result.emplace(line.substr(0, separator), line.substr(separator + 1));
    }
    return result;
}

bool expected(const std::map<std::string, std::string>& fixture,
              const char* key)
{
    const auto found = fixture.find(key);
    assert(found != fixture.end());
    return found->second == "true";
}

std::string expectedText(const std::map<std::string, std::string>& fixture,
                         const char* key)
{
    const auto found = fixture.find(key);
    assert(found != fixture.end());
    return found->second;
}

bool hasHandle(const jaguar::LayoutSnapshot& snapshot,
               jaguar::PlacedItemHandle handle)
{
    for (const jaguar::LayoutPlacementSnapshot& placement : snapshot.placements)
    {
        if (placement.handle == handle)
            return true;
    }
    return false;
}

bool sameSnapshotState(const jaguar::LayoutSnapshot& first,
                       const jaguar::LayoutSnapshot& second)
{
    if (!first.container.SameBase(second.container) ||
        first.slotGenerations != second.slotGenerations ||
        first.freeSlotIndices != second.freeSlotIndices ||
        first.placements.size() != second.placements.size() ||
        first.cdeSnapshot.dynamicHazards.size() !=
            second.cdeSnapshot.dynamicHazards.size())
    {
        return false;
    }

    for (std::size_t i = 0; i < first.placements.size(); ++i)
    {
        const auto& before = first.placements[i];
        const auto& after = second.placements[i];
        const auto& beforeTransform = before.placedItem.Transform();
        const auto& afterTransform = after.placedItem.Transform();
        if (!(before.handle == after.handle) ||
            &before.placedItem.GetItem() != &after.placedItem.GetItem() ||
            before.placedItem.GetItemIndex() != after.placedItem.GetItemIndex() ||
            before.placedItem.GetItem().ExternalId() !=
                after.placedItem.GetItem().ExternalId() ||
            beforeTransform.rotationRad != afterTransform.rotationRad ||
            beforeTransform.translation.x != afterTransform.translation.x ||
            beforeTransform.translation.y != afterTransform.translation.y)
        {
            return false;
        }

        const auto& beforeVertices = before.placedItem.CollisionShape().Vertices();
        const auto& afterVertices = after.placedItem.CollisionShape().Vertices();
        if (beforeVertices.size() != afterVertices.size())
            return false;
        for (std::size_t vertex = 0; vertex < beforeVertices.size(); ++vertex)
        {
            if (beforeVertices[vertex].x != afterVertices[vertex].x ||
                beforeVertices[vertex].y != afterVertices[vertex].y)
            {
                return false;
            }
        }
    }

    for (std::size_t i = 0; i < first.cdeSnapshot.dynamicHazards.size(); ++i)
    {
        const auto& before = first.cdeSnapshot.dynamicHazards[i];
        const auto& after = second.cdeSnapshot.dynamicHazards[i];
        if (!(before.entity == after.entity) || before.dynamic != after.dynamic ||
            before.shape.get() != after.shape.get())
        {
            return false;
        }
    }
    return true;
}

std::string itemsText(const jaguar::Layout& layout)
{
    std::ostringstream output;
    bool first = true;
    for (const std::reference_wrapper<const jaguar::Item>& item : layout.Items())
    {
        if (!first)
            output << ',';
        first = false;
        output << item.get().Index() << ':' << item.get().ExternalId();
    }
    return output.str();
}
}

void runLayoutLifecycleDifferentialTests()
{
    const auto fixture = parseFixture(loadReference("layout_lifecycle.txt"));
    assert(fixture.size() == 18);

    const jaguar::Container container = makeContainer(20.0, 10.0);
    const jaguar::Container clonedContainer = container;
    const jaguar::Container recreatedContainer = makeContainer(20.0, 10.0);
    assert(container.SameBase(clonedContainer) ==
           expected(fixture, "container.clone_same_base"));
    assert(container.SameBase(recreatedContainer) ==
           expected(fixture, "container.recreated_same_base"));

    const jaguar::Item item = makeItem(0, 1000);
    jaguar::Layout roundtripLayout(container);
    const auto savedHandle = roundtripLayout.PlaceItem(
        item, jaguar::Transform2d::FromRotationTranslation(0.0, {1.0, 1.0}));
    assert(savedHandle.HasValue());
    const jaguar::LayoutSnapshot snapshot = roundtripLayout.Save();
    assert(roundtripLayout.RemoveItem(savedHandle.Value()).HasValue());
    const jaguar::Item mutationItem = makeItem(1, 1001);
    const auto mutatedHandle = roundtripLayout.PlaceItem(
        mutationItem, jaguar::Transform2d::FromRotationTranslation(0.0, {5.0, 1.0}));
    assert(mutatedHandle.HasValue());
    const auto restored = roundtripLayout.Restore(snapshot);
    assert(restored.HasValue() == expected(fixture, "roundtrip.restore_succeeded"));
    const jaguar::LayoutSnapshot restoredSnapshot = roundtripLayout.Save();
    assert(restoredSnapshot.placements.size() ==
           static_cast<std::size_t>(std::stoul(expectedText(fixture, "roundtrip.placement_count"))));
    assert(restoredSnapshot.cdeSnapshot.dynamicHazards.size() ==
           static_cast<std::size_t>(std::stoul(expectedText(fixture, "roundtrip.dynamic_hazard_count"))));
    assert(hasHandle(restoredSnapshot, savedHandle.Value()) ==
           expected(fixture, "roundtrip.saved_key_present"));
    assert(!hasHandle(restoredSnapshot, mutatedHandle.Value()) ==
           expected(fixture, "roundtrip.mutated_key_stale"));
    assert(!restoredSnapshot.placements.empty());
    const auto& restoredPlacement = restoredSnapshot.placements.front();
    std::ostringstream restoredItemText;
    restoredItemText << restoredPlacement.placedItem.GetItemIndex() << ':'
                     << std::fixed << std::setprecision(1)
                     << restoredPlacement.placedItem.Transform().translation.x << ':'
                     << restoredPlacement.placedItem.Transform().translation.y;
    assert(restoredItemText.str() == expectedText(fixture, "roundtrip.restored_item"));

    jaguar::Layout sharedBaseTarget(clonedContainer);
    const auto sharedBaseRestore = sharedBaseTarget.Restore(snapshot);
    assert(sharedBaseRestore.HasValue() ==
           expected(fixture, "restore.shared_base_succeeded"));
    assert(sharedBaseTarget.Save().placements.size() ==
           static_cast<std::size_t>(std::stoul(expectedText(fixture, "restore.shared_base_placement_count"))));

    jaguar::Layout mismatchTarget(recreatedContainer);
    const jaguar::Item firstMismatchItem = makeItem(7, 1007);
    const jaguar::Item secondMismatchItem = makeItem(8, 1008);
    const auto firstMismatchHandle = mismatchTarget.PlaceItem(
        firstMismatchItem,
        jaguar::Transform2d::FromRotationTranslation(0.0, {2.0, 2.0}));
    const auto secondMismatchHandle = mismatchTarget.PlaceItem(
        secondMismatchItem,
        jaguar::Transform2d::FromRotationTranslation(0.0, {8.0, 2.0}));
    assert(firstMismatchHandle.HasValue() && secondMismatchHandle.HasValue());
    const jaguar::LayoutSnapshot beforeMismatch = mismatchTarget.Save();
    const auto mismatchRestore = mismatchTarget.Restore(snapshot);
    const bool mismatchRejected =
        !mismatchRestore.HasValue() &&
        mismatchRestore.GetError().code == jaguar::ErrorCode::SnapshotMismatch;
    assert(mismatchRejected ==
           expected(fixture, "restore.different_base_rejected"));
    const jaguar::LayoutSnapshot afterMismatch = mismatchTarget.Save();
    const bool mismatchStateUnchanged =
        !container.SameBase(beforeMismatch.container) &&
        recreatedContainer.SameBase(beforeMismatch.container) &&
        hasHandle(afterMismatch, firstMismatchHandle.Value()) &&
        hasHandle(afterMismatch, secondMismatchHandle.Value()) &&
        sameSnapshotState(beforeMismatch, afterMismatch) &&
        afterMismatch.placements[0].placedItem.GetItemIndex() == 7 &&
        afterMismatch.placements[1].placedItem.GetItemIndex() == 8;
    assert(mismatchStateUnchanged ==
           expected(fixture, "restore.different_base_state_unchanged"));

    jaguar::Layout swapLayout(container);
    const jaguar::Item swapFirstItem = makeItem(12, 1012);
    const jaguar::Item swapSecondItem = makeItem(13, 1013);
    const auto swapFirstHandle = swapLayout.PlaceItem(
        swapFirstItem,
        jaguar::Transform2d::FromRotationTranslation(0.0, {1.0, 1.0}));
    const auto swapSecondHandle = swapLayout.PlaceItem(
        swapSecondItem,
        jaguar::Transform2d::FromRotationTranslation(0.0, {5.0, 1.0}));
    assert(swapFirstHandle.HasValue() && swapSecondHandle.HasValue());
    const jaguar::LayoutSnapshot oldContainerSnapshot = swapLayout.Save();
    assert(swapLayout.SwapContainer(makeContainer(21.0, 10.0)).HasValue());
    const jaguar::LayoutSnapshot afterSwap = swapLayout.Save();
    assert(afterSwap.placements.size() ==
           static_cast<std::size_t>(std::stoul(expectedText(fixture, "swap.placement_count"))));
    assert(afterSwap.cdeSnapshot.dynamicHazards.size() ==
           static_cast<std::size_t>(std::stoul(expectedText(fixture, "swap.dynamic_hazard_count"))));
    const auto oldSnapshotRestore = swapLayout.Restore(oldContainerSnapshot);
    const bool oldSnapshotRejected =
        !oldSnapshotRestore.HasValue() &&
        oldSnapshotRestore.GetError().code == jaguar::ErrorCode::SnapshotMismatch;
    assert(oldSnapshotRejected == expected(fixture, "swap.old_snapshot_rejected"));
    const jaguar::LayoutSnapshot afterOldSnapshot = swapLayout.Save();
    const bool oldSnapshotStateUnchanged =
        sameSnapshotState(afterSwap, afterOldSnapshot) &&
        hasHandle(afterOldSnapshot, swapFirstHandle.Value()) &&
        hasHandle(afterOldSnapshot, swapSecondHandle.Value());
    assert(oldSnapshotStateUnchanged ==
           expected(fixture, "swap.old_snapshot_state_unchanged"));

    const jaguar::Item firstItem = makeItem(5, 1005);
    const jaguar::Item secondItem = makeItem(6, 1006);
    const jaguar::Item duplicateItem = makeItem(5, 1505);
    jaguar::Layout itemsLayout(makeContainer(30.0, 10.0));
    const auto firstItemHandle = itemsLayout.PlaceItem(
        firstItem, jaguar::Transform2d::FromRotationTranslation(0.0, {1.0, 1.0}));
    assert(firstItemHandle.HasValue());
    assert(itemsLayout.PlaceItem(
               secondItem,
               jaguar::Transform2d::FromRotationTranslation(0.0, {8.0, 1.0}))
               .HasValue());
    assert(itemsLayout.PlaceItem(
               duplicateItem,
               jaguar::Transform2d::FromRotationTranslation(0.0, {15.0, 1.0}))
               .HasValue());
    assert(itemsText(itemsLayout) == expectedText(fixture, "items.before_removal"));
    assert(itemsLayout.RemoveItem(firstItemHandle.Value()).HasValue());
    assert(itemsText(itemsLayout) == expectedText(fixture, "items.after_removal"));
}
