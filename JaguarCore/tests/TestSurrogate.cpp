#include "JaguarCore/FailFast/Surrogate.h"
#include "JaguarCore/Geometry/ShapeModification.h"

#include <cassert>
#include <cmath>
#include <limits>
#include <random>
#include <vector>

void runSurrogateTests()
{
    const auto polygon = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {0.0, 4.0}, {10.0, 4.0}, {10.0, 0.0}});
    assert(polygon.HasValue());

    jaguar::SurrogateConfig config{};
    config.poleLimits = {{{8, 0.0}, {4, 0.75}, {2, 0.9}}};
    config.failFastPoleAreaRatio = 0.5;
    config.failFastPiers = 1;

    const auto surrogate = jaguar::Surrogate::Create(polygon.Value(), config);
    assert(surrogate.HasValue());
    assert(!surrogate.Value().Poles().empty());
    assert(surrogate.Value().FailFastPoleCount() > 0);
    assert(surrogate.Value().Piers().size() == 1);
    assert(std::abs(surrogate.Value().ConvexHullArea() - 40.0) < 1.0e-9);

    const auto offsetSquare = jaguar::SimplePolygon::Create({
        {100.0, 100.0}, {101.0, 100.0}, {101.0, 101.0}, {100.0, 101.0}});
    assert(offsetSquare.HasValue());
    jaguar::SurrogateConfig missingPierConfig{};
    missingPierConfig.poleLimits = {{{1, 0.0}, {0, 0.0}, {0, 0.0}}};
    missingPierConfig.failFastPiers = 1;
    const auto missingPierSurrogate = jaguar::Surrogate::Create(
        offsetSquare.Value(), missingPierConfig);
    assert(!missingPierSurrogate.HasValue());
    assert(missingPierSurrogate.GetError().code == jaguar::ErrorCode::Infeasible);

    const float tinySide = std::numeric_limits<float>::epsilon() * 0.5F;
    const auto tinySquare = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {tinySide, 0.0}, {tinySide, tinySide}, {0.0, tinySide}});
    assert(tinySquare.HasValue());
    jaguar::SurrogateConfig tinyConfig{};
    tinyConfig.poleLimits = {{{2, 0.0}, {2, 0.0}, {2, 0.0}}};
    tinyConfig.failFastPiers = 1;
    const auto tinySurrogate = jaguar::Surrogate::Create(
        tinySquare.Value(), tinyConfig);
    assert(tinySurrogate.HasValue());
    assert(tinySurrogate.Value().Piers().size() == 1);
    const auto& tinyPier = tinySurrogate.Value().Piers().front();
    assert(std::abs(tinyPier.start.x - tinySide) < 1.0e-13);
    assert(std::abs(tinyPier.start.y - 2.9801939e-11) < 1.0e-15);
    assert(std::abs(tinyPier.end.x - tinySide) < 1.0e-13);
    assert(std::abs(tinyPier.end.y - 5.9574844613e-8) < 1.0e-15);

    for (const auto& pier : surrogate.Value().Piers())
    {
        assert(polygon.Value().Contains(pier.start));
        assert(polygon.Value().Contains(pier.end));
        assert(pier.Length() < polygon.Value().BoundingBox().Width());
        const jaguar::Point2d midpoint{
            (pier.start.x + pier.end.x) * 0.5,
            (pier.start.y + pier.end.y) * 0.5};
        assert(polygon.Value().Contains(midpoint));
    }

    for (const auto& pole : surrogate.Value().Poles())
    {
        assert(polygon.Value().Contains(pole.center));
        assert(pole.radius > 0.0);
        (void)pole;
    }

    const auto concave = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {4.0, 0.0}, {4.0, 4.0}, {2.0, 2.0}, {0.0, 4.0}});
    assert(concave.HasValue());
    const auto concaveSurrogate = jaguar::Surrogate::Create(concave.Value(), config);
    assert(concaveSurrogate.HasValue());
    assert(!concaveSurrogate.Value().Poles().empty());

    jaguar::SurrogateConfig thresholdConfig{};
    thresholdConfig.poleLimits = {{{2, 0.9}, {0, 0.0}, {0, 0.0}}};
    const auto thresholdSurrogate = jaguar::Surrogate::Create(
        polygon.Value(), thresholdConfig);
    assert(thresholdSurrogate.HasValue());
    assert(thresholdSurrogate.Value().Poles().size() >= 2);

    const auto uShape = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {10.0, 0.0}, {10.0, 10.0}, {6.0, 10.0},
        {6.0, 4.0}, {4.0, 4.0}, {4.0, 10.0}, {0.0, 10.0}});
    assert(uShape.HasValue());
    jaguar::SurrogateConfig singleNonzeroLimitConfig{};
    singleNonzeroLimitConfig.poleLimits = {{{8, 0.0}, {0, 0.0}, {0, 0.0}}};
    const auto singleNonzeroLimitSurrogate = jaguar::Surrogate::Create(
        uShape.Value(), singleNonzeroLimitConfig);
    assert(singleNonzeroLimitSurrogate.HasValue());
    assert(singleNonzeroLimitSurrogate.Value().Poles().size() == 2);

    jaguar::SurrogateConfig roundedStateConfig{};
    roundedStateConfig.poleLimits =
        {{{2, 0.75000001}, {2, 0.75000001}, {2, 0.75000001}}};
    roundedStateConfig.failFastPoleAreaRatio = 0.50000001;
    const auto roundedStateSurrogate = jaguar::Surrogate::Create(
        uShape.Value(), roundedStateConfig);
    assert(roundedStateSurrogate.HasValue());
    assert(roundedStateSurrogate.Value().Config().poleLimits[0].second == 0.75);
    assert(roundedStateSurrogate.Value().Config().failFastPoleAreaRatio == 0.5);
    assert(roundedStateSurrogate.Value().Config() == roundedStateConfig);

    jaguar::SimplePolygon cachedConfigShape = uShape.Value();
    assert(cachedConfigShape.GenerateSurrogate(roundedStateConfig).HasValue());
    const jaguar::Surrogate* cachedConfig = cachedConfigShape.GetSurrogate();
    jaguar::SurrogateConfig equivalentF32Config = roundedStateConfig;
    equivalentF32Config.poleLimits = {{{2, 0.75}, {2, 0.75}, {2, 0.75}}};
    equivalentF32Config.failFastPoleAreaRatio = 0.5;
    assert(cachedConfigShape.GenerateSurrogate(equivalentF32Config).HasValue());
    assert(cachedConfigShape.GetSurrogate() == cachedConfig);

    jaguar::SurrogateConfig uConfig{};
    uConfig.poleLimits = {{{1, 0.0}, {0, 0.0}, {0, 0.0}}};
    uConfig.failFastPiers = 1;
    const auto uSurrogate = jaguar::Surrogate::Create(uShape.Value(), uConfig);
    assert(uSurrogate.HasValue());
    assert(uSurrogate.Value().Piers().size() == 1);
    assert(uSurrogate.Value().Piers().front().Length() > 8.0);

    // Rust jagua-rs 0.9.0 oracle: the same U shape with the configured
    // largest-remaining-first pole limits and 50% fail-fast coverage.
    jaguar::SurrogateConfig rustOracleConfig{};
    rustOracleConfig.poleLimits = {{{8, 0.0}, {4, 0.75}, {2, 0.9}}};
    rustOracleConfig.failFastPoleAreaRatio = 0.5;
    rustOracleConfig.failFastPiers = 2;
    const auto rustOracleSurrogate = jaguar::Surrogate::Create(
        uShape.Value(), rustOracleConfig);
    assert(rustOracleSurrogate.HasValue());
    assert(rustOracleSurrogate.Value().Poles().size() == 7);
    assert(rustOracleSurrogate.Value().FailFastPoleCount() == 3);
    assert(rustOracleSurrogate.Value().Piers().size() == 2);
    assert(std::abs(rustOracleSurrogate.Value().ConvexHullArea() - 100.0) <
           1.0e-9);
    assert((rustOracleSurrogate.Value().ConvexHullIndices() ==
            std::vector<std::size_t>{0U, 1U, 2U, 7U}));
    const auto transformedRustSurrogate =
        rustOracleSurrogate.Value().TransformedRustF32(
            jaguar::Transform2d::FromRotationTranslation(0.0, {1.0, 2.0}));
    assert(transformedRustSurrogate.HasValue());
    assert(transformedRustSurrogate.Value().ConvexHullIndices() ==
           rustOracleSurrogate.Value().ConvexHullIndices());
    const auto& rustPole = rustOracleSurrogate.Value().Poles().front();
    assert(std::abs(rustPole.center.x - 2.34375) < 1.0e-5);
    assert(std::abs(rustPole.center.y - 2.34375) < 1.0e-5);
    assert(std::abs(rustPole.radius - 2.342291116714) < 1.0e-5);
    const auto& rustPier = rustOracleSurrogate.Value().Piers().front();
