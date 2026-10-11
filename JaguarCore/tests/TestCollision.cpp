#include "JaguarCore/Collision/CollisionEngine.h"

#include <cassert>
#include <algorithm>
#include <limits>
#include <memory>
#include <random>
#include <stdexcept>
#include <thread>
#include <vector>

namespace
{
std::shared_ptr<const jaguar::SimplePolygon> Rectangle(double xMin,
                                                       double yMin,
                                                       double xMax,
                                                       double yMax)
{
    const auto polygon = jaguar::SimplePolygon::Create({
        {xMin, yMin}, {xMax, yMin}, {xMax, yMax}, {xMin, yMax}});
    assert(polygon.HasValue());
    return std::make_shared<const jaguar::SimplePolygon>(polygon.Value());
}
}

void runCollisionTests()
{
    const double rawRoundedBoundsMax = 1.00000006;
    jaguar::CollisionEngine roundedBoundsEngine(
        jaguar::Rect2d{0.0, 0.0, rawRoundedBoundsMax, 1.0},
        jaguar::CDEConfig{});
    assert(roundedBoundsEngine.Bounds().xMax ==
           static_cast<double>(static_cast<float>(rawRoundedBoundsMax)));

    const auto root = jaguar::Rect2d::Create(0.0, 0.0, 10.0, 10.0);
    assert(root.HasValue());

    jaguar::QuadTree quadtree(root.Value(), 4, 0);
    const auto containedShape = jaguar::SimplePolygon::Create({
        {4.0, 4.0}, {4.5, 4.0}, {4.5, 4.5}, {4.0, 4.5}});
    assert(containedShape.HasValue());
    const jaguar::HazardHandle containedHandle{42, 1};
    quadtree.RegisterHazard(containedHandle,
                            {jaguar::HazardKind::PlacedItem, 42, 0, 0},
                            std::make_shared<const jaguar::SimplePolygon>(
                                containedShape.Value()), true);
    assert(quadtree.PresenceAt(containedHandle, root.Value()) ==
           jaguar::HazardPresence::Partial);
    assert(quadtree.NodeCount() > 1);
    assert(quadtree.PresenceAt(containedHandle, root.Value().Quadrants()[1]) ==
           jaguar::HazardPresence::None);
    const auto focusedQuery = root.Value().Quadrants()[2];
    const auto virtualResult = quadtree.QueryVirtualRoot(focusedQuery);
    const auto fullResult = quadtree.Query(focusedQuery);
    assert(virtualResult == fullResult);

    const auto coveringShape = jaguar::SimplePolygon::Create({
        {-1.0, -1.0}, {11.0, -1.0}, {11.0, 11.0}, {-1.0, 11.0}});
    assert(coveringShape.HasValue());
    const jaguar::HazardHandle coveringHandle{43, 1};
    quadtree.RegisterHazard(coveringHandle,
                            {jaguar::HazardKind::PlacedItem, 43, 0, 0},
                            std::make_shared<const jaguar::SimplePolygon>(
                                coveringShape.Value()), false);
    // jagua-rs QTHazard::from_root：根节点一律 Partial；Entire 只出现在
    // 懒扩展后的子节点（覆盖形状在四个象限的质心都在形内 → Entire）。
    assert(quadtree.PresenceAt(coveringHandle, root.Value()) ==
           jaguar::HazardPresence::Partial);
    assert(quadtree.PresenceAt(
               coveringHandle, root.Value().QuadrantsRustF32()[0]) ==
           jaguar::HazardPresence::Entire);
    assert(quadtree.QueryVirtualRoot(root.Value()) ==
           quadtree.Query(root.Value()));

    jaguar::HazardCollector collector;
    collector.Insert(containedHandle,
                     {jaguar::HazardKind::PlacedItem, 42, 0, 0});
    collector.Insert(containedHandle,
                     {jaguar::HazardKind::PlacedItem, 99, 0, 0});
    assert(collector.Size() == 1);
    assert(collector.ContainsEntity(
        {jaguar::HazardKind::PlacedItem, 99, 0, 0}));
    assert(collector.RemoveEntity(
        {jaguar::HazardKind::PlacedItem, 99, 0, 0}));
    assert(collector.Empty());

    const jaguar::HazardEntity firstDuplicateEntity{
        jaguar::HazardKind::PlacedItem, 77, 0, 0, {50, 1},
        jaguar::Transform2d::Identity()};
    const jaguar::HazardEntity secondDuplicateEntity{
        jaguar::HazardKind::PlacedItem, 77, 0, 0, {51, 1},
        jaguar::Transform2d::Identity()};
    collector.Insert({50, 1}, firstDuplicateEntity);
    collector.Insert({51, 1}, secondDuplicateEntity);
    assert(collector.Size() == 2);

    jaguar::CollisionEngine duplicateEntityEngine(
        root.Value(), jaguar::CDEConfig{});
    assert(duplicateEntityEngine.RegisterHazard({
               firstDuplicateEntity,
               Rectangle(1.0, 1.0, 2.0, 2.0), true})
               .HasValue());
    assert(duplicateEntityEngine.RegisterHazard({
               secondDuplicateEntity,
               Rectangle(3.0, 1.0, 4.0, 2.0), true})
               .HasValue());
    const auto duplicateQuery = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {5.0, 0.0}, {5.0, 3.0}, {0.0, 3.0}});
    assert(duplicateQuery.HasValue());
    const auto duplicateEntityCollisions = duplicateEntityEngine.CollectPolygonCollisions(
        duplicateQuery.Value());
    assert(duplicateEntityCollisions.size() == 2);
    assert(!(duplicateEntityCollisions[0] == duplicateEntityCollisions[1]));

    const auto outerHazardShape = Rectangle(0.0, 0.0, 10.0, 10.0);
    jaguar::CollisionEngine dynamicHoleEngine(root.Value(), jaguar::CDEConfig{});
    assert(dynamicHoleEngine.RegisterHazard({
               {jaguar::HazardKind::Exterior}, outerHazardShape, false})
               .HasValue());
    const jaguar::Hazard dynamicHole{
        {jaguar::HazardKind::Hole, 0, 0, 88},
        Rectangle(1.0, 1.0, 2.0, 2.0), true};
    assert(dynamicHoleEngine.RegisterHazard(dynamicHole).HasValue());
    const auto dynamicHoleSnapshot = dynamicHoleEngine.Save();
    jaguar::CollisionEngine restoredDynamicHoleEngine(
        root.Value(), jaguar::CDEConfig{});
    assert(restoredDynamicHoleEngine.RegisterHazard({
               {jaguar::HazardKind::Exterior}, outerHazardShape, false})
               .HasValue());
    assert(restoredDynamicHoleEngine.Restore(dynamicHoleSnapshot).HasValue());
    const auto restoredDynamicHoleSnapshot =
        restoredDynamicHoleEngine.Save();
    assert(restoredDynamicHoleSnapshot.dynamicHazards.size() == 1);
    assert(restoredDynamicHoleSnapshot.dynamicHazards.front().entity.kind ==
           jaguar::HazardKind::Hole);

    jaguar::CollisionEngine handleReuseEngine(root.Value(), jaguar::CDEConfig{});
    const auto firstHandle = handleReuseEngine.RegisterHazard({
        {jaguar::HazardKind::PlacedItem, 501, 0, 0},
        Rectangle(1.0, 1.0, 2.0, 2.0), true});
    assert(firstHandle.HasValue());
    assert(handleReuseEngine.DeregisterHazard(firstHandle.Value()).HasValue());
    const auto replacementHandle = handleReuseEngine.RegisterHazard({
        {jaguar::HazardKind::PlacedItem, 502, 0, 0},
        Rectangle(5.0, 5.0, 6.0, 6.0), true});
    assert(replacementHandle.HasValue());
    assert(!(replacementHandle.Value() == firstHandle.Value()));
    const auto staleDeregister =
        handleReuseEngine.DeregisterHazard(firstHandle.Value());
    assert(!staleDeregister.HasValue());
    assert(staleDeregister.GetError().code == jaguar::ErrorCode::InvalidInput);
    const auto firstQuery = jaguar::SimplePolygon::Create({
        {1.2, 1.2}, {1.8, 1.2}, {1.8, 1.8}, {1.2, 1.8}});
    const auto replacementQuery = jaguar::SimplePolygon::Create({
        {5.2, 5.2}, {5.8, 5.2}, {5.8, 5.8}, {5.2, 5.8}});
    assert(firstQuery.HasValue() && replacementQuery.HasValue());
    assert(!handleReuseEngine.DetectPolygonCollision(
        firstQuery.Value(), jaguar::NoFilter{}));
    assert(handleReuseEngine.DetectPolygonCollision(
        replacementQuery.Value(), jaguar::NoFilter{}));

    jaguar::CollisionEngine slotMapHazardEngine(root.Value(),
                                               jaguar::CDEConfig{});
    const auto slotMapExterior = slotMapHazardEngine.RegisterHazard({
        {jaguar::HazardKind::Exterior}, Rectangle(0.0, 0.0, 10.0, 10.0), false});
    const auto firstSlotMapHazard = slotMapHazardEngine.RegisterHazard({
        {jaguar::HazardKind::PlacedItem, 601, 0, 0, {11, 1},
         jaguar::Transform2d::Identity()},
        Rectangle(1.0, 1.0, 2.0, 2.0), true});
    const auto secondSlotMapHazard = slotMapHazardEngine.RegisterHazard({
        {jaguar::HazardKind::PlacedItem, 602, 0, 0, {12, 1},
         jaguar::Transform2d::Identity()},
        Rectangle(3.0, 1.0, 4.0, 2.0), true});
    assert(slotMapExterior.HasValue() && firstSlotMapHazard.HasValue() &&
           secondSlotMapHazard.HasValue());
    assert(slotMapExterior.Value().index == 1 &&
           slotMapExterior.Value().generation == 1);
    assert(firstSlotMapHazard.Value().index == 2 &&
           firstSlotMapHazard.Value().generation == 1);
    assert(secondSlotMapHazard.Value().index == 3 &&
           secondSlotMapHazard.Value().generation == 1);
    assert(slotMapHazardEngine.DeregisterHazard(
               firstSlotMapHazard.Value()).HasValue());
    assert(slotMapHazardEngine.DeregisterHazard(
               secondSlotMapHazard.Value()).HasValue());
    const auto reusedSlotMapHazard = slotMapHazardEngine.RegisterHazard({
        {jaguar::HazardKind::PlacedItem, 603, 0, 0, {13, 1},
         jaguar::Transform2d::Identity()},
        Rectangle(5.0, 1.0, 6.0, 2.0), true});
    assert(reusedSlotMapHazard.HasValue());
    assert(reusedSlotMapHazard.Value().index ==
           secondSlotMapHazard.Value().index);
    assert(reusedSlotMapHazard.Value().generation ==
           secondSlotMapHazard.Value().generation + 2);

    jaguar::CollisionEngine restoreSlotMapHazardEngine(
        root.Value(), jaguar::CDEConfig{});
    assert(restoreSlotMapHazardEngine.RegisterHazard({
               {jaguar::HazardKind::Exterior},
               Rectangle(0.0, 0.0, 10.0, 10.0), false})
               .HasValue());
    const auto restoreHazardA = restoreSlotMapHazardEngine.RegisterHazard({
        {jaguar::HazardKind::PlacedItem, 611, 0, 0, {21, 1},
         jaguar::Transform2d::Identity()},
        Rectangle(1.0, 1.0, 2.0, 2.0), true});
    const auto restoreHazardB = restoreSlotMapHazardEngine.RegisterHazard({
        {jaguar::HazardKind::PlacedItem, 612, 0, 0, {22, 1},
         jaguar::Transform2d::Identity()},
        Rectangle(3.0, 1.0, 4.0, 2.0), true});
    assert(restoreHazardA.HasValue() && restoreHazardB.HasValue());
    const jaguar::CollisionSnapshot restoreHazardSnapshot =
        restoreSlotMapHazardEngine.Save();
    assert(restoreSlotMapHazardEngine.DeregisterHazard(
               restoreHazardA.Value()).HasValue());
    assert(restoreSlotMapHazardEngine.DeregisterHazard(
               restoreHazardB.Value()).HasValue());
    assert(restoreSlotMapHazardEngine.RegisterHazard({
               {jaguar::HazardKind::PlacedItem, 613, 0, 0, {23, 1},
                jaguar::Transform2d::Identity()},
               Rectangle(5.0, 1.0, 6.0, 2.0), true})
               .HasValue());
    assert(restoreSlotMapHazardEngine.Restore(restoreHazardSnapshot).HasValue());
    const auto restoredHazardA =
        restoreSlotMapHazardEngine.DeregisterHazard({3, 5});
    const auto restoredHazardB =
        restoreSlotMapHazardEngine.DeregisterHazard({2, 3});
    assert(restoredHazardA.HasValue() && restoredHazardA.Value().entity.itemIndex == 611);
    assert(restoredHazardB.HasValue() && restoredHazardB.Value().entity.itemIndex == 612);
    const auto restoredHazardReuse = restoreSlotMapHazardEngine.RegisterHazard({
        {jaguar::HazardKind::PlacedItem, 614, 0, 0, {24, 1},
         jaguar::Transform2d::Identity()},
        Rectangle(7.0, 1.0, 8.0, 2.0), true});
    assert(restoredHazardReuse.HasValue());
    assert(restoredHazardReuse.Value().index == 2 &&
           restoredHazardReuse.Value().generation == 5);

    jaguar::QuadTree handleReuseTree(root.Value(), 4, 1);
    handleReuseTree.RegisterHazard(
        replacementHandle.Value(),
        {jaguar::HazardKind::PlacedItem, 502, 0, 0},
        Rectangle(5.0, 5.0, 6.0, 6.0), true);
    assert(handleReuseTree.PresenceAt(firstHandle.Value(), root.Value()) ==
           jaguar::HazardPresence::None);
    assert(handleReuseTree.Query(root.Value()).size() == 1);

    jaguar::CollisionEngine callbackEngine(root.Value(), jaguar::CDEConfig{});
    const auto callbackHazardA = callbackEngine.RegisterHazard({
        {jaguar::HazardKind::PlacedItem, 100, 0, 0},
        Rectangle(2.0, 2.0, 4.0, 4.0), true});
    const auto callbackHazardB = callbackEngine.RegisterHazard({
        {jaguar::HazardKind::PlacedItem, 101, 0, 0},
        Rectangle(4.0, 2.0, 6.0, 4.0), true});
    assert(callbackHazardA.HasValue());
    assert(callbackHazardB.HasValue());
    const auto callbackShape = jaguar::SimplePolygon::Create({
        {1.0, 1.0}, {7.0, 1.0}, {7.0, 5.0}, {1.0, 5.0}});
    assert(callbackShape.HasValue());

    jaguar::HazardCollector callbackCollector;
    std::size_t callbackCount = 0;
    const bool stopped = callbackEngine.CollectPolygonCollisionsUntil(
        callbackShape.Value(), callbackCollector, jaguar::NoFilter{},
        [&callbackCount](const jaguar::HazardEntity&) {
            ++callbackCount;
            return true;
        });
    assert(stopped);
    assert(callbackCount == 1);
    assert(callbackCollector.Size() == 1);

    const bool completed = callbackEngine.CollectPolygonCollisionsUntil(
        callbackShape.Value(), callbackCollector, jaguar::NoFilter{},
        [](const jaguar::HazardEntity&) { return false; });
    assert(!completed);
    assert(callbackCollector.Size() == 2);

    jaguar::SurrogateConfig callbackSurrogateConfig{};
    callbackSurrogateConfig.poleLimits = {{{2, 0.0}, {1, 0.8}, {1, 0.9}}};
    callbackSurrogateConfig.failFastPoleAreaRatio = 1.0;
    const auto callbackSurrogateShape = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {2.0, 0.0}, {2.0, 2.0}, {0.0, 2.0}});
    assert(callbackSurrogateShape.HasValue());
    const auto callbackSurrogate = jaguar::Surrogate::Create(
        callbackSurrogateShape.Value(), callbackSurrogateConfig);
    assert(callbackSurrogate.HasValue());
    jaguar::CollisionEngine surrogateCallbackEngine(root.Value(), jaguar::CDEConfig{});
    const auto surrogateCallbackHazard = surrogateCallbackEngine.RegisterHazard({
        {jaguar::HazardKind::PlacedItem, 102, 0, 0},
        Rectangle(4.0, 4.0, 6.0, 6.0), true});
    assert(surrogateCallbackHazard.HasValue());
    jaguar::HazardCollector surrogateCollector;
    std::size_t surrogateCallbackCount = 0;
    const auto surrogateStopped =
        surrogateCallbackEngine.CollectSurrogateCollisionsUntil(
            callbackSurrogate.Value(),
            jaguar::Transform2d::FromRotationTranslation(0.0, {4.0, 4.0}),
            surrogateCollector, jaguar::NoFilter{},
            [&surrogateCallbackCount](const jaguar::HazardEntity&) {
                ++surrogateCallbackCount;
                return true;
            });
    assert(surrogateStopped.HasValue());
    assert(surrogateStopped.Value());
    assert(surrogateCallbackCount == 1);
    assert(surrogateCollector.Size() == 1);
    const auto surrogateCompleted = surrogateCallbackEngine.CollectSurrogateCollisionsUntil(
        callbackSurrogate.Value(),
        jaguar::Transform2d::FromRotationTranslation(0.0, {0.0, 0.0}),
        surrogateCollector, jaguar::NoFilter{},
        [](const jaguar::HazardEntity&) { return false; });
    assert(surrogateCompleted.HasValue());
    assert(!surrogateCompleted.Value());
    assert(surrogateCollector.Size() == 1);

    jaguar::CDEConfig config{};
    config.quadtreeDepth = 4;
    config.cdThreshold = 8;

    jaguar::CollisionEngine engine(root.Value(), config);

    const auto hazard = engine.RegisterHazard({
        {jaguar::HazardKind::PlacedItem, 7, 0, 0}, Rectangle(4.0, 4.0, 6.0, 6.0), true});
    assert(hazard.HasValue());

    const auto colliding = jaguar::SimplePolygon::Create({
        {5.0, 5.0}, {7.0, 5.0}, {7.0, 7.0}, {5.0, 7.0}});
    assert(colliding.HasValue());
    assert(engine.DetectPolygonCollision(colliding.Value(), jaguar::NoFilter{}));

    const auto batchClear = jaguar::SimplePolygon::Create({
        {0.5, 0.5}, {1.5, 0.5}, {1.5, 1.5}, {0.5, 1.5}});
    assert(batchClear.HasValue());
    const auto batchOutside = jaguar::SimplePolygon::Create({
        {-1.0, 1.0}, {1.0, 1.0}, {1.0, 2.0}, {-1.0, 2.0}});
    assert(batchOutside.HasValue());
    const std::vector<jaguar::SimplePolygon> batch{
        colliding.Value(), batchClear.Value(), batchOutside.Value()};
    const auto batchResults = engine.DetectPolygonCollisions(batch);
    assert(batchResults.size() == batch.size());
    assert(batchResults[0] == 1);
    assert(batchResults[1] == 0);
    assert(batchResults[2] == 1);
    for (std::size_t i = 0; i < batch.size(); ++i)
    {
        assert(batchResults[i] ==
               (engine.DetectPolygonCollision(batch[i], jaguar::NoFilter{}) ? 1 : 0));
    }

    jaguar::HazardFilter ignorePlaced{};
    ignorePlaced.irrelevant = [](const jaguar::HazardEntity& entity) {
        return entity.kind == jaguar::HazardKind::PlacedItem;
    };
    const auto filteredBatchResults = engine.DetectPolygonCollisions(batch, ignorePlaced);
    assert(filteredBatchResults.size() == batch.size());
    assert(filteredBatchResults[0] == 0);
    assert(filteredBatchResults[1] == 0);
    assert(filteredBatchResults[2] == 1);

    const auto emptyBatchResults =
        engine.DetectPolygonCollisions(std::vector<jaguar::SimplePolygon>{});
    assert(emptyBatchResults.empty());

    jaguar::BatchCollisionOptions parallelOptions{};
    parallelOptions.workerCount = 2;
    parallelOptions.grainSize = 1;
    const auto parallelResults = engine.DetectPolygonCollisionsParallel(
        batch, jaguar::NoFilter{}, parallelOptions);
    assert(parallelResults.HasValue());
    assert(parallelResults.Value() == batchResults);
    const auto singleWorkerResults = engine.DetectPolygonCollisionsParallel(
        batch, jaguar::NoFilter{}, {1, 2});
    assert(singleWorkerResults.HasValue());
    assert(singleWorkerResults.Value() == batchResults);
    const auto emptyParallelResults = engine.DetectPolygonCollisionsParallel(
        {}, jaguar::NoFilter{}, parallelOptions);
    assert(emptyParallelResults.HasValue());
    assert(emptyParallelResults.Value().empty());
    const auto invalidParallelResults = engine.DetectPolygonCollisionsParallel(
        batch, jaguar::NoFilter{}, {2, 0});
    assert(!invalidParallelResults.HasValue());
    assert(invalidParallelResults.GetError().code == jaguar::ErrorCode::InvalidInput);
    for (std::size_t repeat = 0; repeat < 20; ++repeat)
    {
        const auto repeatedResults = engine.DetectPolygonCollisionsParallel(
            batch, jaguar::NoFilter{}, parallelOptions);
        assert(repeatedResults.HasValue());
        assert(repeatedResults.Value() == batchResults);
    }

    std::vector<std::uint8_t> concurrentLeft;
    std::vector<std::uint8_t> concurrentRight;
    std::thread left([&] {
        const auto result = engine.DetectPolygonCollisionsParallel(
            batch, jaguar::NoFilter{}, parallelOptions);
        assert(result.HasValue());
        concurrentLeft = result.Value();
    });
    std::thread right([&] {
        const auto result = engine.DetectPolygonCollisionsParallel(
            batch, jaguar::NoFilter{}, {2, 1});
        assert(result.HasValue());
        concurrentRight = result.Value();
    });
    left.join();
    right.join();
    assert(concurrentLeft == batchResults);
    assert(concurrentRight == batchResults);

    std::vector<std::uint8_t> ordinaryConcurrentLeft;
    std::vector<std::uint8_t> ordinaryConcurrentRight;
    std::thread ordinaryLeft([&] {
        ordinaryConcurrentLeft = engine.DetectPolygonCollisions(batch);
    });
    std::thread ordinaryRight([&] {
        ordinaryConcurrentRight = engine.DetectPolygonCollisions(batch);
    });
    ordinaryLeft.join();
    ordinaryRight.join();
    assert(ordinaryConcurrentLeft == batchResults);
    assert(ordinaryConcurrentRight == batchResults);

    const auto innerContainer = Rectangle(2.0, 2.0, 8.0, 8.0);
    const auto exteriorHazard = engine.RegisterHazard({
        {jaguar::HazardKind::Exterior, 0, 0, 0}, innerContainer, false});
    assert(exteriorHazard.HasValue());

    const auto outsideInnerContainer = jaguar::SimplePolygon::Create({
        {0.5, 0.5}, {1.0, 0.5}, {1.0, 1.0}, {0.5, 1.0}});
    assert(outsideInnerContainer.HasValue());
    assert(engine.DetectPolygonCollision(
        outsideInnerContainer.Value(), jaguar::NoFilter{}));
    const auto exteriorCollisions = engine.CollectPolygonCollisions(
        outsideInnerContainer.Value());
    assert(std::any_of(exteriorCollisions.begin(), exteriorCollisions.end(),
                       [](const auto& entity) {
                           return entity.kind == jaguar::HazardKind::Exterior;
                       }));

    const auto insideInnerContainer = jaguar::SimplePolygon::Create({
        {3.0, 3.0}, {4.0, 3.0}, {4.0, 4.0}, {3.0, 4.0}});
    assert(insideInnerContainer.HasValue());
    const auto insideCollisions = engine.CollectPolygonCollisions(
        insideInnerContainer.Value());
    assert(std::none_of(insideCollisions.begin(), insideCollisions.end(),
                        [](const auto& entity) {
                            return entity.kind == jaguar::HazardKind::Exterior;
                        }));

    jaguar::HazardFilter exteriorFilter{};
    exteriorFilter.irrelevant = [](const jaguar::HazardEntity& entity) {
        return entity.kind == jaguar::HazardKind::Exterior;
    };
    assert(!engine.DetectPolygonCollision(
        outsideInnerContainer.Value(), exteriorFilter));

    const auto clearAfterExterior = jaguar::SimplePolygon::Create({
        {2.5, 2.5}, {3.5, 2.5}, {3.5, 3.5}, {2.5, 3.5}});
    assert(clearAfterExterior.HasValue());
    assert(!engine.DetectPolygonCollision(clearAfterExterior.Value(), jaguar::NoFilter{}));

    const auto collisions = engine.CollectPolygonCollisions(colliding.Value());
    assert(collisions.size() == 1);
    assert(collisions.front().kind == jaguar::HazardKind::PlacedItem);

    const auto outsideAfterExterior = jaguar::SimplePolygon::Create({
        {-1.0, 1.0}, {1.0, 1.0}, {1.0, 2.0}, {-1.0, 2.0}});
    assert(outsideAfterExterior.HasValue());
    assert(engine.DetectPolygonCollision(outsideAfterExterior.Value(), jaguar::NoFilter{}));
    const auto outsideCollisions = engine.CollectPolygonCollisions(outsideAfterExterior.Value());
    assert(outsideCollisions.size() == 1);
    assert(outsideCollisions.front().kind == jaguar::HazardKind::Exterior);

    const auto enclosing = jaguar::SimplePolygon::Create({
        {-1.0, -1.0}, {11.0, -1.0}, {11.0, 11.0}, {-1.0, 11.0}});
    assert(enclosing.HasValue());
    assert(engine.DetectPolygonCollision(enclosing.Value(), jaguar::NoFilter{}));
    const auto enclosingCollisions = engine.CollectPolygonCollisions(enclosing.Value());
    const bool enclosingHasExterior = std::any_of(
        enclosingCollisions.begin(), enclosingCollisions.end(), [](const auto& entity) {
            return entity.kind == jaguar::HazardKind::Exterior;
        });
    assert(enclosingHasExterior);

    jaguar::CDEConfig depthZeroConfig{};
    depthZeroConfig.quadtreeDepth = 0;
    depthZeroConfig.cdThreshold = 8;
    jaguar::CollisionEngine containmentEngine(root.Value(), depthZeroConfig);
    const auto registeredInnerExterior = containmentEngine.RegisterHazard({
        {jaguar::HazardKind::Exterior, 0, 0, 0},
        Rectangle(2.0, 2.0, 8.0, 8.0), false});
    assert(registeredInnerExterior.HasValue());
    const auto queryContainingExterior = jaguar::SimplePolygon::Create({
        {1.0, 1.0}, {9.0, 1.0}, {9.0, 9.0}, {1.0, 9.0}});
    assert(queryContainingExterior.HasValue());
    assert(!containmentEngine.DetectPolygonCollision(
        queryContainingExterior.Value(), jaguar::NoFilter{}));

    const auto surrogateShape = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {2.0, 0.0}, {2.0, 2.0}, {0.0, 2.0}});
    assert(surrogateShape.HasValue());
    jaguar::SurrogateConfig surrogateConfig{};
    surrogateConfig.poleLimits = {{{2, 0.0}, {1, 0.8}, {1, 0.9}}};
    surrogateConfig.failFastPoleAreaRatio = 1.0;
    const auto surrogate = jaguar::Surrogate::Create(surrogateShape.Value(), surrogateConfig);
    assert(surrogate.HasValue());

    jaguar::CollisionEngine boundaryOverlapEngine(root.Value(),
                                                   jaguar::CDEConfig{});
    const auto boundaryOverlapHazard = boundaryOverlapEngine.RegisterHazard({
        {jaguar::HazardKind::PlacedItem, 77, 0, 0},
        Rectangle(0.0, 0.4, 0.25, 0.6), true});
    assert(boundaryOverlapHazard.HasValue());
    jaguar::HazardFilter ignoreExterior{};
    ignoreExterior.irrelevant = [](const jaguar::HazardEntity& entity) {
        return entity.kind == jaguar::HazardKind::Exterior;
    };
    assert(surrogate.Value().FailFastPoleCount() > 0);
    const jaguar::Circle2d& boundaryPole = surrogate.Value().Poles().front();

    jaguar::CollisionEngine depthZeroSurrogateEngine(
        root.Value(), depthZeroConfig);
    assert(depthZeroSurrogateEngine.RegisterHazard({
               {jaguar::HazardKind::Exterior}, Rectangle(4.0, 4.0, 6.0, 6.0), false})
               .HasValue());
    const auto rootCrossingTransform =
        jaguar::Transform2d::FromRotationTranslation(
            0.0,
            {boundaryPole.radius * 0.5 - boundaryPole.center.x,
             boundaryPole.radius * 0.5 - boundaryPole.center.y});
    const auto rootCrossingCollision =
        depthZeroSurrogateEngine.DetectSurrogateCollision(
            surrogate.Value(), rootCrossingTransform, jaguar::NoFilter{});
    assert(rootCrossingCollision.HasValue());
    assert(!rootCrossingCollision.Value());
    jaguar::HazardCollector rootCrossingCollector;
    const auto rootCrossingCollected =
        depthZeroSurrogateEngine.CollectSurrogateCollisionsUntil(
            surrogate.Value(), rootCrossingTransform, rootCrossingCollector,
            jaguar::NoFilter{}, [](const jaguar::HazardEntity&) { return false; });
    assert(rootCrossingCollected.HasValue());
    assert(!rootCrossingCollected.Value());
    assert(rootCrossingCollector.Empty());

    const auto notchContainer = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {10.0, 0.0}, {10.0, 10.0}, {6.0, 10.0},
        {6.0, 4.0}, {4.0, 4.0}, {4.0, 10.0}, {0.0, 10.0}});
    assert(notchContainer.HasValue());
    const auto smallSurrogateShape = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {0.1, 0.0}, {0.1, 0.1}, {0.0, 0.1}});
    assert(smallSurrogateShape.HasValue());
    jaguar::SurrogateConfig notchSurrogateConfig{};
    notchSurrogateConfig.poleLimits = {{{1, 0.0}, {0, 0.0}, {0, 0.0}}};
    notchSurrogateConfig.failFastPoleAreaRatio = 1.0;
    const auto notchSurrogate = jaguar::Surrogate::Create(
        smallSurrogateShape.Value(), notchSurrogateConfig);
    assert(notchSurrogate.HasValue());
    assert(notchSurrogate.Value().FailFastPoleCount() > 0);
    jaguar::CollisionEngine notchSurrogateEngine(root.Value(), depthZeroConfig);
    assert(notchSurrogateEngine.RegisterHazard({
               {jaguar::HazardKind::Exterior},
               std::make_shared<const jaguar::SimplePolygon>(notchContainer.Value()),
               false})
               .HasValue());
    const jaguar::Circle2d& notchPole = notchSurrogate.Value().Poles().front();
    const auto notchTransform = jaguar::Transform2d::FromRotationTranslation(
        0.0, {5.0 - notchPole.center.x, 8.0 - notchPole.center.y});
    const auto notchCollision = notchSurrogateEngine.DetectSurrogateCollision(
        notchSurrogate.Value(), notchTransform, jaguar::NoFilter{});
    assert(notchCollision.HasValue());
    assert(!notchCollision.Value());

    jaguar::CDEConfig notchHighThresholdConfig{};
    notchHighThresholdConfig.quadtreeDepth = 4;
    notchHighThresholdConfig.cdThreshold = 8;
    jaguar::CollisionEngine notchHighThresholdEngine(
        root.Value(), notchHighThresholdConfig);
    assert(notchHighThresholdEngine.RegisterHazard({
               {jaguar::HazardKind::Exterior},
               std::make_shared<const jaguar::SimplePolygon>(notchContainer.Value()),
               false})
               .HasValue());
    const auto notchHighThresholdDetect =
        notchHighThresholdEngine.DetectSurrogateCollision(
            notchSurrogate.Value(), notchTransform, jaguar::NoFilter{});
    assert(notchHighThresholdDetect.HasValue());
    assert(notchHighThresholdDetect.Value());
    jaguar::HazardCollector notchHighThresholdCollector;
    const auto notchHighThresholdCollect =
        notchHighThresholdEngine.CollectSurrogateCollisionsUntil(
            notchSurrogate.Value(), notchTransform, notchHighThresholdCollector,
            jaguar::NoFilter{}, [](const jaguar::HazardEntity&) { return false; });
    assert(notchHighThresholdCollect.HasValue());
    assert(!notchHighThresholdCollect.Value());
    assert(notchHighThresholdCollector.Empty());

    jaguar::CDEConfig notchLowThresholdConfig{};
    notchLowThresholdConfig.quadtreeDepth = 4;
    notchLowThresholdConfig.cdThreshold = 0;
    jaguar::CollisionEngine notchLowThresholdEngine(
        root.Value(), notchLowThresholdConfig);
    assert(notchLowThresholdEngine.RegisterHazard({
               {jaguar::HazardKind::Exterior},
               std::make_shared<const jaguar::SimplePolygon>(notchContainer.Value()),
               false})
               .HasValue());
    const auto notchLowThresholdDetect =
        notchLowThresholdEngine.DetectSurrogateCollision(
            notchSurrogate.Value(), notchTransform, jaguar::NoFilter{});
    assert(notchLowThresholdDetect.HasValue());
    assert(notchLowThresholdDetect.Value());
    jaguar::HazardCollector notchLowThresholdCollector;
    const auto notchLowThresholdCollect =
        notchLowThresholdEngine.CollectSurrogateCollisionsUntil(
            notchSurrogate.Value(), notchTransform, notchLowThresholdCollector,
            jaguar::NoFilter{}, [](const jaguar::HazardEntity&) { return false; });
    assert(notchLowThresholdCollect.HasValue());
    assert(!notchLowThresholdCollect.Value());
    assert(notchLowThresholdCollector.Size() == 1);
    assert(notchLowThresholdCollector.Entries().front().second.kind ==
           jaguar::HazardKind::Exterior);

    jaguar::SurrogateConfig notchPierOnlyConfig{};
    notchPierOnlyConfig.poleLimits = {{{1, 0.0}, {0, 0.0}, {0, 0.0}}};
    notchPierOnlyConfig.failFastPiers = 1;
    const auto notchPierOnlySurrogate =
        jaguar::Surrogate::Create(notchContainer.Value(), notchPierOnlyConfig);
    assert(notchPierOnlySurrogate.HasValue());
    assert(notchPierOnlySurrogate.Value().FailFastPoleCount() == 0);
    assert(notchPierOnlySurrogate.Value().Piers().size() == 1);
    const auto largeRoot = jaguar::Rect2d::Create(0.0, 0.0, 40.0, 40.0);
    assert(largeRoot.HasValue());
    const auto largeNotchContainer = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {40.0, 0.0}, {40.0, 40.0}, {30.0, 40.0},
        {30.0, 10.0}, {10.0, 10.0}, {10.0, 40.0}, {0.0, 40.0}});
    assert(largeNotchContainer.HasValue());
    jaguar::CollisionEngine largeNotchEngine(largeRoot.Value(), notchHighThresholdConfig);
    assert(largeNotchEngine.RegisterHazard({
               {jaguar::HazardKind::Exterior},
               std::make_shared<const jaguar::SimplePolygon>(
                   largeNotchContainer.Value()),
               false})
               .HasValue());
    const auto notchPierOnlyCollision =
        largeNotchEngine.DetectSurrogateCollision(
            notchPierOnlySurrogate.Value(),
            jaguar::Transform2d::FromRotationTranslation(0.0, {15.0, 15.0}),
            jaguar::NoFilter{});
    assert(notchPierOnlyCollision.HasValue());
    assert(notchPierOnlyCollision.Value());

    const auto boundaryPoleTransform =
        jaguar::Transform2d::FromRotationTranslation(
            0.0,
            {-boundaryPole.center.x - boundaryPole.radius * 0.5,
             0.5 - boundaryPole.center.y});
    const auto boundaryOverlap = boundaryOverlapEngine.DetectSurrogateCollision(
        surrogate.Value(), boundaryPoleTransform, ignoreExterior);
    assert(boundaryOverlap.HasValue());
    assert(boundaryOverlap.Value());

    const auto invalidSurrogateCollision = engine.DetectSurrogateCollision(
        surrogate.Value(),
        jaguar::Transform2d::FromRotationTranslation(
            std::numeric_limits<double>::quiet_NaN(), {0.0, 0.0}),
        jaguar::NoFilter{});
    assert(!invalidSurrogateCollision.HasValue());
    assert(invalidSurrogateCollision.GetError().code == jaguar::ErrorCode::InvalidInput);
    const auto collisionResult = engine.DetectSurrogateCollision(
        surrogate.Value(), jaguar::Transform2d::FromRotationTranslation(0.0, {4.0, 4.0}),
        jaguar::NoFilter{});
    assert(collisionResult.HasValue());
    assert(collisionResult.Value());
    const auto noCollisionResult = engine.DetectSurrogateCollision(
        surrogate.Value(),
        jaguar::Transform2d::FromRotationTranslation(0.0, {20.0, 20.0}),
        exteriorFilter);
    assert(noCollisionResult.HasValue());
    assert(!noCollisionResult.Value());
    jaguar::HazardFilter exteriorHandleFilter{};
    exteriorHandleFilter.irrelevantHandle =
        [exteriorHazardHandle = exteriorHazard.Value()](
            jaguar::HazardHandle handle, const jaguar::HazardEntity&) {
            return handle == exteriorHazardHandle;
        };
    const auto filteredNoCollisionResult = engine.DetectSurrogateCollision(
        surrogate.Value(),
        jaguar::Transform2d::FromRotationTranslation(0.0, {20.0, 20.0}),
        exteriorHandleFilter);
    assert(filteredNoCollisionResult.HasValue());
    assert(!filteredNoCollisionResult.Value());

    jaguar::CollisionEngine duplicateSnapshotEngine(
        root.Value(), jaguar::CDEConfig{});
    assert(duplicateSnapshotEngine.RegisterHazard({
               firstDuplicateEntity,
               Rectangle(1.0, 1.0, 2.0, 2.0), true})
               .HasValue());
    assert(duplicateSnapshotEngine.RegisterHazard({
               secondDuplicateEntity,
               Rectangle(3.0, 1.0, 4.0, 2.0), true})
               .HasValue());
    const auto duplicateSnapshot = duplicateSnapshotEngine.Save();
    assert(duplicateSnapshot.dynamicHazards.size() == 2);
    assert(duplicateSnapshotEngine.Restore(duplicateSnapshot).HasValue());
    assert(duplicateSnapshotEngine.CollectPolygonCollisions(
                duplicateQuery.Value()).size() == 2);

    jaguar::CollisionEngine stableHandleEngine(
        root.Value(), jaguar::CDEConfig{});
    const auto stableHandle = stableHandleEngine.RegisterHazard({
        {jaguar::HazardKind::PlacedItem, 78, 0, 0},
        Rectangle(1.0, 1.0, 2.0, 2.0), true});
    assert(stableHandle.HasValue());
    const auto unchangedHazardSnapshot = stableHandleEngine.Save();
    assert(stableHandleEngine.Restore(unchangedHazardSnapshot).HasValue());
    assert(stableHandleEngine.DeregisterHazard(stableHandle.Value()).HasValue());

    const auto snapshot = engine.Save();
    const auto snapshotBeforeFailedRestore = engine.Save();
    assert(engine.DeregisterHazard(hazard.Value()).HasValue());
    assert(!engine.DetectPolygonCollision(colliding.Value(), jaguar::NoFilter{}));
    assert(engine.Restore(snapshot).HasValue());
    assert(engine.DetectPolygonCollision(colliding.Value(), jaguar::NoFilter{}));

    jaguar::CollisionSnapshot nonDynamicSnapshot = snapshot;
    nonDynamicSnapshot.dynamicHazards.front().dynamic = false;
    const auto nonDynamicRestore = engine.Restore(nonDynamicSnapshot);
    assert(nonDynamicRestore.HasValue());
    assert(engine.Save().dynamicHazards.size() ==
           snapshot.dynamicHazards.size());
    assert(engine.DetectPolygonCollision(colliding.Value(), jaguar::NoFilter{}));

    jaguar::CollisionSnapshot invalidSnapshot = engine.Save();
    invalidSnapshot.dynamicHazards.clear();
    invalidSnapshot.dynamicHazards.push_back(
        {{jaguar::HazardKind::PlacedItem, 999, 0, 0}, nullptr, true});
    const auto invalidRestore = engine.Restore(invalidSnapshot);
    assert(!invalidRestore.HasValue());
    assert(invalidRestore.GetError().code == jaguar::ErrorCode::InvalidInput);
    assert(engine.DetectPolygonCollision(colliding.Value(), jaguar::NoFilter{}));

    const auto snapshotAfterFailedRestore = engine.Save();
    assert(snapshotAfterFailedRestore.dynamicHazards.size() ==
           snapshotBeforeFailedRestore.dynamicHazards.size());
    for (std::size_t i = 0;
         i < snapshotBeforeFailedRestore.dynamicHazards.size(); ++i)
    {
        const auto& beforeHazard = snapshotBeforeFailedRestore.dynamicHazards[i];
        const auto& afterHazard = snapshotAfterFailedRestore.dynamicHazards[i];
        assert(beforeHazard.dynamic == afterHazard.dynamic);
        assert(beforeHazard.entity == afterHazard.entity);
        assert(beforeHazard.shape && afterHazard.shape);
        assert(beforeHazard.shape->VertexCount() ==
               afterHazard.shape->VertexCount());
        for (std::size_t vertex = 0;
             vertex < beforeHazard.shape->VertexCount(); ++vertex)
        {
            assert(std::abs(beforeHazard.shape->Vertices()[vertex].x -
                            afterHazard.shape->Vertices()[vertex].x) < 1.0e-12);
            assert(std::abs(beforeHazard.shape->Vertices()[vertex].y -
                            afterHazard.shape->Vertices()[vertex].y) < 1.0e-12);
        }
    }

    jaguar::CollisionSnapshot staticHazardAsDynamic = engine.Save();
    assert(!staticHazardAsDynamic.dynamicHazards.empty());
    staticHazardAsDynamic.dynamicHazards.front().entity.kind =
        jaguar::HazardKind::Exterior;
