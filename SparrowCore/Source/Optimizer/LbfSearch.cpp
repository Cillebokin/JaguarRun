#include "SparrowCore/Optimizer/LbfSearch.h"
#include "LbfOptimizerTrace.h"
#include "LbfSearchTrace.h"

#include "SparrowCore/Eval/LbfEvaluator.h"
#include "SparrowCore/Sampling/LocalSearchSampler.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <locale>
#include <sstream>
#include <utility>
#include <vector>

namespace
{
using SearchResultValue = std::optional<sparrow::SearchResult>;
constexpr float TWO_PI = 6.28318530717958647692F;

sparrow::Result<SearchResultValue> InvalidSearch(const char* message)
{
    return sparrow::Result<SearchResultValue>::FromError(
        {jaguar::ErrorCode::InvalidInput, message});
}

sparrow::LbfLoss LossFromShape(const jaguar::SimplePolygon& shape)
{
    return sparrow::LbfLoss::FromBoundingBox(shape.BoundingBox());
}

float SampleUniformRange(float minimum, float maximum, sparrow::SparrowRng& rng) noexcept
{
    return minimum + (maximum - minimum) * rng.Uniform01F32();
}

class LbfUniformSampler
{
public:
    static sparrow::Result<LbfUniformSampler> Create(
        const jaguar::Rect2d& bounds, const jaguar::Item& item)
    {
        const float xMin = static_cast<float>(bounds.xMin);
        const float yMin = static_cast<float>(bounds.yMin);
        const float xMax = static_cast<float>(bounds.xMax);
        const float yMax = static_cast<float>(bounds.yMax);
        if (!std::isfinite(xMin) || !std::isfinite(yMin)
            || !std::isfinite(xMax) || !std::isfinite(yMax)
            || xMin >= xMax || yMin >= yMax)
        {
            return sparrow::Result<LbfUniformSampler>::FromError(
                {jaguar::ErrorCode::InvalidInput,
                 "LBF uniform sampler bounds must be finite and non-degenerate"});
        }

        std::vector<float> discreteRotations;
        if (item.AllowedRotation().mode == jaguar::RotationMode::Discrete)
        {
            discreteRotations.reserve(item.AllowedRotation().radians.size());
            for (const double angle : item.AllowedRotation().radians)
            {
                const float angleF32 = static_cast<float>(angle);
                if (!std::isfinite(angle) || !std::isfinite(angleF32))
                {
                    return sparrow::Result<LbfUniformSampler>::FromError(
                        {jaguar::ErrorCode::InvalidInput,
                         "LBF uniform sampler rotation must be finite"});
                }
                discreteRotations.push_back(angleF32);
            }
            if (discreteRotations.empty())
            {
                return sparrow::Result<LbfUniformSampler>::FromError(
                    {jaguar::ErrorCode::InvalidInput,
                     "LBF discrete rotation set must not be empty"});
            }
        }

        return sparrow::Result<LbfUniformSampler>::FromValue(
            LbfUniformSampler{item.AllowedRotation().mode,
                              {xMin, yMin, xMax, yMax},
                              std::move(discreteRotations)});
    }

    jaguar::Transform2d Sample(sparrow::SparrowRng& rng) const noexcept
    {
        float rotation = 0.0F;
        if (m_RotationMode == jaguar::RotationMode::Continuous)
        {
            rotation = SampleUniformRange(0.0F, TWO_PI, rng);
        }
        else if (m_RotationMode == jaguar::RotationMode::Discrete)
        {
            const auto index = rng.UniformIndex(
                static_cast<std::uint32_t>(m_DiscreteRotations.size()));
            rotation = m_DiscreteRotations[static_cast<std::size_t>(index)];
        }

        const float x = SampleUniformRange(m_Bounds.xMin, m_Bounds.xMax, rng);
        const float y = SampleUniformRange(m_Bounds.yMin, m_Bounds.yMax, rng);
        return jaguar::Transform2d::FromRotationTranslation(
            static_cast<double>(rotation),
            {static_cast<double>(x), static_cast<double>(y)});
    }