#ifdef JAGUARCORE_TEST_RELEASE
    assert(std::abs(rustPier.start.x - 3.998000144958) < 1.0e-5);
    assert(std::abs(rustPier.start.y - 9.050000190735) < 1.0e-5);
    assert(std::abs(rustPier.end.x - 0.001999974251) < 1.0e-5);
    assert(std::abs(rustPier.end.y - 9.050001144409) < 1.0e-5);
#else
    assert(std::abs(rustPier.start.x - 3.998656988144) < 1.0e-5);
    assert(std::abs(rustPier.start.y - 7.315051555634) < 1.0e-5);
    assert(std::abs(rustPier.end.x - 1.315051674843) < 1.0e-5);
    assert(std::abs(rustPier.end.y - 9.998657226562) < 1.0e-5);
#endif

    const auto lShape = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {8.0, 0.0}, {8.0, 3.0},
        {3.0, 3.0}, {3.0, 8.0}, {0.0, 8.0}});
    assert(lShape.HasValue());
    const auto lSurrogate = jaguar::Surrogate::Create(
        lShape.Value(), rustOracleConfig);
    assert(lSurrogate.HasValue());
    assert(lSurrogate.Value().Poles().size() == 7);
    assert(lSurrogate.Value().FailFastPoleCount() == 3);
    assert(lSurrogate.Value().Piers().size() == 2);
    assert(std::abs(lSurrogate.Value().ConvexHullArea() - 51.5) < 1.0e-9);
    assert((lSurrogate.Value().ConvexHullIndices() ==
            std::vector<std::size_t>{0U, 1U, 2U, 4U, 5U}));
    assert(std::abs(lSurrogate.Value().Poles()[0].center.x - 1.7578125) < 1.0e-6);
    assert(std::abs(lSurrogate.Value().Poles()[0].center.y - 1.7578125) < 1.0e-6);
    assert(std::abs(lSurrogate.Value().Poles()[0].radius - 1.756718397141) < 1.0e-6);
    const auto& lPier = lSurrogate.Value().Piers().front();