#ifdef JAGUARCORE_TEST_RELEASE
    const auto staticHazardRestore = engine.Restore(staticHazardAsDynamic);
    assert(staticHazardRestore.HasValue());
    assert(engine.Save().dynamicHazards.front().entity.kind ==
           jaguar::HazardKind::Exterior);
#else
    bool staticHazardRestoreThrew = false;
    try
    {
        (void)engine.Restore(staticHazardAsDynamic);
    }
    catch (const std::logic_error&)
    {
        staticHazardRestoreThrew = true;
    }
    assert(staticHazardRestoreThrew);
    assert(engine.Save().dynamicHazards.empty());
    assert(!engine.DetectPolygonCollision(colliding.Value(), jaguar::NoFilter{}));
#endif
#ifdef JAGUARCORE_TEST_RELEASE
    assert(engine.DetectPolygonCollision(colliding.Value(), jaguar::NoFilter{}));
#endif

    // Fixed-seed larger differential: a deeply subdivided tree must agree
    // with a deliberately non-subdivided index before and after atomic
    // snapshot restore, including after deterministic removals.
    const auto randomRoot = jaguar::Rect2d::Create(0.0, 0.0, 100.0, 100.0);
    assert(randomRoot.HasValue());
    jaguar::CDEConfig deepConfig{};
    deepConfig.quadtreeDepth = 6;
    deepConfig.cdThreshold = 4;
    jaguar::CDEConfig flatConfig = deepConfig;
    flatConfig.cdThreshold = 255;
    jaguar::CollisionEngine deepEngine(randomRoot.Value(), deepConfig);
    jaguar::CollisionEngine flatEngine(randomRoot.Value(), flatConfig);

    std::mt19937_64 random(0x9e3779b97f4a7c15ULL);
    std::uniform_real_distribution<double> position(0.0, 94.0);
    std::uniform_real_distribution<double> size(0.5, 6.0);
    std::vector<jaguar::HazardHandle> deepHandles;
    std::vector<jaguar::HazardHandle> flatHandles;
    deepHandles.reserve(64);
    flatHandles.reserve(64);
    for (std::size_t index = 0; index < 64; ++index)
    {
        const double xMin = position(random);
        const double yMin = position(random);
        const auto shape = Rectangle(xMin, yMin, xMin + size(random),
                                     yMin + size(random));
        const jaguar::Hazard hazard{
            {jaguar::HazardKind::PlacedItem, 1000 + index, 0, 0}, shape, true};
        const auto deepHandle = deepEngine.RegisterHazard(hazard);
        const auto flatHandle = flatEngine.RegisterHazard(hazard);
        assert(deepHandle.HasValue());
        assert(flatHandle.HasValue());
        deepHandles.push_back(deepHandle.Value());
        flatHandles.push_back(flatHandle.Value());
    }

    std::uniform_real_distribution<double> queryPosition(-4.0, 104.0);
    std::uniform_real_distribution<double> querySize(0.25, 8.0);
    std::vector<jaguar::SimplePolygon> randomQueries;
    randomQueries.reserve(256);
    for (std::size_t index = 0; index < 256; ++index)
    {
        const double xMin = queryPosition(random);
        const double yMin = queryPosition(random);
        const auto query = jaguar::SimplePolygon::Create({
            {xMin, yMin},
            {xMin + querySize(random), yMin},
            {xMin + querySize(random), yMin + querySize(random)},
            {xMin, yMin + querySize(random)}});
        assert(query.HasValue());
        randomQueries.push_back(query.Value());
    }

    const auto assertRandomAgreement =
        [](const jaguar::CollisionEngine& leftEngine,
           const jaguar::CollisionEngine& rightEngine,
           const std::vector<jaguar::SimplePolygon>& queries) {
        for (const auto& query : queries)
        {
            assert(leftEngine.DetectPolygonCollision(query, jaguar::NoFilter{}) ==
                   rightEngine.DetectPolygonCollision(query, jaguar::NoFilter{}));
        }
    };
    assertRandomAgreement(deepEngine, flatEngine, randomQueries);
    const auto randomSnapshot = deepEngine.Save();
    assert(randomSnapshot.dynamicHazards.size() == 64);
    jaguar::CollisionEngine restoredDeepEngine(randomRoot.Value(), deepConfig);
    assert(restoredDeepEngine.Restore(randomSnapshot).HasValue());
    assertRandomAgreement(restoredDeepEngine, flatEngine, randomQueries);

    for (std::size_t index = 0; index < deepHandles.size(); index += 3)
    {
        assert(deepEngine.DeregisterHazard(deepHandles[index]).HasValue());
        assert(flatEngine.DeregisterHazard(flatHandles[index]).HasValue());
    }
    assertRandomAgreement(deepEngine, flatEngine, randomQueries);
    const auto reducedSnapshot = deepEngine.Save();
    assert(reducedSnapshot.dynamicHazards.size() == 42);
    jaguar::CollisionEngine restoredReducedEngine(randomRoot.Value(), deepConfig);
    assert(restoredReducedEngine.Restore(reducedSnapshot).HasValue());
    assertRandomAgreement(restoredReducedEngine, flatEngine, randomQueries);
}