    jaguar::Rect2d BoundingBox() const noexcept
    {
        return {static_cast<double>(m_Bounds.xMin),
                static_cast<double>(m_Bounds.yMin),
                static_cast<double>(m_Bounds.xMax),
                static_cast<double>(m_Bounds.yMax)};
    }

private:
    struct BoundsF32
    {
        float xMin = 0.0F;
        float yMin = 0.0F;
        float xMax = 0.0F;
        float yMax = 0.0F;
    };

    LbfUniformSampler(jaguar::RotationMode rotationMode,
                      BoundsF32 bounds,
                      std::vector<float> discreteRotations)
        : m_RotationMode(rotationMode),
          m_Bounds(bounds),
          m_DiscreteRotations(std::move(discreteRotations))
    {
    }

    jaguar::RotationMode m_RotationMode = jaguar::RotationMode::None;
    BoundsF32 m_Bounds{};
    std::vector<float> m_DiscreteRotations;
};

class TracingEvaluator final : public sparrow::SampleEvaluator
{
public:
    TracingEvaluator(const jaguar::Layout& layout,
                     const jaguar::Item& item,
                     const jaguar::HazardFilter& filter,
                     const sparrow::LbfSearchTraceCallback* callback)
        : m_Delegate(layout, item, filter),
          m_Callback(callback)
    {
    }

    void SetPhase(sparrow::LbfSearchTracePhase phase) noexcept
    {
        m_Phase = phase;
    }

    sparrow::SampleEval Evaluate(
        const jaguar::Transform2d& transform,
        const std::optional<sparrow::SampleEval>& upperBound) override
    {
        const sparrow::SampleEval evaluation =
            m_Delegate.Evaluate(transform, upperBound);
        if (m_Callback != nullptr)
        {
            sparrow::LbfSearchTraceEvent event;
            event.type = sparrow::LbfSearchTraceEvent::Type::Evaluation;
            event.phase = m_Phase;
            event.transform = transform;
            event.evaluation = evaluation;
            (*m_Callback)(event);
        }
        return evaluation;
    }

    std::size_t EvaluationCount() const noexcept override
    {
        return m_Delegate.EvaluationCount();
    }

private:
    sparrow::LbfEvaluator m_Delegate;
    const sparrow::LbfSearchTraceCallback* m_Callback = nullptr;
    sparrow::LbfSearchTracePhase m_Phase =
        sparrow::LbfSearchTracePhase::ContainerSample;
};
}

