#include "JaguarCore/version.h"
#include "JaguarCore/geometry/point.h"
#include "JaguarCore/geometry/rect.h"
#include "JaguarCore/numeric_policy.h"
#include "JaguarCore/result.h"

#include <cassert>
#include <cmath>
#include <cstdlib>
#include <limits>

void runGeometryTests();
void runPolygonTests();
void runShapeModificationTests();
void runSurrogateTests();
void runCollisionTests();
void runLayoutTests();
void runIoTests();
void runJsonCodecTests();
void runSvgExportTests();
void runSoaTests();
void runGeometryDifferentialTests();
void runProblemDifferentialTests();
void runCollisionDifferentialTests();
void runEdgeMatrixDifferentialTests();
void runContainmentMatrixDifferentialTests();
void runTrajectoryDifferentialTests();
void runOptimizerDifferentialTests();
void runSppTests();
void runBppTests();
void runMsppTests();
void runProblemRandomStateTests();
void runBoundaryDifferentialTests();

namespace
{
void testVersion()
{
    const char* version = jaguar::version();
    assert(version != nullptr);
    assert(version[0] != '\0');
    (void)version;
}

void testResult()
{
    const auto success = jaguar::Result<int>::fromValue(42);
    assert(success.hasValue());
    assert(success.value() == 42);

    const auto failure = jaguar::Result<int>::fromError(
        {jaguar::ErrorCode::InvalidInput, "invalid"});
    assert(!failure.hasValue());
    assert(failure.error().code == jaguar::ErrorCode::InvalidInput);
}

void testNumericPolicy()
{
    const jaguar::NumericPolicy policy{};
    assert(policy.isFinite(1.0));
    assert(!policy.isFinite(std::numeric_limits<double>::infinity()));
    assert(policy.nearlyEqual(1.0, 1.0 + 0.5e-9));
    assert(!policy.nearlyEqual(1.0, 1.0 + 1.0e-3));
}

void testPoint()
{
    const jaguar::Point2d first{0.0, 0.0};
    const jaguar::Point2d second{3.0, 4.0};
    assert(first.isFinite());
    assert(std::abs(first.squaredDistanceTo(second) - 25.0) < 1.0e-12);
}

void testRect()
{
    const auto created = jaguar::Rect2d::create(0.0, 0.0, 10.0, 20.0);
    assert(created.hasValue());
    assert(std::abs(created.value().area() - 200.0) < 1.0e-12);
    assert(created.value().contains({5.0, 10.0}));

    const auto inner = jaguar::Rect2d::create(2.0, 3.0, 8.0, 12.0);
    assert(inner.hasValue());
    assert(created.value().relationTo(inner.value()) == jaguar::RectRelation::Surrounding);
    assert(inner.value().relationTo(created.value()) == jaguar::RectRelation::Enclosed);

    const auto crossing = jaguar::Rect2d::create(5.0, 10.0, 15.0, 30.0);
    assert(crossing.hasValue());
    const auto overlap = created.value().intersection(crossing.value());
    assert(overlap.has_value());
    assert(std::abs(overlap->area() - 50.0) < 1.0e-12);

    const auto inflated = created.value().inflated(1.0, 2.0);
    assert(std::abs(inflated.width() - 12.0) < 1.0e-12);
    assert(std::abs(inflated.height() - 24.0) < 1.0e-12);

    const auto invalid = jaguar::Rect2d::create(10.0, 0.0, 0.0, 20.0);
    assert(!invalid.hasValue());
    assert(invalid.error().code == jaguar::ErrorCode::InvalidInput);
}
}

int main()
{
    testVersion();
    testResult();
    testNumericPolicy();
    testPoint();
    testRect();
    runGeometryTests();
    runPolygonTests();
    runShapeModificationTests();
    runSurrogateTests();
    runCollisionTests();
    runLayoutTests();
    runIoTests();
    runJsonCodecTests();
    runSvgExportTests();
    runSoaTests();
    runGeometryDifferentialTests();
    runProblemDifferentialTests();
    runCollisionDifferentialTests();
    runEdgeMatrixDifferentialTests();
    runContainmentMatrixDifferentialTests();
    runTrajectoryDifferentialTests();
    runOptimizerDifferentialTests();
    runSppTests();
    runBppTests();
    runMsppTests();
    runProblemRandomStateTests();
    runBoundaryDifferentialTests();
    return 0;
}
