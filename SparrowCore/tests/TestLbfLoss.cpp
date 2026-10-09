#include "SparrowCore/Optimizer/LbfLoss.h"
#include "SparrowCore/Optimizer/SampleEval.h"
#include "SparrowCore/Sampling/BestSamples.h"

#include "JaguarCore/Geometry/Rect.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <ostream>
#include <stdexcept>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace
{
bool Expect(bool condition, std::ostream& output, const char* message)
{
    if (condition)
    {
        return true;
    }

    output << "LBF LOSS TEST FAILURE: " << message << '\n';
    return false;
}

bool TestLbfLossValues(std::ostream& output)
{
    const jaguar::Rect2d bbox{1.25, 2.5, 8.75, 6.5};
    const sparrow::LbfLoss loss = sparrow::LbfLoss::FromBoundingBox(bbox);
    if (!Expect(loss.xMax == static_cast<float>(8.75)
                    && loss.yMax == static_cast<float>(6.5),
                output,
                "LBF loss must preserve float bbox maxima"))
    {
        return false;
    }
    if (!Expect(loss.Cost() == 94.0F,
                output,
                "LBF loss cost must be 10*x_max+y_max"))
    {
        return false;
    }

    const jaguar::Rect2d wideSample{0.0, 0.0, 20.0, 10.0};
    const jaguar::Rect2d tightened = loss.TightenSampleBBox(wideSample);
    const double expectedTightenedXMax = static_cast<double>(static_cast<float>(9.4));
    if (!Expect(tightened.xMin == 0.0 && tightened.yMin == 0.0
                    && tightened.xMax == expectedTightenedXMax
                    && tightened.yMax == 10.0,
                output,
                "LBF loss must tighten sample x_max using the weighted cost"))
    {
        return false;
    }

    const jaguar::Rect2d alreadyTight{0.0, 0.0, 5.0, 10.0};
    const jaguar::Rect2d unchanged = loss.TightenSampleBBox(alreadyTight);
    if (!Expect(unchanged.xMax == 5.0 && unchanged.yMax == 10.0,
                output,
                "LBF loss must not widen an existing sample bbox"))
    {
        return false;
    }

    const sparrow::LbfLoss floatLoss =
        sparrow::LbfLoss::FromBoundingBox({0.0, 0.0, 0.123456789, 0.987654321});
    if (!Expect(floatLoss.xMax == static_cast<float>(0.123456789)
                    && floatLoss.yMax == static_cast<float>(0.987654321),
                output,
                "LBF loss must use float decision precision"))
    {
        return false;
    }

    const sparrow::LbfLoss equalLeft{1.0F, 2.0F};
    const sparrow::LbfLoss equalRight{0.0F, 12.0F};
    if (!Expect(!(equalLeft < equalRight) && !(equalRight < equalLeft),
                output,
                "equal LBF costs must compare equal"))
    {
        return false;
    }

    const sparrow::LbfLoss nanLoss{
        std::numeric_limits<float>::quiet_NaN(), 1.0F};
    const sparrow::LbfLoss finiteLoss{1.0F, 1.0F};
    if (!Expect(!(nanLoss < finiteLoss) && finiteLoss < nanLoss,
                output,
                "NaN LBF loss must never be preferred over finite loss"))
    {
        return false;
    }
    return Expect(std::isinf(nanLoss.Cost()),
                  output,
                  "NaN LBF loss cost must be treated as non-finite");
}

bool TestSampleEvalOrdering(std::ostream& output)
{
    const sparrow::SampleEval clearLow{sparrow::SampleKind::SAMPLE_KIND_CLEAR, 1.0F};
    const sparrow::SampleEval clearHigh{sparrow::SampleKind::SAMPLE_KIND_CLEAR, 2.0F};
    const sparrow::SampleEval collisionLow{sparrow::SampleKind::SAMPLE_KIND_COLLISION, 0.0F};
    const sparrow::SampleEval collisionHigh{sparrow::SampleKind::SAMPLE_KIND_COLLISION, 3.0F};
    const sparrow::SampleEval invalid{sparrow::SampleKind::SAMPLE_KIND_INVALID, 0.0F};

    if (!Expect(clearLow < clearHigh && collisionLow < collisionHigh,
                output,
                "same-kind SampleEval values must sort by loss"))
    {
        return false;
    }
    if (!Expect(clearLow < collisionLow && collisionLow < invalid,
                output,
                "SampleEval order must be Clear < Collision < Invalid"))
    {
        return false;
    }
    if (!Expect(sparrow::IsBetterSample(clearLow, collisionHigh)
                    && !sparrow::IsBetterSample(collisionHigh, clearLow),
                output,
                "betterSample must use the three-state ordering"))
    {
        return false;
    }

    float withinRustUlps = 1.0F;
    for (int step = 0; step < 4; ++step)
    {
        withinRustUlps = std::nextafter(
            withinRustUlps, std::numeric_limits<float>::infinity());
    }
    const sparrow::SampleEval lower{sparrow::SampleKind::SAMPLE_KIND_CLEAR, 1.0F};
    const sparrow::SampleEval withinUlps{
        sparrow::SampleKind::SAMPLE_KIND_CLEAR, withinRustUlps};
    if (!Expect(!sparrow::IsBetterSample(lower, withinUlps)
                    && !sparrow::IsBetterSample(withinUlps, lower),
                output,
                "SampleEval must treat Rust's default four-ULP margin as equal"))
    {
        return false;
    }

    const float outsideRustUlps = std::nextafter(
        withinRustUlps, std::numeric_limits<float>::infinity());
    const sparrow::SampleEval outsideUlps{
        sparrow::SampleKind::SAMPLE_KIND_CLEAR, outsideRustUlps};
    if (!Expect(sparrow::IsBetterSample(lower, outsideUlps)
                    && !sparrow::IsBetterSample(outsideUlps, lower),
                output,
                "SampleEval must order values beyond Rust's four-ULP margin"))
    {
        return false;
    }

    const sparrow::SampleEval nanEval{
        sparrow::SampleKind::SAMPLE_KIND_CLEAR,
        std::numeric_limits<float>::quiet_NaN()};
    bool nanOrderingThrows = false;
    try
    {
        static_cast<void>(sparrow::IsBetterSample(nanEval, clearLow));
    }
    catch (const std::domain_error&)
    {
        nanOrderingThrows = true;
    }
    if (!Expect(nanOrderingThrows && !(nanEval == nanEval),
                output,
                "NaN SampleEval ordering must fail and derived equality must be false"))
    {
        return false;
    }
    return Expect(nanEval < invalid,
                  output,
                  "A non-invalid NaN sample must remain better than Invalid");
}

bool TestBestSamples(std::ostream& output)
{
    sparrow::BestSamples empty(0U, 0.5F);
#if defined(NDEBUG)
    if (!Expect(empty.Report(jaguar::Transform2d::Identity(),
                            {sparrow::SampleKind::SAMPLE_KIND_CLEAR, 1.0F})
                    && empty.Size() == 1U
                    && empty.Best().has_value()
                    && empty.UpperBound().kind == sparrow::SampleKind::SAMPLE_KIND_INVALID
                    && empty.Report(
                        jaguar::Transform2d::FromRotationTranslation(0.0, {10.0, 0.0}),
                        {sparrow::SampleKind::SAMPLE_KIND_CLEAR, 2.0F})
                    && empty.Size() == 2U,
                output,
                "Release zero-capacity BestSamples must reproduce Rust's unbounded behavior"))
    {
        return false;
    }
#else
    bool zeroCapacityReportThrows = false;
    bool zeroCapacityUpperBoundThrows = false;
    try
    {
        static_cast<void>(empty.Report(
            jaguar::Transform2d::Identity(),
            {sparrow::SampleKind::SAMPLE_KIND_CLEAR, 1.0F}));
    }
    catch (const std::underflow_error&)
    {
        zeroCapacityReportThrows = true;
    }
    try
    {
        static_cast<void>(empty.UpperBound());
    }
    catch (const std::underflow_error&)
    {
        zeroCapacityUpperBoundThrows = true;
    }
    if (!Expect(zeroCapacityReportThrows
                    && zeroCapacityUpperBoundThrows
                    && empty.Size() == 0U
                    && !empty.Best().has_value(),
                output,
                "Debug zero-capacity BestSamples must report Rust's size - 1 underflow"))
    {
        return false;
    }
#endif

    sparrow::BestSamples samples(2U, 0.5F);
    const auto firstTransform =
        jaguar::Transform2d::FromRotationTranslation(0.0, {0.0, 0.0});
    const auto secondTransform =
        jaguar::Transform2d::FromRotationTranslation(0.0, {10.0, 0.0});
    const auto thirdTransform =
        jaguar::Transform2d::FromRotationTranslation(0.0, {20.0, 0.0});
    if (!Expect(samples.Report(firstTransform, {sparrow::SampleKind::SAMPLE_KIND_CLEAR, 5.0F})
                    && samples.Report(secondTransform,
                                      {sparrow::SampleKind::SAMPLE_KIND_CLEAR, 3.0F})
                    && samples.Size() == 2U,
                output,
                "BestSamples must accept samples until capacity"))
    {
        return false;
    }
    if (!Expect(samples.UpperBound().loss == 5.0F,
                output,
                "BestSamples upper bound must be the current worst evaluation"))
    {
        return false;
    }
    if (!Expect(samples.Report(thirdTransform, {sparrow::SampleKind::SAMPLE_KIND_CLEAR, 4.0F})
                    && samples.Size() == 2U
                    && samples.UpperBound().loss == 4.0F,
                output,
                "A sample below the full-set upper bound must be accepted"))
    {
        return false;
    }
    if (!Expect(samples.Report(thirdTransform, {sparrow::SampleKind::SAMPLE_KIND_CLEAR, 2.0F})
                    && samples.Size() == 2U,
                output,
                "A better sample must evict the worst sample"))
    {
        return false;
    }
    if (!Expect(samples.Best().has_value()
                    && samples.Best()->second.loss == 2.0F
                    && samples.UpperBound().loss == 3.0F,
                output,
                "BestSamples must stay sorted and expose best/upper bound"))
    {
        return false;
    }

    const auto similarTransform =
        jaguar::Transform2d::FromRotationTranslation(0.01,
                                                     {20.1, 0.1});
    if (!Expect(!samples.Report(similarTransform,
                                {sparrow::SampleKind::SAMPLE_KIND_CLEAR, 2.5F}),
                output,
                "A similar sample with a worse loss must be rejected"))
    {
        return false;
    }
    if (!Expect(samples.Report(similarTransform,
                               {sparrow::SampleKind::SAMPLE_KIND_CLEAR, 1.5F})
                    && samples.Size() == 2U,
                output,
                "A better similar sample must replace the old sample"))
    {
        return false;
    }

    const sparrow::SampleEval nanEval{
        sparrow::SampleKind::SAMPLE_KIND_CLEAR,
        std::numeric_limits<float>::quiet_NaN()};
    bool nanReportThrows = false;
    try
    {
        static_cast<void>(samples.Report(
            jaguar::Transform2d::FromRotationTranslation(0.0, {100.0, 100.0}),
            nanEval));
    }
    catch (const std::domain_error&)
    {
        nanReportThrows = true;
    }
    if (!Expect(nanReportThrows,
                output,
                "BestSamples must propagate Rust's invalid NaN comparison failure"))
    {
        return false;
    }
    return Expect(!samples.Report(
                       jaguar::Transform2d::FromRotationTranslation(0.0, {100.0, 100.0}),
                       {sparrow::SampleKind::SAMPLE_KIND_INVALID, 0.0F}),
                  output,
                  "BestSamples must reject Invalid evaluations");
}

bool TestBestSamplesBoundaryMatchesRustReference(std::ostream& output)
{
    std::ifstream referenceFile;
    for (const char* path : {
             "SparrowCore/Tests/Fixtures/BestSamplesBoundaryRustReference.tsv",
             "../SparrowCore/Tests/Fixtures/BestSamplesBoundaryRustReference.tsv",
             "../JaguarRun/SparrowCore/Tests/Fixtures/BestSamplesBoundaryRustReference.tsv",
             "..\\SparrowCore\\Tests\\Fixtures\\BestSamplesBoundaryRustReference.tsv"})
    {
        referenceFile.open(path);
        if (referenceFile.is_open())
        {
            break;
        }
        referenceFile.clear();
    }
    if (!Expect(referenceFile.is_open(),
                output,
                "BestSamples boundary Rust fixture cannot be opened"))
    {
        return false;
    }

    struct Operation
    {
        double rotation;
        double x;
        double y;
        float loss;
    };
    struct BoundaryCase
    {
        const char* name;
        std::size_t capacity;
        float threshold;
        std::vector<Operation> operations;
    };

    const float translationThreshold = 0.05F;
    const float translationBelow = std::nextafter(translationThreshold, 0.0F);
    const float rotationThreshold = 0.017453292F;
    const float rotationBelow = std::nextafter(rotationThreshold, 0.0F);
    const auto operation = [](double x, double y, double rotation, float loss) {
        return Operation{rotation, x, y, loss};
    };
    const std::vector<BoundaryCase> cases = {
        {"translation_f32_rounding",
         3U,
         translationThreshold,
         {operation(1.0, 0.0, 0.0, 1.0F),
          operation(1.05000001, 0.0, 0.0, 2.0F)}},
        {"translation_threshold_positive_infinity",
         3U,
         std::numeric_limits<float>::infinity(),
         {operation(0.0, 0.0, 0.0, 1.0F),
          operation(10.0, 0.0, 0.0, 2.0F)}},
        {"translation_threshold_nan",
         3U,
         std::numeric_limits<float>::quiet_NaN(),
         {operation(0.0, 0.0, 0.0, 1.0F),
          operation(0.0, 0.0, 0.0, 2.0F)}},
        {"translation_threshold_negative_infinity",
         3U,
         -std::numeric_limits<float>::infinity(),
         {operation(0.0, 0.0, 0.0, 1.0F),
          operation(0.0, 0.0, 0.0, 2.0F)}},
        {"translation_x_exact",
         3U,
         translationThreshold,
         {operation(0.0, 0.0, 0.0, 1.0F),
          operation(static_cast<double>(translationThreshold), 0.0, 0.0, 2.0F)}},
        {"translation_x_below",
         3U,
         translationThreshold,
         {operation(0.0, 0.0, 0.0, 1.0F),
          operation(static_cast<double>(translationBelow), 0.0, 0.0, 2.0F)}},
        {"translation_y_exact",
         3U,
         translationThreshold,
         {operation(0.0, 0.0, 0.0, 1.0F),
          operation(0.0, static_cast<double>(translationThreshold), 0.0, 2.0F)}},
        {"translation_y_below",
         3U,
         translationThreshold,
         {operation(0.0, 0.0, 0.0, 1.0F),
          operation(0.0, static_cast<double>(translationBelow), 0.0, 2.0F)}},
        {"rotation_one_degree_exact",
         3U,
         translationThreshold,
         {operation(0.0, 0.0, 0.0, 1.0F),
          operation(0.0, 0.0, static_cast<double>(rotationThreshold), 2.0F)}},
        {"rotation_one_degree_below",
         3U,
         translationThreshold,
         {operation(0.0, 0.0, 0.0, 1.0F),
          operation(0.0, 0.0, static_cast<double>(rotationBelow), 2.0F)}},
        {"capacity_three",
         3U,
         0.5F,
         {operation(0.0, 0.0, 0.0, 5.0F),
          operation(10.0, 0.0, 0.0, 3.0F),
          operation(20.0, 0.0, 0.0, 4.0F),
          operation(30.0, 0.0, 0.0, 5.0F),
          operation(40.0, 0.0, 0.0, 6.0F),
          operation(50.0, 0.0, 0.0, 4.5F),
          operation(60.0, 0.0, 0.0, 2.0F)}},
        {"capacity_one",
         1U,
         0.5F,
         {operation(0.0, 0.0, 0.0, 5.0F),
          operation(10.0, 0.0, 0.0, 4.0F),
          operation(20.0, 0.0, 0.0, 6.0F)}}};

    const auto encodeLoss = [](float loss) {
        std::uint32_t bits = 0U;
        std::memcpy(&bits, &loss, sizeof(bits));
        std::ostringstream encoded;
        encoded << std::hex << std::setw(8) << std::setfill('0') << bits;
        return encoded.str();
    };

    std::vector<std::string> actualRows;
    for (const BoundaryCase& boundaryCase : cases)
    {
        sparrow::BestSamples samples(boundaryCase.capacity, boundaryCase.threshold);
        for (std::size_t index = 0U; index < boundaryCase.operations.size(); ++index)
        {
            const Operation& input = boundaryCase.operations[index];
            const bool accepted = samples.Report(
                jaguar::Transform2d::FromRotationTranslation(
                    input.rotation, {input.x, input.y}),
                {sparrow::SampleKind::SAMPLE_KIND_CLEAR, input.loss});

            std::string retained;
            for (const auto& sample : samples.Samples())
            {
                if (!retained.empty())
                {
                    retained += ',';
                }
                retained += encodeLoss(sample.second.loss);
            }
            if (retained.empty())
            {
                retained = "-";
            }
            const sparrow::SampleEval upperBound = samples.UpperBound();
            const std::string upper =
                upperBound.kind == sparrow::SampleKind::SAMPLE_KIND_INVALID
                    ? "invalid"
                    : encodeLoss(upperBound.loss);
            actualRows.push_back(
                std::string(boundaryCase.name) + '\t'
                + std::to_string(index + 1U) + '\t'
                + (accepted ? "1" : "0") + '\t'
                + std::to_string(samples.Size()) + '\t'
                + retained + '\t' + upper);
        }
    }

    std::vector<std::string> expectedRows;
    bool headerSeen = false;
    std::string line;
    while (std::getline(referenceFile, line))
    {
        if (line.empty() || line.front() == '#')
        {
            continue;
        }
        if (!headerSeen)
        {
            if (!Expect(line == "case\tstep\taccepted\tsize\tlosses_best_to_worst\tupper_bound",
                        output,
                        "malformed BestSamples boundary fixture header"))
            {
                return false;
            }
            headerSeen = true;
            continue;
        }
        expectedRows.push_back(line);
    }

    if (actualRows != expectedRows)
    {
        const std::size_t comparedRows = std::min(actualRows.size(), expectedRows.size());
        for (std::size_t index = 0U; index < comparedRows; ++index)
        {
            if (actualRows[index] != expectedRows[index])
            {
                output << "BestSamples boundary row " << index + 1U
                       << " expected: " << expectedRows[index]
                       << " actual: " << actualRows[index] << '\n';
                break;
            }
        }
        return Expect(false,
                      output,
                      "C++ BestSamples boundary trace differs from Rust");
    }
    return Expect(headerSeen && expectedRows.size() == actualRows.size(),
                  output,
                  "BestSamples boundary fixture row count differs");
}

bool TestBestSamplesSequenceAndNanFailuresMatchRustReference(std::ostream& output)
{
    std::ifstream referenceFile;
    for (const char* path : {
             "SparrowCore/Tests/Fixtures/BestSamplesSequenceRustReference.tsv",
             "../SparrowCore/Tests/Fixtures/BestSamplesSequenceRustReference.tsv",
             "../JaguarRun/SparrowCore/Tests/Fixtures/BestSamplesSequenceRustReference.tsv",
             "..\\SparrowCore\\Tests\\Fixtures\\BestSamplesSequenceRustReference.tsv"})
    {
        referenceFile.open(path);
        if (referenceFile.is_open())
        {
            break;
        }
        referenceFile.clear();
    }
    if (!Expect(referenceFile.is_open(),
                output,
                "BestSamples sequence Rust fixture cannot be opened"))
    {
        return false;
    }

    const auto decodeBits = [](const std::string& encoded, std::uint32_t& bits) {
        if (encoded.size() != 8U)
        {
            return false;
        }
        try
        {
            const unsigned long value = std::stoul(encoded, nullptr, 16);
            if (value > std::numeric_limits<std::uint32_t>::max())
            {
                return false;
            }
            bits = static_cast<std::uint32_t>(value);
            return true;
        }
        catch (const std::exception&)
        {
            return false;
        }
    };
    const auto decodeFloat = [&](const std::string& encoded, float& value) {
        std::uint32_t bits = 0U;
        if (!decodeBits(encoded, bits))
        {
            return false;
        }
        std::memcpy(&value, &bits, sizeof(value));
        return true;
    };
    const auto decodeEvaluation = [&](const std::string& encoded,
                                      sparrow::SampleEval& evaluation) {
        if (encoded == "invalid")
        {
            evaluation = {sparrow::SampleKind::SAMPLE_KIND_INVALID, 0.0F};
            return true;
        }
        const std::size_t separator = encoded.find(':');
        if (separator == std::string::npos)
        {
            return false;
        }
        const std::string_view kind(encoded.data(), separator);
        const std::string lossBits = encoded.substr(separator + 1U);
        float loss = 0.0F;
        if (!decodeFloat(lossBits, loss))
        {
            return false;
        }
        if (kind == "clear")
        {
            evaluation = {sparrow::SampleKind::SAMPLE_KIND_CLEAR, loss};
        }
        else if (kind == "collision")
        {
            evaluation = {sparrow::SampleKind::SAMPLE_KIND_COLLISION, loss};
        }
        else
        {
            return false;
        }
        return true;
    };
    const auto encodeEvaluation = [](const sparrow::SampleEval& evaluation) {
        if (evaluation.kind == sparrow::SampleKind::SAMPLE_KIND_INVALID)
        {
            return std::string("invalid");
        }
        std::uint32_t bits = 0U;
        std::memcpy(&bits, &evaluation.loss, sizeof(bits));
        std::ostringstream encoded;
        encoded << (evaluation.kind == sparrow::SampleKind::SAMPLE_KIND_CLEAR
                        ? "clear:"
                        : "collision:")
                << std::hex << std::setw(8) << std::setfill('0') << bits;
        return encoded.str();
    };

    sparrow::BestSamples samples(0U, 0.0F);
    std::string activeCase;
    std::size_t expectedStep = 1U;
    std::size_t caseCapacity = 0U;
    std::uint32_t caseThresholdBits = 0U;
    std::size_t rowCount = 0U;
    bool headerSeen = false;
    std::string line;
    while (std::getline(referenceFile, line))
    {
        if (line.empty() || line.front() == '#')
        {
            continue;
        }
        if (!headerSeen)
        {
            if (!Expect(line == "case\tstep\tcapacity\tthreshold_bits\trotation_bits\tx_bits\ty_bits\tevaluation\toutcome\tsize\tretained_best_to_worst\tupper_bound",
                        output,
                        "malformed BestSamples sequence fixture header"))
            {
                return false;
            }
            headerSeen = true;
            continue;
        }

        std::string caseName;
        std::size_t step = 0U;
        std::size_t capacity = 0U;
        std::string thresholdEncoded;
        std::string rotationEncoded;
        std::string xEncoded;
        std::string yEncoded;
        std::string evaluationEncoded;
        std::string expectedOutcome;
        std::size_t expectedSize = 0U;
        std::string expectedRetained;
        std::string expectedUpperBound;
        std::istringstream row(line);
        if (!(row >> caseName >> step >> capacity >> thresholdEncoded
                  >> rotationEncoded >> xEncoded >> yEncoded >> evaluationEncoded
                  >> expectedOutcome >> expectedSize >> expectedRetained
                  >> expectedUpperBound))
        {
            output << "Malformed BestSamples sequence fixture row: " << line << '\n';
            return false;
        }

        float threshold = 0.0F;
        float rotation = 0.0F;
        float x = 0.0F;
        float y = 0.0F;
        sparrow::SampleEval evaluation;
        if (!decodeFloat(thresholdEncoded, threshold)
            || !decodeFloat(rotationEncoded, rotation)
            || !decodeFloat(xEncoded, x)
            || !decodeFloat(yEncoded, y)
            || !decodeEvaluation(evaluationEncoded, evaluation))
        {
            output << "Invalid encoded BestSamples sequence fixture row: " << line << '\n';
            return false;
        }

        if (caseName != activeCase)
        {
            activeCase = caseName;
            expectedStep = 1U;
            caseCapacity = capacity;
            std::memcpy(&caseThresholdBits, &threshold, sizeof(caseThresholdBits));
            samples = sparrow::BestSamples(capacity, threshold);
        }
        std::uint32_t thresholdBits = 0U;
        std::memcpy(&thresholdBits, &threshold, sizeof(thresholdBits));
        if (step != expectedStep || capacity != caseCapacity
            || thresholdBits != caseThresholdBits)
        {
            output << "Inconsistent BestSamples sequence fixture at case " << caseName
                   << " step " << step << '\n';
            return false;
        }
        ++expectedStep;

        std::string actualOutcome;
        try
        {
            const bool accepted = samples.Report(
                jaguar::Transform2d::FromRotationTranslation(
                    static_cast<double>(rotation),
                    {static_cast<double>(x), static_cast<double>(y)}),
                evaluation);
            actualOutcome = accepted ? "accepted" : "rejected";
        }
        catch (const std::domain_error&)
        {
            actualOutcome = "panic";
        }
        catch (const std::exception& error)
        {
            output << "Unexpected BestSamples exception in case " << caseName
                   << " step " << step << ": " << error.what() << '\n';
            return false;
        }

        std::string actualRetained;
        for (const auto& sample : samples.Samples())
        {
            if (!actualRetained.empty())
            {
                actualRetained += ',';
            }
            actualRetained += encodeEvaluation(sample.second);
        }
        if (actualRetained.empty())
        {
            actualRetained = "-";
        }
        const std::string actualUpperBound = encodeEvaluation(samples.UpperBound());
        if (actualOutcome != expectedOutcome || samples.Size() != expectedSize
            || actualRetained != expectedRetained
            || actualUpperBound != expectedUpperBound)
        {
            output << "BestSamples sequence mismatch at " << caseName << " step " << step
                   << ": expected " << expectedOutcome << '/' << expectedSize << '/'
                   << expectedRetained << '/' << expectedUpperBound << ", actual "
                   << actualOutcome << '/' << samples.Size() << '/' << actualRetained
                   << '/' << actualUpperBound << '\n';
            return false;
        }
        ++rowCount;
    }
    return Expect(headerSeen && rowCount == 40U,
                  output,
                  "BestSamples sequence fixture must cover 32 reports and four two-step NaN cases");
}

bool TestNonTransitiveSampleEvalSortMatchesRustReference(std::ostream& output)
{
    std::ifstream referenceFile;
    for (const char* path : {
             "SparrowCore/Tests/Fixtures/SampleEvalNonTransitiveSortRustReference.tsv",
             "../SparrowCore/Tests/Fixtures/SampleEvalNonTransitiveSortRustReference.tsv",
             "../JaguarRun/SparrowCore/Tests/Fixtures/SampleEvalNonTransitiveSortRustReference.tsv",
             "..\\SparrowCore\\Tests\\Fixtures\\SampleEvalNonTransitiveSortRustReference.tsv"})
    {
        referenceFile.open(path);
        if (referenceFile.is_open())
        {
            break;
        }
        referenceFile.clear();
    }
    if (!Expect(referenceFile.is_open(),
                output,
                "non-transitive SampleEval Rust fixture cannot be opened"))
    {
        return false;
    }

    const std::array<std::uint32_t, 4> lossBits = {
        0x3f800000U, 0x3f800004U, 0x3f800008U, 0x3f80000cU};
    std::array<sparrow::SampleEval, 4> evaluations{};
    for (std::size_t id = 0U; id < evaluations.size(); ++id)
    {
        float loss = 0.0F;
        std::memcpy(&loss, &lossBits[id], sizeof(loss));
        evaluations[id] = {sparrow::SampleKind::SAMPLE_KIND_CLEAR, loss};
    }
    if (!Expect(!sparrow::IsBetterSample(evaluations[0], evaluations[1])
                    && !sparrow::IsBetterSample(evaluations[1], evaluations[0])
                    && !sparrow::IsBetterSample(evaluations[1], evaluations[2])
                    && !sparrow::IsBetterSample(evaluations[2], evaluations[1])
                    && !sparrow::IsBetterSample(evaluations[2], evaluations[3])
                    && !sparrow::IsBetterSample(evaluations[3], evaluations[2])
                    && sparrow::IsBetterSample(evaluations[0], evaluations[2])
                    && sparrow::IsBetterSample(evaluations[1], evaluations[3]),
                output,
                "four-ULP SampleEval chain must expose non-transitive comparisons"))
    {
        return false;
    }

    const auto decodeOrder = [](const std::string& encoded,
                                std::array<std::size_t, 4>& ids) {
        if (encoded.size() != ids.size())
        {
            return false;
        }
        for (std::size_t position = 0U; position < ids.size(); ++position)
        {
            if (encoded[position] < 'A' || encoded[position] > 'D')
            {
                return false;
            }
            ids[position] = static_cast<std::size_t>(encoded[position] - 'A');
        }
        for (std::size_t left = 0U; left < ids.size(); ++left)
        {
            for (std::size_t right = left + 1U; right < ids.size(); ++right)
            {
                if (ids[left] == ids[right])
                {
                    return false;
                }
            }
        }
        return true;
    };

    std::array<std::string, 24> seenInputs{};
    std::size_t rowCount = 0U;
    bool headerSeen = false;
    std::string line;
    while (std::getline(referenceFile, line))
    {
        if (line.empty() || line.front() == '#')
        {
            continue;
        }
        if (!headerSeen)
        {
            if (!Expect(line == "input_order\toutput_order",
                        output,
                        "malformed non-transitive SampleEval fixture header"))
            {
                return false;
            }
            headerSeen = true;
            continue;
        }

        std::istringstream row(line);
        std::string inputOrder;
        std::string expectedOrder;
        if (!Expect(static_cast<bool>(row >> inputOrder >> expectedOrder),
                    output,
                    "malformed non-transitive SampleEval fixture row"))
        {
            return false;
        }
        std::array<std::size_t, 4> inputIds{};
        std::array<std::size_t, 4> expectedIds{};
        if (!Expect(decodeOrder(inputOrder, inputIds)
                        && decodeOrder(expectedOrder, expectedIds)
                        && rowCount < seenInputs.size(),
                    output,
                    "invalid input/output order in non-transitive SampleEval fixture"))
        {
            return false;
        }

        for (std::size_t repetition = 0U; repetition < 32U; ++repetition)
        {
            sparrow::BestSamples samples(4U, 0.0F);
            for (const std::size_t id : inputIds)
            {
                const auto transform = jaguar::Transform2d::FromRotationTranslation(
                    0.0, {static_cast<double>(id), 0.0});
                if (!Expect(samples.Report(transform, evaluations[id]),
                            output,
                            "BestSamples rejected a distinct reference sample"))
                {
                    return false;
                }
            }

            std::array<std::size_t, 4> actualIds{};
            for (std::size_t index = 0U; index < actualIds.size(); ++index)
            {
                actualIds[index] = static_cast<std::size_t>(std::llround(
                    samples.Samples()[index].first.translation.x));
            }
            if (actualIds != expectedIds)
            {
                output << "C++ SampleEval sort order: input=" << inputOrder
                       << " expected=" << expectedOrder << " actual=";
                for (const std::size_t id : actualIds)
                {
                    output << static_cast<char>('A' + id);
                }
                output << '\n';
                return Expect(false,
                              output,
                              "C++ BestSamples order differs from the locked Rust sort reference");
            }
        }
        seenInputs[rowCount++] = inputOrder;
    }

    std::sort(seenInputs.begin(), seenInputs.end());
    std::array<std::string, 24> expectedInputs{};
    std::array<std::size_t, 4> permutation = {0U, 1U, 2U, 3U};
    for (std::size_t index = 0U; index < expectedInputs.size(); ++index)
    {
        for (const std::size_t id : permutation)
        {
            expectedInputs[index].push_back(static_cast<char>('A' + id));
        }
        std::next_permutation(permutation.begin(), permutation.end());
    }
    std::sort(expectedInputs.begin(), expectedInputs.end());
    return Expect(headerSeen && rowCount == seenInputs.size()
                      && seenInputs == expectedInputs,
                  output,
                  "non-transitive SampleEval fixture must cover all 24 input permutations");
}

bool TestSampleEvalComparatorBoundaryMatchesRustReference(std::ostream& output)
{
    std::ifstream referenceFile;
    for (const char* path : {
             "SparrowCore/Tests/Fixtures/SampleEvalComparatorBoundaryRustReference.tsv",
             "../SparrowCore/Tests/Fixtures/SampleEvalComparatorBoundaryRustReference.tsv",
             "../JaguarRun/SparrowCore/Tests/Fixtures/SampleEvalComparatorBoundaryRustReference.tsv",
             "..\\SparrowCore\\Tests\\Fixtures\\SampleEvalComparatorBoundaryRustReference.tsv"})
    {
        referenceFile.open(path);
        if (referenceFile.is_open())
        {
            break;
        }
        referenceFile.clear();
    }
    if (!Expect(referenceFile.is_open(),
                output,
                "SampleEval boundary Rust fixture cannot be opened"))
    {
        return false;
    }

    const auto fromBits = [](std::uint32_t bits) {
        float value = 0.0F;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    };
    struct BoundaryCase
    {
        const char* name;
        std::array<sparrow::SampleEval, 3> evaluations;
    };
    const std::array<BoundaryCase, 7> cases = {{
        {"positive_ulp_chain", {{
             {sparrow::SampleKind::SAMPLE_KIND_CLEAR, fromBits(0x3f800000U)},
             {sparrow::SampleKind::SAMPLE_KIND_CLEAR, fromBits(0x3f800004U)},
             {sparrow::SampleKind::SAMPLE_KIND_CLEAR, fromBits(0x3f800008U)}}}},
        {"epsilon_chain", {{
             {sparrow::SampleKind::SAMPLE_KIND_CLEAR, 0.0F},
             {sparrow::SampleKind::SAMPLE_KIND_CLEAR,
              std::numeric_limits<float>::epsilon() * 0.75F},
             {sparrow::SampleKind::SAMPLE_KIND_CLEAR,
              std::numeric_limits<float>::epsilon() * 1.5F}}}},
        {"negative_epsilon_chain", {{
             {sparrow::SampleKind::SAMPLE_KIND_CLEAR,
              -std::numeric_limits<float>::epsilon() * 1.5F},
             {sparrow::SampleKind::SAMPLE_KIND_CLEAR,
              -std::numeric_limits<float>::epsilon() * 0.75F},
             {sparrow::SampleKind::SAMPLE_KIND_CLEAR, 0.0F}}}},
        {"negative_ulp_chain", {{
             {sparrow::SampleKind::SAMPLE_KIND_CLEAR, fromBits(0xbf800000U)},
             {sparrow::SampleKind::SAMPLE_KIND_CLEAR, fromBits(0xbf7ffffcU)},
             {sparrow::SampleKind::SAMPLE_KIND_CLEAR, fromBits(0xbf7ffff8U)}}}},
        {"positive_midrange_ulp_chain", {{
             {sparrow::SampleKind::SAMPLE_KIND_CLEAR, fromBits(0x40000000U)},
             {sparrow::SampleKind::SAMPLE_KIND_CLEAR, fromBits(0x40000004U)},
             {sparrow::SampleKind::SAMPLE_KIND_CLEAR, fromBits(0x40000008U)}}}},
        {"max_finite_to_infinity_chain", {{
             {sparrow::SampleKind::SAMPLE_KIND_CLEAR, fromBits(0x7f7ffffbU)},
             {sparrow::SampleKind::SAMPLE_KIND_CLEAR,
              std::numeric_limits<float>::max()},
             {sparrow::SampleKind::SAMPLE_KIND_CLEAR,
              std::numeric_limits<float>::infinity()}}}},
        {"signed_zero_ties", {{
             {sparrow::SampleKind::SAMPLE_KIND_CLEAR, 0.0F},
             {sparrow::SampleKind::SAMPLE_KIND_CLEAR, -0.0F},
             {sparrow::SampleKind::SAMPLE_KIND_CLEAR, 0.0F}}}},
    }};
    constexpr std::array<std::string_view, 6> permutations = {
        "ABC", "ACB", "BAC", "BCA", "CAB", "CBA"};
    std::array<std::array<bool, permutations.size()>, cases.size()> seen{};
    std::size_t rowCount = 0U;
    bool headerSeen = false;
    std::string line;
    while (std::getline(referenceFile, line))
    {
        if (line.empty() || line.front() == '#')
        {
            continue;
        }
        if (!headerSeen)
        {
            if (!Expect(line == "case\tinput_order\toutput_order",
                        output,
                        "malformed SampleEval boundary fixture header"))
            {
                return false;
            }
            headerSeen = true;
            continue;
        }

        std::istringstream row(line);
        std::string caseName;
        std::string inputOrder;
        std::string expectedOrder;
        if (!Expect(static_cast<bool>(row >> caseName >> inputOrder >> expectedOrder),
                    output,
                    "malformed SampleEval boundary fixture row"))
        {
            return false;
        }

        std::size_t caseIndex = cases.size();
        for (std::size_t index = 0U; index < cases.size(); ++index)
        {
            if (caseName == cases[index].name)
            {
                caseIndex = index;
                break;
            }
        }
        std::size_t permutationIndex = permutations.size();
        for (std::size_t index = 0U; index < permutations.size(); ++index)
        {
            if (inputOrder == permutations[index])
            {
                permutationIndex = index;
                break;
            }
        }
        if (!Expect(caseIndex < cases.size()
                        && permutationIndex < permutations.size()
                        && !seen[caseIndex][permutationIndex],
                    output,
                    "unknown or duplicate SampleEval boundary fixture row"))
        {
            return false;
        }

        const auto decode = [](std::string_view encoded,
                               std::array<std::size_t, 3>& ids) {
            if (encoded.size() != ids.size())
            {
                return false;
            }
            for (std::size_t index = 0U; index < ids.size(); ++index)
            {
                if (encoded[index] < 'A' || encoded[index] > 'C')
                {
                    return false;
                }
                ids[index] = static_cast<std::size_t>(encoded[index] - 'A');
            }
            return ids[0] != ids[1] && ids[0] != ids[2] && ids[1] != ids[2];
        };
        std::array<std::size_t, 3> inputIds{};
        std::array<std::size_t, 3> expectedIds{};
        if (!Expect(decode(inputOrder, inputIds) && decode(expectedOrder, expectedIds),
                    output,
                    "invalid SampleEval boundary order"))
        {
            return false;
        }

        for (std::size_t repetition = 0U; repetition < 32U; ++repetition)
        {
            sparrow::BestSamples samples(3U, 0.0F);
            for (const std::size_t id : inputIds)
            {
                const auto transform = jaguar::Transform2d::FromRotationTranslation(
                    0.0, {static_cast<double>(id), 0.0});
                if (!Expect(samples.Report(transform, cases[caseIndex].evaluations[id]),
                            output,
                            "BestSamples rejected a finite/infinite Rust boundary sample"))
                {
                    return false;
                }
            }

            std::array<std::size_t, 3> actualIds{};
            for (std::size_t index = 0U; index < actualIds.size(); ++index)
            {
                actualIds[index] = static_cast<std::size_t>(std::llround(
                    samples.Samples()[index].first.translation.x));
            }
            if (!Expect(actualIds == expectedIds,
                        output,
                        "BestSamples boundary order differs from Rust reference"))
            {
                output << "case=" << caseName << " input=" << inputOrder
                       << " expected=" << expectedOrder << " actual=";
                for (const std::size_t id : actualIds)
                {
                    output << static_cast<char>('A' + id);
                }
                output << '\n';
                return false;
            }
        }

        seen[caseIndex][permutationIndex] = true;
        ++rowCount;
    }

    const bool allRowsSeen = std::all_of(
        seen.begin(), seen.end(),
        [](const auto& caseRows) {
            return std::all_of(caseRows.begin(), caseRows.end(), [](bool value) {
                return value;
            });
        });
    return Expect(headerSeen && rowCount == cases.size() * permutations.size()
                      && allRowsSeen,
                  output,
                  "SampleEval boundary fixture must cover seven cases and all permutations");
}

bool TestSampleEvalNanMatchesRustFailure(std::ostream& output)
{
    const sparrow::SampleEval nan{
        sparrow::SampleKind::SAMPLE_KIND_CLEAR,
        std::numeric_limits<float>::quiet_NaN()};
    const sparrow::SampleEval finite{sparrow::SampleKind::SAMPLE_KIND_CLEAR, 1.0F};
    bool orderingThrows = false;
    try
    {
        static_cast<void>(nan < finite);
    }
    catch (const std::domain_error&)
    {
        orderingThrows = true;
    }
    return Expect(orderingThrows && !(nan == nan),
                  output,
                  "NaN comparison must fail and NaN equality must be false like Rust");
}
}

bool RunLbfLossTests(std::ostream& output)
{
    return TestLbfLossValues(output)
        && TestSampleEvalOrdering(output)
        && TestNonTransitiveSampleEvalSortMatchesRustReference(output)
        && TestSampleEvalComparatorBoundaryMatchesRustReference(output)
        && TestSampleEvalNanMatchesRustFailure(output)
        && TestBestSamples(output)
        && TestBestSamplesBoundaryMatchesRustReference(output)
        && TestBestSamplesSequenceAndNanFailuresMatchRustReference(output);
}