#ifdef JAGUARCORE_TEST_RELEASE
    assert(std::abs(lPier.start.x - 7.638461112976) < 1.0e-5);
    assert(std::abs(lPier.start.y - 0.001499980688) < 1.0e-5);
    assert(std::abs(lPier.end.x - 7.638461112976) < 1.0e-5);
    assert(std::abs(lPier.end.y - 2.998500108719) < 1.0e-5);
#else
    assert(std::abs(lPier.start.x - 7.838461875916) < 1.0e-5);
    assert(std::abs(lPier.start.y - 0.001499980688) < 1.0e-5);
    assert(std::abs(lPier.end.x - 7.838461875916) < 1.0e-5);
    assert(std::abs(lPier.end.y - 2.998500108719) < 1.0e-5);
#endif

    const auto cShape = jaguar::SimplePolygon::Create({
        {0.0, 0.0}, {10.0, 0.0}, {10.0, 2.0}, {3.0, 2.0},
        {3.0, 8.0}, {10.0, 8.0}, {10.0, 10.0}, {0.0, 10.0}});
    assert(cShape.HasValue());
    const auto cSurrogate = jaguar::Surrogate::Create(
        cShape.Value(), rustOracleConfig);
    assert(cSurrogate.HasValue());
    assert(cSurrogate.Value().Poles().size() == 8);
    assert(cSurrogate.Value().FailFastPoleCount() == 6);
    assert(cSurrogate.Value().Piers().size() == 2);
    assert(std::abs(cSurrogate.Value().ConvexHullArea() - 100.0) < 1.0e-9);
    assert((cSurrogate.Value().ConvexHullIndices() ==
            std::vector<std::size_t>{0U, 1U, 6U, 7U}));
    assert(std::abs(cSurrogate.Value().Poles()[0].center.x - 1.533203125) < 1.0e-6);
    assert(std::abs(cSurrogate.Value().Poles()[0].center.y - 8.466796875) < 1.0e-6);
    assert(std::abs(cSurrogate.Value().Poles()[0].radius - 1.533203125) < 1.0e-6);
    const auto& cPier = cSurrogate.Value().Piers().front();
#ifdef JAGUARCORE_TEST_RELEASE
    assert(std::abs(cPier.start.x - 9.997974395752) < 1.0e-5);
    assert(std::abs(cPier.start.y - 0.526007711887) < 1.0e-5);
    assert(std::abs(cPier.end.x - 5.950236797333) < 1.0e-5);
    assert(std::abs(cPier.end.y - 1.999262690544) < 1.0e-5);
