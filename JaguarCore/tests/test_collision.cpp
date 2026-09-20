#include "JaguarCore/collision/collision_engine.h"

#include <cassert>
#include <algorithm>
#include <memory>
#include <random>
#include <thread>
#include <vector>

namespace
{
std::shared_ptr<const jaguar::SimplePolygon> rectangle(double xMin,
                                                       double yMin,
                                                       double xMax,
                                                       double yMax)
{
    const auto polygon = jaguar::SimplePolygon::create({
        {xMin, yMin}, {xMax, yMin}, {xMax, yMax}, {xMin, yMax}});
    assert(polygon.hasValue());
    return std::make_shared<const jaguar::SimplePolygon>(polygon.value());
}
}

void runCollisionTests()
{
    const auto root = jaguar::Rect2d::create(0.0, 0.0, 10.0, 10.0);
    assert(root.hasValue());

    jaguar::QuadTree quadtree(root.value(), 4, 0);
    const auto containedShape = jaguar::SimplePolygon::create({
        {4.0, 4.0}, {4.5, 4.0}, {4.5, 4.5}, {4.0, 4.5}});
    assert(containedShape.hasValue());
    const jaguar::HazardHandle containedHandle{42, 1};
    quadtree.registerHazard(containedHandle,
                            {jaguar::HazardKind::PlacedItem, 42, 0, 0},
                            std::make_shared<const jaguar::SimplePolygon>(
                                containedShape.value()), true);
    assert(quadtree.presenceAt(containedHandle, root.value()) ==
           jaguar::HazardPresence::Partial);
    assert(quadtree.nodeCount() > 1);
    assert(quadtree.presenceAt(containedHandle, root.value().quadrants()[1]) ==
           jaguar::HazardPresence::None);
    const auto focusedQuery = root.value().quadrants()[2];
    const auto virtualResult = quadtree.queryVirtualRoot(focusedQuery);
    const auto fullResult = quadtree.query(focusedQuery);
    assert(virtualResult == fullResult);

    const auto coveringShape = jaguar::SimplePolygon::create({
        {-1.0, -1.0}, {11.0, -1.0}, {11.0, 11.0}, {-1.0, 11.0}});
    assert(coveringShape.hasValue());
    const jaguar::HazardHandle coveringHandle{43, 1};
    quadtree.registerHazard(coveringHandle,
                            {jaguar::HazardKind::PlacedItem, 43, 0, 0},
                            std::make_shared<const jaguar::SimplePolygon>(
                                coveringShape.value()), false);
    assert(quadtree.presenceAt(coveringHandle, root.value()) ==
           jaguar::HazardPresence::Entire);
    assert(quadtree.queryVirtualRoot(root.value()) ==
           quadtree.query(root.value()));

    jaguar::HazardCollector collector;
    collector.insert(containedHandle,
                     {jaguar::HazardKind::PlacedItem, 42, 0, 0});
    collector.insert(containedHandle,
                     {jaguar::HazardKind::PlacedItem, 99, 0, 0});
    assert(collector.size() == 1);
    assert(collector.containsEntity(
        {jaguar::HazardKind::PlacedItem, 99, 0, 0}));
    assert(collector.removeEntity(
        {jaguar::HazardKind::PlacedItem, 99, 0, 0}));
    assert(collector.empty());

    collector.insert({50, 1}, {jaguar::HazardKind::PlacedItem, 77, 0, 0});
    collector.insert({51, 1}, {jaguar::HazardKind::PlacedItem, 77, 0, 0});
    assert(collector.size() == 2);

    jaguar::CollisionEngine duplicateEntityEngine(
        root.value(), jaguar::CDEConfig{});
    assert(duplicateEntityEngine.registerHazard({
               {jaguar::HazardKind::PlacedItem, 77, 0, 0},
               rectangle(1.0, 1.0, 2.0, 2.0), true})
               .hasValue());
    assert(duplicateEntityEngine.registerHazard({
               {jaguar::HazardKind::PlacedItem, 77, 0, 0},
               rectangle(3.0, 1.0, 4.0, 2.0), true})
               .hasValue());
    const auto duplicateQuery = jaguar::SimplePolygon::create({
        {0.0, 0.0}, {5.0, 0.0}, {5.0, 3.0}, {0.0, 3.0}});
    assert(duplicateQuery.hasValue());
    assert(duplicateEntityEngine.collectPolygonCollisions(
                duplicateQuery.value()).size() == 2);

    jaguar::CollisionEngine handleReuseEngine(root.value(), jaguar::CDEConfig{});
    const auto firstHandle = handleReuseEngine.registerHazard({
        {jaguar::HazardKind::PlacedItem, 501, 0, 0},
        rectangle(1.0, 1.0, 2.0, 2.0), true});
    assert(firstHandle.hasValue());
    assert(handleReuseEngine.deregisterHazard(firstHandle.value()).hasValue());
    const auto replacementHandle = handleReuseEngine.registerHazard({
        {jaguar::HazardKind::PlacedItem, 502, 0, 0},
        rectangle(5.0, 5.0, 6.0, 6.0), true});
    assert(replacementHandle.hasValue());
    assert(!(replacementHandle.value() == firstHandle.value()));
    const auto staleDeregister =
        handleReuseEngine.deregisterHazard(firstHandle.value());
    assert(!staleDeregister.hasValue());
    assert(staleDeregister.error().code == jaguar::ErrorCode::InvalidInput);
    const auto firstQuery = jaguar::SimplePolygon::create({
        {1.2, 1.2}, {1.8, 1.2}, {1.8, 1.8}, {1.2, 1.8}});
    const auto replacementQuery = jaguar::SimplePolygon::create({
        {5.2, 5.2}, {5.8, 5.2}, {5.8, 5.8}, {5.2, 5.8}});
    assert(firstQuery.hasValue() && replacementQuery.hasValue());
    assert(!handleReuseEngine.detectPolygonCollision(
        firstQuery.value(), jaguar::NoFilter{}));
    assert(handleReuseEngine.detectPolygonCollision(
        replacementQuery.value(), jaguar::NoFilter{}));

    jaguar::QuadTree handleReuseTree(root.value(), 4, 1);
    handleReuseTree.registerHazard(
        replacementHandle.value(),
        {jaguar::HazardKind::PlacedItem, 502, 0, 0},
        rectangle(5.0, 5.0, 6.0, 6.0), true);
    assert(handleReuseTree.presenceAt(firstHandle.value(), root.value()) ==
           jaguar::HazardPresence::None);
    assert(handleReuseTree.query(root.value()).size() == 1);

    jaguar::CollisionEngine callbackEngine(root.value(), jaguar::CDEConfig{});
    const auto callbackHazardA = callbackEngine.registerHazard({
        {jaguar::HazardKind::PlacedItem, 100, 0, 0},
        rectangle(2.0, 2.0, 4.0, 4.0), true});
    const auto callbackHazardB = callbackEngine.registerHazard({
        {jaguar::HazardKind::PlacedItem, 101, 0, 0},
        rectangle(4.0, 2.0, 6.0, 4.0), true});
    assert(callbackHazardA.hasValue());
    assert(callbackHazardB.hasValue());
    const auto callbackShape = jaguar::SimplePolygon::create({
        {1.0, 1.0}, {7.0, 1.0}, {7.0, 5.0}, {1.0, 5.0}});
    assert(callbackShape.hasValue());

    jaguar::HazardCollector callbackCollector;
    std::size_t callbackCount = 0;
    const bool stopped = callbackEngine.collectPolygonCollisionsUntil(
        callbackShape.value(), callbackCollector, jaguar::NoFilter{},
        [&callbackCount](const jaguar::HazardEntity&) {
            ++callbackCount;
            return true;
        });
    assert(stopped);
    assert(callbackCount == 1);
    assert(callbackCollector.size() == 1);

    const bool completed = callbackEngine.collectPolygonCollisionsUntil(
        callbackShape.value(), callbackCollector, jaguar::NoFilter{},
        [](const jaguar::HazardEntity&) { return false; });
    assert(!completed);
    assert(callbackCollector.size() == 2);

    jaguar::SurrogateConfig callbackSurrogateConfig{};
    callbackSurrogateConfig.poleLimits = {{{2, 0.0}, {1, 0.8}, {1, 0.9}}};
    callbackSurrogateConfig.failFastPoleAreaRatio = 1.0;
    const auto callbackSurrogateShape = jaguar::SimplePolygon::create({
        {0.0, 0.0}, {2.0, 0.0}, {2.0, 2.0}, {0.0, 2.0}});
    assert(callbackSurrogateShape.hasValue());
    const auto callbackSurrogate = jaguar::Surrogate::create(
        callbackSurrogateShape.value(), callbackSurrogateConfig);
    assert(callbackSurrogate.hasValue());
    jaguar::CollisionEngine surrogateCallbackEngine(root.value(), jaguar::CDEConfig{});
    const auto surrogateCallbackHazard = surrogateCallbackEngine.registerHazard({
        {jaguar::HazardKind::PlacedItem, 102, 0, 0},
        rectangle(4.0, 4.0, 6.0, 6.0), true});
    assert(surrogateCallbackHazard.hasValue());
    jaguar::HazardCollector surrogateCollector;
    std::size_t surrogateCallbackCount = 0;
    const bool surrogateStopped =
        surrogateCallbackEngine.collectSurrogateCollisionsUntil(
            callbackSurrogate.value(),
            jaguar::Transform2d::fromRotationTranslation(0.0, {4.0, 4.0}),
            surrogateCollector, jaguar::NoFilter{},
            [&surrogateCallbackCount](const jaguar::HazardEntity&) {
                ++surrogateCallbackCount;
                return true;
            });
    assert(surrogateStopped);
    assert(surrogateCallbackCount == 1);
    assert(surrogateCollector.size() == 1);
    assert(!surrogateCallbackEngine.collectSurrogateCollisionsUntil(
        callbackSurrogate.value(),
        jaguar::Transform2d::fromRotationTranslation(0.0, {0.0, 0.0}),
        surrogateCollector, jaguar::NoFilter{},
        [](const jaguar::HazardEntity&) { return false; }));
    assert(surrogateCollector.size() == 1);

    jaguar::CDEConfig config{};
    config.quadtreeDepth = 4;
    config.cdThreshold = 8;

    jaguar::CollisionEngine engine(root.value(), config);
    const auto invalidDynamicExterior = engine.registerHazard({
        {jaguar::HazardKind::Exterior, 0, 0, 0},
        rectangle(0.0, 0.0, 10.0, 10.0), true});
    assert(!invalidDynamicExterior.hasValue());
    assert(invalidDynamicExterior.error().code == jaguar::ErrorCode::InvalidInput);

    const auto hazard = engine.registerHazard({
        {jaguar::HazardKind::PlacedItem, 7, 0, 0}, rectangle(4.0, 4.0, 6.0, 6.0), true});
    assert(hazard.hasValue());

    const auto colliding = jaguar::SimplePolygon::create({
        {5.0, 5.0}, {7.0, 5.0}, {7.0, 7.0}, {5.0, 7.0}});
    assert(colliding.hasValue());
    assert(engine.detectPolygonCollision(colliding.value(), jaguar::NoFilter{}));

    const auto batchClear = jaguar::SimplePolygon::create({
        {0.5, 0.5}, {1.5, 0.5}, {1.5, 1.5}, {0.5, 1.5}});
    assert(batchClear.hasValue());
    const auto batchOutside = jaguar::SimplePolygon::create({
        {-1.0, 1.0}, {1.0, 1.0}, {1.0, 2.0}, {-1.0, 2.0}});
    assert(batchOutside.hasValue());
    const std::vector<jaguar::SimplePolygon> batch{
        colliding.value(), batchClear.value(), batchOutside.value()};
    const auto batchResults = engine.detectPolygonCollisions(batch);
    assert(batchResults.size() == batch.size());
    assert(batchResults[0] == 1);
    assert(batchResults[1] == 0);
    assert(batchResults[2] == 1);
    for (std::size_t i = 0; i < batch.size(); ++i)
    {
        assert(batchResults[i] ==
               (engine.detectPolygonCollision(batch[i], jaguar::NoFilter{}) ? 1 : 0));
    }

    jaguar::HazardFilter ignorePlaced{};
    ignorePlaced.irrelevant = [](const jaguar::HazardEntity& entity) {
        return entity.kind == jaguar::HazardKind::PlacedItem;
    };
    const auto filteredBatchResults = engine.detectPolygonCollisions(batch, ignorePlaced);
    assert(filteredBatchResults.size() == batch.size());
    assert(filteredBatchResults[0] == 0);
    assert(filteredBatchResults[1] == 0);
    assert(filteredBatchResults[2] == 1);

    const auto emptyBatchResults =
        engine.detectPolygonCollisions(std::vector<jaguar::SimplePolygon>{});
    assert(emptyBatchResults.empty());

    jaguar::BatchCollisionOptions parallelOptions{};
    parallelOptions.workerCount = 2;
    parallelOptions.grainSize = 1;
    const auto parallelResults = engine.detectPolygonCollisionsParallel(
        batch, jaguar::NoFilter{}, parallelOptions);
    assert(parallelResults == batchResults);
    const auto singleWorkerResults = engine.detectPolygonCollisionsParallel(
        batch, jaguar::NoFilter{}, {1, 2});
    assert(singleWorkerResults == batchResults);
    for (std::size_t repeat = 0; repeat < 20; ++repeat)
    {
        assert(engine.detectPolygonCollisionsParallel(
                   batch, jaguar::NoFilter{}, parallelOptions) == batchResults);
    }

    std::vector<std::uint8_t> concurrentLeft;
    std::vector<std::uint8_t> concurrentRight;
    std::thread left([&] {
        concurrentLeft = engine.detectPolygonCollisionsParallel(
            batch, jaguar::NoFilter{}, parallelOptions);
    });
    std::thread right([&] {
        concurrentRight = engine.detectPolygonCollisionsParallel(
            batch, jaguar::NoFilter{}, {2, 1});
    });
    left.join();
    right.join();
    assert(concurrentLeft == batchResults);
    assert(concurrentRight == batchResults);

    const auto innerContainer = rectangle(2.0, 2.0, 8.0, 8.0);
    const auto exteriorHazard = engine.registerHazard({
        {jaguar::HazardKind::Exterior, 0, 0, 0}, innerContainer, false});
    assert(exteriorHazard.hasValue());

    const auto outsideInnerContainer = jaguar::SimplePolygon::create({
        {0.5, 0.5}, {1.0, 0.5}, {1.0, 1.0}, {0.5, 1.0}});
    assert(outsideInnerContainer.hasValue());
    assert(engine.detectPolygonCollision(
        outsideInnerContainer.value(), jaguar::NoFilter{}));
    const auto exteriorCollisions = engine.collectPolygonCollisions(
        outsideInnerContainer.value());
    assert(std::any_of(exteriorCollisions.begin(), exteriorCollisions.end(),
                       [](const auto& entity) {
                           return entity.kind == jaguar::HazardKind::Exterior;
                       }));

    const auto insideInnerContainer = jaguar::SimplePolygon::create({
        {3.0, 3.0}, {4.0, 3.0}, {4.0, 4.0}, {3.0, 4.0}});
    assert(insideInnerContainer.hasValue());
    const auto insideCollisions = engine.collectPolygonCollisions(
        insideInnerContainer.value());
    assert(std::none_of(insideCollisions.begin(), insideCollisions.end(),
                        [](const auto& entity) {
                            return entity.kind == jaguar::HazardKind::Exterior;
                        }));

    jaguar::HazardFilter exteriorFilter{};
    exteriorFilter.irrelevant = [](const jaguar::HazardEntity& entity) {
        return entity.kind == jaguar::HazardKind::Exterior;
    };
    assert(!engine.detectPolygonCollision(
        outsideInnerContainer.value(), exteriorFilter));

    const auto clearAfterExterior = jaguar::SimplePolygon::create({
        {2.5, 2.5}, {3.5, 2.5}, {3.5, 3.5}, {2.5, 3.5}});
    assert(clearAfterExterior.hasValue());
    assert(!engine.detectPolygonCollision(clearAfterExterior.value(), jaguar::NoFilter{}));

    const auto collisions = engine.collectPolygonCollisions(colliding.value());
    assert(collisions.size() == 1);
    assert(collisions.front().kind == jaguar::HazardKind::PlacedItem);

    const auto outsideAfterExterior = jaguar::SimplePolygon::create({
        {-1.0, 1.0}, {1.0, 1.0}, {1.0, 2.0}, {-1.0, 2.0}});
    assert(outsideAfterExterior.hasValue());
    assert(engine.detectPolygonCollision(outsideAfterExterior.value(), jaguar::NoFilter{}));
    const auto outsideCollisions = engine.collectPolygonCollisions(outsideAfterExterior.value());
    assert(outsideCollisions.size() == 1);
    assert(outsideCollisions.front().kind == jaguar::HazardKind::Exterior);

    const auto enclosing = jaguar::SimplePolygon::create({
        {-1.0, -1.0}, {11.0, -1.0}, {11.0, 11.0}, {-1.0, 11.0}});
    assert(enclosing.hasValue());
    assert(engine.detectPolygonCollision(enclosing.value(), jaguar::NoFilter{}));
    const auto enclosingCollisions = engine.collectPolygonCollisions(enclosing.value());
    const bool enclosingHasExterior = std::any_of(
        enclosingCollisions.begin(), enclosingCollisions.end(), [](const auto& entity) {
            return entity.kind == jaguar::HazardKind::Exterior;
        });
    assert(enclosingHasExterior);

    const auto surrogateShape = jaguar::SimplePolygon::create({
        {0.0, 0.0}, {2.0, 0.0}, {2.0, 2.0}, {0.0, 2.0}});
    assert(surrogateShape.hasValue());
    jaguar::SurrogateConfig surrogateConfig{};
    surrogateConfig.poleLimits = {{{2, 0.0}, {1, 0.8}, {1, 0.9}}};
    surrogateConfig.failFastPoleAreaRatio = 1.0;
    const auto surrogate = jaguar::Surrogate::create(surrogateShape.value(), surrogateConfig);
    assert(surrogate.hasValue());
    assert(engine.detectSurrogateCollision(
        surrogate.value(), jaguar::Transform2d::fromRotationTranslation(0.0, {4.0, 4.0}),
        jaguar::NoFilter{}));
    assert(!engine.detectSurrogateCollision(
        surrogate.value(),
        jaguar::Transform2d::fromRotationTranslation(0.0, {20.0, 20.0}),
        exteriorFilter));
    jaguar::HazardFilter exteriorHandleFilter{};
    exteriorHandleFilter.irrelevantHandle =
        [exteriorHazardHandle = exteriorHazard.value()](
            jaguar::HazardHandle handle, const jaguar::HazardEntity&) {
            return handle == exteriorHazardHandle;
        };
    assert(!engine.detectSurrogateCollision(
        surrogate.value(),
        jaguar::Transform2d::fromRotationTranslation(0.0, {20.0, 20.0}),
        exteriorHandleFilter));

    jaguar::CollisionEngine duplicateSnapshotEngine(
        root.value(), jaguar::CDEConfig{});
    assert(duplicateSnapshotEngine.registerHazard({
               {jaguar::HazardKind::PlacedItem, 77, 0, 0},
               rectangle(1.0, 1.0, 2.0, 2.0), true})
               .hasValue());
    assert(duplicateSnapshotEngine.registerHazard({
               {jaguar::HazardKind::PlacedItem, 77, 0, 0},
               rectangle(3.0, 1.0, 4.0, 2.0), true})
               .hasValue());
    const auto duplicateSnapshot = duplicateSnapshotEngine.save();
    assert(duplicateSnapshot.dynamicHazards.size() == 2);
    assert(duplicateSnapshotEngine.restore(duplicateSnapshot).hasValue());
    assert(duplicateSnapshotEngine.collectPolygonCollisions(
                duplicateQuery.value()).size() == 2);

    const auto snapshot = engine.save();
    const auto snapshotBeforeFailedRestore = engine.save();
    assert(engine.deregisterHazard(hazard.value()).hasValue());
    assert(!engine.detectPolygonCollision(colliding.value(), jaguar::NoFilter{}));
    assert(engine.restore(snapshot).hasValue());
    assert(engine.detectPolygonCollision(colliding.value(), jaguar::NoFilter{}));

    jaguar::CollisionSnapshot nonDynamicSnapshot = snapshot;
    nonDynamicSnapshot.dynamicHazards.front().dynamic = false;
    const auto nonDynamicRestore = engine.restore(nonDynamicSnapshot);
    assert(!nonDynamicRestore.hasValue());
    assert(nonDynamicRestore.error().code == jaguar::ErrorCode::SnapshotMismatch);
    assert(engine.detectPolygonCollision(colliding.value(), jaguar::NoFilter{}));

    jaguar::CollisionSnapshot invalidSnapshot = engine.save();
    invalidSnapshot.dynamicHazards.clear();
    invalidSnapshot.dynamicHazards.push_back(
        {{jaguar::HazardKind::PlacedItem, 999, 0, 0}, nullptr, true});
    const auto invalidRestore = engine.restore(invalidSnapshot);
    assert(!invalidRestore.hasValue());
    assert(invalidRestore.error().code == jaguar::ErrorCode::InvalidInput);
    assert(engine.detectPolygonCollision(colliding.value(), jaguar::NoFilter{}));

    const auto snapshotAfterFailedRestore = engine.save();
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
        assert(beforeHazard.shape->vertexCount() ==
               afterHazard.shape->vertexCount());
        for (std::size_t vertex = 0;
             vertex < beforeHazard.shape->vertexCount(); ++vertex)
        {
            assert(std::abs(beforeHazard.shape->vertices()[vertex].x -
                            afterHazard.shape->vertices()[vertex].x) < 1.0e-12);
            assert(std::abs(beforeHazard.shape->vertices()[vertex].y -
                            afterHazard.shape->vertices()[vertex].y) < 1.0e-12);
        }
    }

    jaguar::CollisionSnapshot staticHazardAsDynamic = engine.save();
    assert(!staticHazardAsDynamic.dynamicHazards.empty());
    staticHazardAsDynamic.dynamicHazards.front().entity.kind =
        jaguar::HazardKind::Exterior;
    const auto staticHazardRestore = engine.restore(staticHazardAsDynamic);
    assert(!staticHazardRestore.hasValue());
    assert(staticHazardRestore.error().code == jaguar::ErrorCode::SnapshotMismatch);
    assert(engine.detectPolygonCollision(colliding.value(), jaguar::NoFilter{}));

    // Fixed-seed larger differential: a deeply subdivided tree must agree
    // with a deliberately non-subdivided index before and after atomic
    // snapshot restore, including after deterministic removals.
    const auto randomRoot = jaguar::Rect2d::create(0.0, 0.0, 100.0, 100.0);
    assert(randomRoot.hasValue());
    jaguar::CDEConfig deepConfig{};
    deepConfig.quadtreeDepth = 6;
    deepConfig.cdThreshold = 4;
    jaguar::CDEConfig flatConfig = deepConfig;
    flatConfig.cdThreshold = 255;
    jaguar::CollisionEngine deepEngine(randomRoot.value(), deepConfig);
    jaguar::CollisionEngine flatEngine(randomRoot.value(), flatConfig);

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
        const auto shape = rectangle(xMin, yMin, xMin + size(random),
                                     yMin + size(random));
        const jaguar::Hazard hazard{
            {jaguar::HazardKind::PlacedItem, 1000 + index, 0, 0}, shape, true};
        const auto deepHandle = deepEngine.registerHazard(hazard);
        const auto flatHandle = flatEngine.registerHazard(hazard);
        assert(deepHandle.hasValue());
        assert(flatHandle.hasValue());
        deepHandles.push_back(deepHandle.value());
        flatHandles.push_back(flatHandle.value());
    }

    std::uniform_real_distribution<double> queryPosition(-4.0, 104.0);
    std::uniform_real_distribution<double> querySize(0.25, 8.0);
    std::vector<jaguar::SimplePolygon> randomQueries;
    randomQueries.reserve(256);
    for (std::size_t index = 0; index < 256; ++index)
    {
        const double xMin = queryPosition(random);
        const double yMin = queryPosition(random);
        const auto query = jaguar::SimplePolygon::create({
            {xMin, yMin},
            {xMin + querySize(random), yMin},
            {xMin + querySize(random), yMin + querySize(random)},
            {xMin, yMin + querySize(random)}});
        assert(query.hasValue());
        randomQueries.push_back(query.value());
    }

    const auto assertRandomAgreement =
        [](const jaguar::CollisionEngine& leftEngine,
           const jaguar::CollisionEngine& rightEngine,
           const std::vector<jaguar::SimplePolygon>& queries) {
        for (const auto& query : queries)
        {
            assert(leftEngine.detectPolygonCollision(query, jaguar::NoFilter{}) ==
                   rightEngine.detectPolygonCollision(query, jaguar::NoFilter{}));
        }
    };
    assertRandomAgreement(deepEngine, flatEngine, randomQueries);
    const auto randomSnapshot = deepEngine.save();
    assert(randomSnapshot.dynamicHazards.size() == 64);
    jaguar::CollisionEngine restoredDeepEngine(randomRoot.value(), deepConfig);
    assert(restoredDeepEngine.restore(randomSnapshot).hasValue());
    assertRandomAgreement(restoredDeepEngine, flatEngine, randomQueries);

    for (std::size_t index = 0; index < deepHandles.size(); index += 3)
    {
        assert(deepEngine.deregisterHazard(deepHandles[index]).hasValue());
        assert(flatEngine.deregisterHazard(flatHandles[index]).hasValue());
    }
    assertRandomAgreement(deepEngine, flatEngine, randomQueries);
    const auto reducedSnapshot = deepEngine.save();
    assert(reducedSnapshot.dynamicHazards.size() == 42);
    jaguar::CollisionEngine restoredReducedEngine(randomRoot.value(), deepConfig);
    assert(restoredReducedEngine.restore(reducedSnapshot).hasValue());
    assertRandomAgreement(restoredReducedEngine, flatEngine, randomQueries);
}
