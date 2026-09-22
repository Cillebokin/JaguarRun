#include "JaguarCore/Version.h"
#include "JaguarCore/Geometry/Point.h"
#include "JaguarCore/Geometry/Rect.h"
#include "JaguarCore/NumericPolicy.h"
#include "JaguarCore/Result.h"

#include <cassert>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <string>

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
void runBoundaryValidationTests();

namespace
{
void testVersion()
{
    const char* version = jaguar::Version();
    assert(version != nullptr);
    assert(version[0] != '\0');
    const char* stableVersion = JaguarCoreVersion();
    assert(stableVersion != nullptr);
    assert(std::string(stableVersion) == version);
    assert(JaguarCoreApiVersion() == 2U);
    (void)version;
}

void testResult()
{
    const auto success = jaguar::Result<int>::FromValue(42);
    assert(success.HasValue());
    assert(success.Value() == 42);

    const auto failure = jaguar::Result<int>::FromError(
        {jaguar::ErrorCode::InvalidInput, "invalid"});
    assert(!failure.HasValue());
    assert(failure.GetError().code == jaguar::ErrorCode::InvalidInput);
}

void testNumericPolicy()
{
    const jaguar::NumericPolicy policy{};
    assert(policy.equalityTolerance ==
           jaguar::NumericPolicy::DefaultEqualityTolerance());
    assert(policy.geometryTolerance ==
           jaguar::NumericPolicy::DefaultGeometryTolerance());
    assert(policy.areaTolerance ==
           jaguar::NumericPolicy::DefaultAreaTolerance());
    assert(policy.arcChordTolerance ==
           jaguar::NumericPolicy::DefaultArcChordTolerance());
    assert(policy.IsFinite(1.0));
    assert(!policy.IsFinite(std::numeric_limits<double>::infinity()));
    assert(policy.IsValid());
    const jaguar::NumericPolicy nanPolicy{
        std::numeric_limits<double>::quiet_NaN(),
        policy.geometryTolerance,
        policy.areaTolerance,
        policy.arcChordTolerance};
    assert(!nanPolicy.IsValid());
    const jaguar::NumericPolicy negativePolicy{
        policy.equalityTolerance,
        -1.0,
        policy.areaTolerance,
        policy.arcChordTolerance};
    assert(!negativePolicy.IsValid());
    assert(policy.NearlyEqual(1.0, 1.0 + 0.5e-9));
    assert(!policy.NearlyEqual(1.0, 1.0 + 1.0e-3));
}

void testPoint()
{
    const jaguar::Point2d first{0.0, 0.0};
    const jaguar::Point2d second{3.0, 4.0};
    assert(first.IsFinite());
    assert(std::abs(first.SquaredDistanceTo(second) - 25.0) < 1.0e-12);
}

void testRect()
{
    const auto created = jaguar::Rect2d::Create(0.0, 0.0, 10.0, 20.0);
    assert(created.HasValue());
    assert(std::abs(created.Value().Area() - 200.0) < 1.0e-12);
    assert(created.Value().Contains({5.0, 10.0}));

    const auto inner = jaguar::Rect2d::Create(2.0, 3.0, 8.0, 12.0);
    assert(inner.HasValue());
    assert(created.Value().RelationTo(inner.Value()) == jaguar::RectRelation::Surrounding);
    assert(inner.Value().RelationTo(created.Value()) == jaguar::RectRelation::Enclosed);

    const auto crossing = jaguar::Rect2d::Create(5.0, 10.0, 15.0, 30.0);
    assert(crossing.HasValue());
    const auto overlap = created.Value().Intersection(crossing.Value());
    assert(overlap.has_value());
    assert(std::abs(overlap->Area() - 50.0) < 1.0e-12);

    const auto inflated = created.Value().Inflated(1.0, 2.0);
    assert(std::abs(inflated.Width() - 12.0) < 1.0e-12);
    assert(std::abs(inflated.Height() - 24.0) < 1.0e-12);

    const auto invalid = jaguar::Rect2d::Create(10.0, 0.0, 0.0, 20.0);
    assert(!invalid.HasValue());
    assert(invalid.GetError().code == jaguar::ErrorCode::InvalidInput);
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
    runBoundaryValidationTests();
    return 0;
}
