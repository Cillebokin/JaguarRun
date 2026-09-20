#include "JaguarCore/fail_fast/surrogate.h"
#include "JaguarCore/geometry/shape_modification.h"

#include <cassert>
#include <cmath>
#include <random>

void runSurrogateTests()
{
    const auto polygon = jaguar::SimplePolygon::create({
        {0.0, 0.0}, {0.0, 4.0}, {10.0, 4.0}, {10.0, 0.0}});
    assert(polygon.hasValue());

    jaguar::SurrogateConfig config{};
    config.poleLimits = {{{8, 0.0}, {4, 0.75}, {2, 0.9}}};
    config.failFastPoleAreaRatio = 0.5;
    config.failFastPiers = 1;

    const auto surrogate = jaguar::Surrogate::create(polygon.value(), config);
    assert(surrogate.hasValue());
    assert(!surrogate.value().poles().empty());
    assert(surrogate.value().failFastPoleCount() > 0);
    assert(surrogate.value().piers().size() == 1);
    assert(std::abs(surrogate.value().convexHullArea() - 40.0) < 1.0e-9);

    for (const auto& pier : surrogate.value().piers())
    {
        assert(polygon.value().contains(pier.start));
        assert(polygon.value().contains(pier.end));
        assert(pier.length() < polygon.value().boundingBox().width());
        const jaguar::Point2d midpoint{
            (pier.start.x + pier.end.x) * 0.5,
            (pier.start.y + pier.end.y) * 0.5};
        assert(polygon.value().contains(midpoint));
    }

    for (const auto& pole : surrogate.value().poles())
    {
        assert(polygon.value().contains(pole.center));
        assert(pole.radius > 0.0);
        (void)pole;
    }

    const auto concave = jaguar::SimplePolygon::create({
        {0.0, 0.0}, {4.0, 0.0}, {4.0, 4.0}, {2.0, 2.0}, {0.0, 4.0}});
    assert(concave.hasValue());
    const auto concaveSurrogate = jaguar::Surrogate::create(concave.value(), config);
    assert(concaveSurrogate.hasValue());
    assert(!concaveSurrogate.value().poles().empty());

    jaguar::SurrogateConfig thresholdConfig{};
    thresholdConfig.poleLimits = {{{2, 0.9}, {0, 0.0}, {0, 0.0}}};
    const auto thresholdSurrogate = jaguar::Surrogate::create(
        polygon.value(), thresholdConfig);
    assert(thresholdSurrogate.hasValue());
    assert(thresholdSurrogate.value().poles().size() >= 2);

    const auto uShape = jaguar::SimplePolygon::create({
        {0.0, 0.0}, {10.0, 0.0}, {10.0, 10.0}, {6.0, 10.0},
        {6.0, 4.0}, {4.0, 4.0}, {4.0, 10.0}, {0.0, 10.0}});
    assert(uShape.hasValue());
    jaguar::SurrogateConfig uConfig{};
    uConfig.poleLimits = {{{1, 0.0}, {0, 0.0}, {0, 0.0}}};
    uConfig.failFastPiers = 1;
    const auto uSurrogate = jaguar::Surrogate::create(uShape.value(), uConfig);
    assert(uSurrogate.hasValue());
    assert(uSurrogate.value().piers().size() == 1);
    assert(uSurrogate.value().piers().front().length() > 8.0);

    // Rust jagua-rs 0.9.0 oracle: the same U shape with the configured
    // largest-remaining-first pole limits and 50% fail-fast coverage.
    jaguar::SurrogateConfig rustOracleConfig{};
    rustOracleConfig.poleLimits = {{{8, 0.0}, {4, 0.75}, {2, 0.9}}};
    rustOracleConfig.failFastPoleAreaRatio = 0.5;
    rustOracleConfig.failFastPiers = 2;
    const auto rustOracleSurrogate = jaguar::Surrogate::create(
        uShape.value(), rustOracleConfig);
    assert(rustOracleSurrogate.hasValue());
    assert(rustOracleSurrogate.value().poles().size() == 7);
    assert(rustOracleSurrogate.value().failFastPoleCount() == 3);
    assert(rustOracleSurrogate.value().piers().size() == 2);
    assert(std::abs(rustOracleSurrogate.value().convexHullArea() - 100.0) <
           1.0e-9);
    const auto& rustPole = rustOracleSurrogate.value().poles().front();
    assert(std::abs(rustPole.center.x - 2.34375) < 1.0e-5);
    assert(std::abs(rustPole.center.y - 2.34375) < 1.0e-5);
    assert(std::abs(rustPole.radius - 2.342291116714) < 1.0e-5);
    const auto& rustPier = rustOracleSurrogate.value().piers().front();
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

    const auto lShape = jaguar::SimplePolygon::create({
        {0.0, 0.0}, {8.0, 0.0}, {8.0, 3.0},
        {3.0, 3.0}, {3.0, 8.0}, {0.0, 8.0}});
    assert(lShape.hasValue());
    const auto lSurrogate = jaguar::Surrogate::create(
        lShape.value(), rustOracleConfig);
    assert(lSurrogate.hasValue());
    assert(lSurrogate.value().poles().size() == 7);
    assert(lSurrogate.value().failFastPoleCount() == 3);
    assert(lSurrogate.value().piers().size() == 2);
    assert(std::abs(lSurrogate.value().convexHullArea() - 51.5) < 1.0e-9);
    assert(std::abs(lSurrogate.value().poles()[0].center.x - 1.7578125) < 1.0e-6);
    assert(std::abs(lSurrogate.value().poles()[0].center.y - 1.7578125) < 1.0e-6);
    assert(std::abs(lSurrogate.value().poles()[0].radius - 1.756718397141) < 1.0e-6);
    const auto& lPier = lSurrogate.value().piers().front();
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

    const auto cShape = jaguar::SimplePolygon::create({
        {0.0, 0.0}, {10.0, 0.0}, {10.0, 2.0}, {3.0, 2.0},
        {3.0, 8.0}, {10.0, 8.0}, {10.0, 10.0}, {0.0, 10.0}});
    assert(cShape.hasValue());
    const auto cSurrogate = jaguar::Surrogate::create(
        cShape.value(), rustOracleConfig);
    assert(cSurrogate.hasValue());
    assert(cSurrogate.value().poles().size() == 8);
    assert(cSurrogate.value().failFastPoleCount() == 6);
    assert(cSurrogate.value().piers().size() == 2);
    assert(std::abs(cSurrogate.value().convexHullArea() - 100.0) < 1.0e-9);
    assert(std::abs(cSurrogate.value().poles()[0].center.x - 1.533203125) < 1.0e-6);
    assert(std::abs(cSurrogate.value().poles()[0].center.y - 8.466796875) < 1.0e-6);
    assert(std::abs(cSurrogate.value().poles()[0].radius - 1.533203125) < 1.0e-6);
    const auto& cPier = cSurrogate.value().piers().front();
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

    auto cachedPolygon = polygon.value();
    assert(cachedPolygon.generateSurrogate(config).hasValue());
    const jaguar::Surrogate* firstCached = cachedPolygon.surrogate();
    (void)firstCached;
    assert(firstCached != nullptr);
    assert(cachedPolygon.generateSurrogate(config).hasValue());
    assert(cachedPolygon.surrogate() == firstCached);

    const auto movedPolygon = cachedPolygon.transformed(
        jaguar::Transform2d::fromRotationTranslation(0.0, {10.0, 20.0}));
    assert(movedPolygon.surrogate() != nullptr);
    assert(std::abs(movedPolygon.surrogate()->poles().front().center.x -
                    (firstCached->poles().front().center.x + 10.0)) < 1.0e-9);
    assert(std::abs(movedPolygon.surrogate()->poles().front().center.y -
                    (firstCached->poles().front().center.y + 20.0)) < 1.0e-9);

    jaguar::SurrogateConfig changedConfig = config;
    changedConfig.failFastPiers = 2;
    assert(cachedPolygon.generateSurrogate(changedConfig).hasValue());
    assert(cachedPolygon.surrogate() != firstCached);

    const auto invalid = jaguar::Surrogate::create(
        polygon.value(), jaguar::SurrogateConfig{{{{1, 0.0}, {0, 0.0}, {0, 0.0}}}, 1.1, 0});
    assert(!invalid.hasValue());
    assert(invalid.error().code == jaguar::ErrorCode::InvalidInput);

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
        const auto randomShape = jaguar::SimplePolygon::create(std::move(points));
        assert(randomShape.hasValue());

        const auto inflated = jaguar::offsetShape(
            randomShape.value(), jaguar::ShapeModifyMode::Inflate, 0.05);
        assert(inflated.hasValue());
        assert(jaguar::shapeModificationValid(
            randomShape.value(), inflated.value(),
            jaguar::ShapeModifyMode::Inflate));

        const auto deflated = jaguar::offsetShapes(
            randomShape.value(), jaguar::ShapeModifyMode::Deflate, 0.05);
        assert(deflated.hasValue());
        assert(!deflated.value().empty());
        for (const auto& component : deflated.value())
        {
            assert(jaguar::shapeModificationValid(
                randomShape.value(), component,
                jaguar::ShapeModifyMode::Deflate));
        }

        const auto randomSurrogate = jaguar::Surrogate::create(
            randomShape.value(), randomConfig);
        assert(randomSurrogate.hasValue());
        for (const auto& pole : randomSurrogate.value().poles())
            assert(randomShape.value().contains(pole.center));
        for (const auto& pier : randomSurrogate.value().piers())
        {
            assert(randomShape.value().contains(pier.start));
            assert(randomShape.value().contains(pier.end));
        }
    }
}