namespace sparrow
{
namespace
{
Result<std::optional<SearchResult>> SearchLbfImpl(
    const jaguar::Layout& layout,
    const jaguar::Item& item,
    const LbfConfig& config,
    SparrowRng& rng,
    const jaguar::HazardFilter& filter,
    const LbfSearchTraceCallback* callback)
{
    const Result<void> configResult = config.Validate();
    if (!configResult.HasValue())
    {
        return Result<std::optional<SearchResult>>::FromError(configResult.GetError());
    }
    if (item.CollisionShape().GetSurrogate() == nullptr)
    {
        return InvalidSearch("LBF item has no cached surrogate");
    }
    const jaguar::Rect2d containerBounds = layout.GetContainer().Bounds();
    if (!std::isfinite(containerBounds.xMin)
        || !std::isfinite(containerBounds.yMin)
        || !std::isfinite(containerBounds.xMax)
        || !std::isfinite(containerBounds.yMax)
        || containerBounds.xMin >= containerBounds.xMax
        || containerBounds.yMin >= containerBounds.yMax)
    {
        return InvalidSearch("LBF container bounds are invalid");
    }

    TracingEvaluator evaluator(layout, item, filter, callback);
    auto samplerResult = LbfUniformSampler::Create(containerBounds, item);
    if (!samplerResult.HasValue())
    {
        return Result<std::optional<SearchResult>>::FromError(
            samplerResult.GetError());
    }
    LbfUniformSampler sampler = std::move(samplerResult.Value());
    const jaguar::Rect2d itemBounds = item.CollisionShape().BoundingBox();
    const float itemMinimumDimension = static_cast<float>(std::min(
        itemBounds.Width(), itemBounds.Height()));
    if (!std::isfinite(itemMinimumDimension) || itemMinimumDimension <= 0.0F)
    {
        return InvalidSearch("LBF item minimum dimension is invalid");
    }

    const std::size_t localSearchBudget = config.LocalSearchSampleBudget();
    const std::size_t uniformBudget = config.UniformSampleBudget();
    std::optional<std::pair<jaguar::Transform2d, SampleEval>> best;
    std::vector<LbfLoss> tighteningHistory;
    std::optional<LbfLoss> bestLoss;
    LbfSearchTracePhase evaluatorPhase = LbfSearchTracePhase::ContainerSample;
    const auto report = [&](const jaguar::Transform2d& candidate,
                            const SampleEval& evaluation,
                            std::size_t sampleIndex,
                            std::size_t sampleBudget) {
        std::optional<LbfLoss> candidateLoss;
        if (evaluation.kind == SampleKind::SAMPLE_KIND_CLEAR)
        {
            const auto transformed = item.CollisionShape().TransformedRustF32(candidate);
            if (transformed.HasValue())
            {
                candidateLoss = LossFromShape(transformed.Value());
            }
        }
        const bool accepted = candidateLoss.has_value()
            && (!bestLoss.has_value() || *candidateLoss < *bestLoss);
        if (accepted)
        {
            best = std::make_pair(candidate, evaluation);
            bestLoss = *candidateLoss;
            tighteningHistory.push_back(*candidateLoss);
        }
        if (callback != nullptr)
        {
            LbfSearchTraceEvent event;
            event.type = LbfSearchTraceEvent::Type::Report;
            event.phase = evaluatorPhase;
            event.transform = candidate;
            event.evaluation = evaluation;
            event.accepted = accepted;
            event.sampleIndex = sampleIndex;
            event.sampleBudget = sampleBudget;
            if (best.has_value())
            {
                event.retained.push_back(*best);
            }
            (*callback)(event);
        }
        return accepted;
    };

    // jagua-rs/lbf divides n_samples into uniform and local-search budgets.
    for (std::size_t index = 0U; index < uniformBudget; ++index)
    {
        const jaguar::Transform2d candidate = sampler.Sample(rng);
        std::optional<SampleEval> upperBound;
        if (best.has_value())
        {
            upperBound = best->second;
        }
        if (report(candidate,
                   evaluator.Evaluate(candidate, upperBound),
                   index,
                   uniformBudget))
        {
            const jaguar::Rect2d tightenedBounds =
                bestLoss->TightenSampleBBox(sampler.BoundingBox());
            auto tightenedSampler = LbfUniformSampler::Create(tightenedBounds, item);
            if (!tightenedSampler.HasValue())
            {
                return Result<std::optional<SearchResult>>::FromError(
                    tightenedSampler.GetError());
            }
            sampler = std::move(tightenedSampler.Value());
        }
    }

    // Rust starts local search only after uniform sampling has found a valid
    // placement; without that seed the local-search budget is left unused.
    if (!best.has_value())
    {
        return Result<std::optional<SearchResult>>::FromValue(std::nullopt);
    }

    if (localSearchBudget != 0U)
    {
        auto localSamplerResult = LocalSearchSampler::Create(
            item, best->first, config, containerBounds);
        if (!localSamplerResult.HasValue())
        {
            return Result<std::optional<SearchResult>>::FromError(
                localSamplerResult.GetError());
        }
        LocalSearchSampler localSampler = std::move(localSamplerResult.Value());
        evaluatorPhase = LbfSearchTracePhase::LocalSearch;
        evaluator.SetPhase(evaluatorPhase);
        for (std::size_t index = 0U; index < localSearchBudget; ++index)
        {
            const jaguar::Transform2d candidate = localSampler.Sample(rng);
            const auto upperBound = std::optional<SampleEval>{best->second};
            if (report(candidate,
                       evaluator.Evaluate(candidate, upperBound),
                       index,
                       localSearchBudget))
            {
                localSampler.ShiftMean(candidate);
            }
            const float progress = static_cast<float>(index)
                / static_cast<float>(localSearchBudget);
            localSampler.DecayStddev(progress);
        }
    }

    const auto& finalSample = *best;
    SearchResult result;
    result.transform = finalSample.first;
    result.loss = *bestLoss;
    result.samples = evaluator.EvaluationCount();
    result.tighteningHistory = std::move(tighteningHistory);
    if (callback != nullptr)
    {
        LbfSearchTraceEvent event;
        event.type = LbfSearchTraceEvent::Type::Result;
        event.phase = LbfSearchTracePhase::FinalRefine;
        event.transform = finalSample.first;
        event.evaluation = finalSample.second;
        event.evaluationCount = result.samples;
        (*callback)(event);
    }
    return Result<std::optional<SearchResult>>::FromValue(
        std::optional<SearchResult>{std::move(result)});
}
}

Result<std::optional<SearchResult>> SearchLbf(
    const jaguar::Layout& layout,
    const jaguar::Item& item,
    const LbfConfig& config,
    SparrowRng& rng,
    const jaguar::HazardFilter& filter)
{
    return SearchLbfImpl(layout, item, config, rng, filter, nullptr);
}

Result<std::optional<SearchResult>> SearchLbfWithTrace(
    const jaguar::Layout& layout,
    const jaguar::Item& item,
    const LbfConfig& config,
    SparrowRng& rng,
    const jaguar::HazardFilter& filter,
    const LbfSearchTraceCallback& callback)
{
    return SearchLbfImpl(layout, item, config, rng, filter, &callback);
}

Result<std::optional<SearchResult>> SearchLbfWithDiagnostics(
    const jaguar::Layout& layout,
    const jaguar::Item& item,
    const LbfConfig& config,
    SparrowRng& rng,
    const jaguar::HazardFilter& filter,
    const LbfDiagnosticCallback& callback)
{
    const LbfSearchTraceCallback trace = [&callback](
        const LbfSearchTraceEvent& event) {
        if (event.type != LbfSearchTraceEvent::Type::Report || !event.accepted)
        {
            return;
        }

        const char* phase = nullptr;
        if (event.phase == LbfSearchTracePhase::ContainerSample)
        {
            phase = "UNI";
        }
        else if (event.phase == LbfSearchTracePhase::LocalSearch)
        {
            phase = "LS";
        }
        if (phase == nullptr)
        {
            return;
        }

        std::ostringstream message;
        message.imbue(std::locale::classic());
        message << '[' << phase << ": " << event.sampleIndex << '/'
                << event.sampleBudget << "] better: r: "
                << std::fixed << std::setprecision(3)
                << static_cast<float>(event.transform.rotationRad)
                       * (180.0F / 3.14159265358979323846F)
                << "°, t: ("
                << static_cast<float>(event.transform.translation.x) << ", "
                << static_cast<float>(event.transform.translation.y) << ')';
        if (event.phase == LbfSearchTracePhase::ContainerSample)
        {
            message << ' ';
        }
        callback(message.str());
    };
    return SearchLbfWithTrace(layout, item, config, rng, filter, trace);
}

Result<std::vector<jaguar::Transform2d>> SampleLbfUniformForTrace(
    const jaguar::Rect2d& bounds,
    const jaguar::Item& item,
    SparrowRng& rng,
    std::size_t sampleCount)
{
    auto samplerResult = LbfUniformSampler::Create(bounds, item);
    if (!samplerResult.HasValue())
    {
        return Result<std::vector<jaguar::Transform2d>>::FromError(
            samplerResult.GetError());
    }

    const LbfUniformSampler sampler = std::move(samplerResult.Value());
    std::vector<jaguar::Transform2d> samples;
    samples.reserve(sampleCount);
    for (std::size_t index = 0U; index < sampleCount; ++index)
    {
        samples.push_back(sampler.Sample(rng));
    }
    return Result<std::vector<jaguar::Transform2d>>::FromValue(
        std::move(samples));
}
}