#else
    assert(std::abs(cPier.start.x - 9.776580810547) < 1.0e-5);
    assert(std::abs(cPier.start.y - 0.000999987125) < 1.0e-5);
    assert(std::abs(cPier.end.x - 7.778581142426) < 1.0e-5);
    assert(std::abs(cPier.end.y - 1.999000072479) < 1.0e-5);
#endif

    auto cachedPolygon = polygon.Value();
    assert(cachedPolygon.GenerateSurrogate(config).HasValue());
    const jaguar::Surrogate* firstCached = cachedPolygon.GetSurrogate();
    (void)firstCached;
    assert(firstCached != nullptr);
    assert(cachedPolygon.GenerateSurrogate(config).HasValue());
    assert(cachedPolygon.GetSurrogate() == firstCached);

    const auto movedPolygon = cachedPolygon.Transformed(
        jaguar::Transform2d::FromRotationTranslation(0.0, {10.0, 20.0}));
    assert(movedPolygon.HasValue());
    assert(movedPolygon.Value().GetSurrogate() != nullptr);
    assert(std::abs(movedPolygon.Value().GetSurrogate()->Poles().front().center.x -
                    (firstCached->Poles().front().center.x + 10.0)) < 1.0e-9);
    assert(std::abs(movedPolygon.Value().GetSurrogate()->Poles().front().center.y -
                    (firstCached->Poles().front().center.y + 20.0)) < 1.0e-9);

    const auto invalidSurrogateTransform = surrogate.Value().Transformed(
        jaguar::Transform2d::FromRotationTranslation(
            std::numeric_limits<double>::quiet_NaN(), {0.0, 0.0}));
    assert(!invalidSurrogateTransform.HasValue());
    assert(invalidSurrogateTransform.GetError().code == jaguar::ErrorCode::InvalidInput);

    jaguar::SurrogateConfig changedConfig = config;
    changedConfig.failFastPiers = 2;
    assert(cachedPolygon.GenerateSurrogate(changedConfig).HasValue());
    assert(cachedPolygon.GetSurrogate() != firstCached);

    const auto invalid = jaguar::Surrogate::Create(
        polygon.Value(), jaguar::SurrogateConfig{{{{1, 0.0}, {0, 0.0}, {0, 0.0}}}, 1.1, 0});
    assert(!invalid.HasValue());
    assert(invalid.GetError().code == jaguar::ErrorCode::InvalidInput);

    jaguar::SurrogateConfig randomConfig{};
    randomConfig.poleLimits = {{{4, 0.0}, {2, 0.75}, {1, 0.9}}};
    randomConfig.failFastPoleAreaRatio = 0.5;
    randomConfig.failFastPiers = 2;
    std::mt19937_64 random(0x535552524F474154ULL);
    std::uniform_real_distribution<double> radius(2.0, 5.0);
    const double pi = std::acos(-1.0);
    for (std::size_t caseIndex = 0; caseIndex < 8; ++caseIndex)
    {
        const std::size_t vertexCount = 6 + random() % 7;
        std::vector<jaguar::Point2d> points;
        points.reserve(vertexCount);
        for (std::size_t vertex = 0; vertex < vertexCount; ++vertex)
        {
            const double angle = 2.0 * pi * static_cast<double>(vertex) /
                                 static_cast<double>(vertexCount);
            const double distance = radius(random);
            points.push_back({distance * std::cos(angle),
                              distance * std::sin(angle)});
        }
        const auto randomShape = jaguar::SimplePolygon::Create(std::move(points));
        assert(randomShape.HasValue());

        const auto inflated = jaguar::OffsetShape(
            randomShape.Value(), jaguar::ShapeModifyMode::Inflate, 0.05);
        assert(inflated.HasValue());
        assert(jaguar::ShapeModificationValid(
            randomShape.Value(), inflated.Value(),
            jaguar::ShapeModifyMode::Inflate));

        const auto deflated = jaguar::OffsetShapes(
            randomShape.Value(), jaguar::ShapeModifyMode::Deflate, 0.05);
        assert(deflated.HasValue());
        assert(!deflated.Value().empty());
        for (const auto& component : deflated.Value())
        {
            assert(jaguar::ShapeModificationValid(
                randomShape.Value(), component,
                jaguar::ShapeModifyMode::Deflate));
        }

        const auto randomSurrogate = jaguar::Surrogate::Create(
            randomShape.Value(), randomConfig);
        assert(randomSurrogate.HasValue());
        for (const auto& pole : randomSurrogate.Value().Poles())
            assert(randomShape.Value().Contains(pole.center));
        for (const auto& pier : randomSurrogate.Value().Piers())
        {
            assert(randomShape.Value().Contains(pier.start));
            assert(randomShape.Value().Contains(pier.end));
        }
    }
}
