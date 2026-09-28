use crate::consts::{
    PRE_REFINE_CD_R_STEPS, PRE_REFINE_CD_TL_RATIOS, SND_REFINE_CD_R_STEPS, SND_REFINE_CD_TL_RATIOS,
    UNIQUE_SAMPLE_THRESHOLD,
};
use crate::eval::sample_eval::{SampleEval, SampleEvaluator};
use crate::sample::best_samples::BestSamples;
use crate::sample::coord_descent::{CDConfig, refine_coord_desc};
use crate::sample::uniform_sampler::UniformBBoxSampler;
use jagua_rs::entities::{Item, Layout, PItemKey};
use jagua_rs::geometry::DTransformation;
use jagua_rs::geometry::geo_enums::RotationRange;
use log::debug;
use rand::Rng;

#[derive(Debug, Clone, Copy)]
pub struct SampleConfig {
    pub n_container_samples: usize,
    pub n_focussed_samples: usize,
    pub n_coord_descents: usize,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum SearchPhase {
    ReferencePlacement,
    FocusedSample,
    ContainerSample,
    PreRefine,
    FinalRefine,
}

trait SearchObserver {
    fn evaluated(
        &mut self,
        _phase: SearchPhase,
        _transform: DTransformation,
        _evaluation: SampleEval,
    ) {
    }

    fn reported(
        &mut self,
        _phase: SearchPhase,
        _transform: DTransformation,
        _evaluation: SampleEval,
        _accepted: bool,
        _retained: &[(DTransformation, SampleEval)],
    ) {
    }

    fn result(
        &mut self,
        _result: &Option<(DTransformation, SampleEval)>,
        _evaluation_count: usize,
    ) {
    }
}

struct NoopSearchObserver;

impl SearchObserver for NoopSearchObserver {}

struct ObservedEvaluator<E, O> {
    inner: E,
    observer: O,
    phase: SearchPhase,
}

impl<E, O: SearchObserver> ObservedEvaluator<E, O> {
    fn set_phase(&mut self, phase: SearchPhase) {
        self.phase = phase;
    }

    fn report(
        &mut self,
        transform: DTransformation,
        evaluation: SampleEval,
        accepted: bool,
        retained: &[(DTransformation, SampleEval)],
    ) {
        self.observer
            .reported(self.phase, transform, evaluation, accepted, retained);
    }
}

impl<E: SampleEvaluator, O: SearchObserver> SampleEvaluator for ObservedEvaluator<E, O> {
    fn evaluate_sample(
        &mut self,
        transform: DTransformation,
        upper_bound: Option<SampleEval>,
    ) -> SampleEval {
        let evaluation = self.inner.evaluate_sample(transform, upper_bound);
        self.observer.evaluated(self.phase, transform, evaluation);
        evaluation
    }

    fn n_evals(&self) -> usize {
        self.inner.n_evals()
    }
}

#[cfg(feature = "oracle-trace")]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SearchTracePhase {
    ReferencePlacement,
    FocusedSample,
    ContainerSample,
    PreRefine,
    FinalRefine,
}

#[cfg(feature = "oracle-trace")]
#[derive(Debug, Clone)]
pub enum SearchTraceEvent {
    Evaluation {
        phase: SearchTracePhase,
        transform: DTransformation,
        evaluation: SampleEval,
    },
    Report {
        phase: SearchTracePhase,
        transform: DTransformation,
        evaluation: SampleEval,
        accepted: bool,
        retained: Vec<(DTransformation, SampleEval)>,
    },
    Result {
        sample: Option<(DTransformation, SampleEval)>,
        evaluation_count: usize,
    },
}

#[cfg(feature = "oracle-trace")]
struct CallbackSearchObserver<'a, F> {
    callback: &'a mut F,
}

#[cfg(feature = "oracle-trace")]
impl<F: FnMut(SearchTraceEvent)> SearchObserver for CallbackSearchObserver<'_, F> {
    fn evaluated(
        &mut self,
        phase: SearchPhase,
        transform: DTransformation,
        evaluation: SampleEval,
    ) {
        (self.callback)(SearchTraceEvent::Evaluation {
            phase: phase.into(),
            transform,
            evaluation,
        });
    }

    fn reported(
        &mut self,
        phase: SearchPhase,
        transform: DTransformation,
        evaluation: SampleEval,
        accepted: bool,
        retained: &[(DTransformation, SampleEval)],
    ) {
        (self.callback)(SearchTraceEvent::Report {
            phase: phase.into(),
            transform,
            evaluation,
            accepted,
            retained: retained.to_vec(),
        });
    }

    fn result(&mut self, result: &Option<(DTransformation, SampleEval)>, evaluation_count: usize) {
        (self.callback)(SearchTraceEvent::Result {
            sample: *result,
            evaluation_count,
        });
    }
}

#[cfg(feature = "oracle-trace")]
impl From<SearchPhase> for SearchTracePhase {
    fn from(phase: SearchPhase) -> Self {
        match phase {
            SearchPhase::ReferencePlacement => Self::ReferencePlacement,
            SearchPhase::FocusedSample => Self::FocusedSample,
            SearchPhase::ContainerSample => Self::ContainerSample,
            SearchPhase::PreRefine => Self::PreRefine,
            SearchPhase::FinalRefine => Self::FinalRefine,
        }
    }
}

/// Algorithm 6 and Figure 7 from https://doi.org/10.48550/arXiv.2509.13329
pub fn search_placement(
    l: &Layout,
    item: &Item,
    ref_pk: Option<PItemKey>,
    evaluator: impl SampleEvaluator,
    sample_config: SampleConfig,
    rng: &mut impl Rng,
) -> (Option<(DTransformation, SampleEval)>, usize) {
    search_placement_impl(
        l,
        item,
        ref_pk,
        evaluator,
        sample_config,
        rng,
        NoopSearchObserver,
    )
}

#[cfg(feature = "oracle-trace")]
pub fn search_placement_with_trace<F: FnMut(SearchTraceEvent)>(
    l: &Layout,
    item: &Item,
    ref_pk: Option<PItemKey>,
    evaluator: impl SampleEvaluator,
    sample_config: SampleConfig,
    rng: &mut impl Rng,
    callback: &mut F,
) -> (Option<(DTransformation, SampleEval)>, usize) {
    search_placement_impl(
        l,
        item,
        ref_pk,
        evaluator,
        sample_config,
        rng,
        CallbackSearchObserver { callback },
    )
}

fn search_placement_impl<O: SearchObserver>(
    l: &Layout,
    item: &Item,
    ref_pk: Option<PItemKey>,
    evaluator: impl SampleEvaluator,
    sample_config: SampleConfig,
    rng: &mut impl Rng,
    observer: O,
) -> (Option<(DTransformation, SampleEval)>, usize) {
    let item_min_dim = f32::min(item.shape_cd.bbox.width(), item.shape_cd.bbox.height());

    let mut best_samples = BestSamples::new(
        sample_config.n_coord_descents,
        item_min_dim * UNIQUE_SAMPLE_THRESHOLD,
    );
    let mut evaluator = ObservedEvaluator {
        inner: evaluator,
        observer,
        phase: SearchPhase::ContainerSample,
    };

    //Create the two uniform samplers, one focussed around the reference placement, one for the whole container
    let focussed_sampler = match ref_pk {
        Some(ref_pk) => {
            //Add the current placement (and evaluation) as a candidate
            evaluator.set_phase(SearchPhase::ReferencePlacement);
            let dt = l.placed_items[ref_pk].d_transf;
            let eval = evaluator.evaluate_sample(dt, Some(best_samples.upper_bound()));

            debug!("[S] Starting from: {:?}", (dt, eval));
            let accepted = best_samples.report(dt, eval);
            evaluator.report(dt, eval, accepted, &best_samples.samples);

            //Create a uniform sampler focussed around the current placement
            let pi_bbox = l.placed_items[ref_pk].shape.bbox;
            UniformBBoxSampler::new(pi_bbox, item, l.container.outer_cd.bbox)
        }
        None => None,
    };
    let container_sampler =
        UniformBBoxSampler::new(l.container.outer_cd.bbox, item, l.container.outer_cd.bbox);

    //Perform the focussed sampling
    if let Some(focussed_sampler) = focussed_sampler {
        evaluator.set_phase(SearchPhase::FocusedSample);
        for _ in 0..sample_config.n_focussed_samples {
            let dt = focussed_sampler.sample(rng);
            let eval = evaluator.evaluate_sample(dt, Some(best_samples.upper_bound()));
            let accepted = best_samples.report(dt, eval);
            evaluator.report(dt, eval, accepted, &best_samples.samples);
        }
    }

    //Perform the container-wide sampling
    evaluator.set_phase(SearchPhase::ContainerSample);
    if let Some(container_sampler) = container_sampler {
        for _ in 0..sample_config.n_container_samples {
            let dt = container_sampler.sample(rng);
            let eval = evaluator.evaluate_sample(dt, Some(best_samples.upper_bound()));
            let accepted = best_samples.report(dt, eval);
            evaluator.report(dt, eval, accepted, &best_samples.samples);
        }
    }

    //Refine some of the best random samples to a local minimum in two steps:

    //1. Do a first refinement of all 'best samples' using coordinate descent
    evaluator.set_phase(SearchPhase::PreRefine);
    for start in best_samples.samples.clone() {
        let descended = refine_coord_desc(start, &mut evaluator, prerefine_cd_config(item), rng);
        let accepted = best_samples.report(descended.0, descended.1);
        evaluator.report(descended.0, descended.1, accepted, &best_samples.samples);
    }

    //2. Take the best one and do an even finer coordinate descent refinement
    evaluator.set_phase(SearchPhase::FinalRefine);
    let final_sample = best_samples
        .best()
        .map(|s| refine_coord_desc(s, &mut evaluator, final_refine_cd_config(item), rng));

    debug!(
        "[S] {} samples evaluated, final: {:?}",
        evaluator.n_evals(),
        final_sample
    );
    let evaluation_count = evaluator.n_evals();
    evaluator.observer.result(&final_sample, evaluation_count);
    (final_sample, evaluation_count)
}

fn prerefine_cd_config(item: &Item) -> CDConfig {
    let item_min_dim = f32::min(item.shape_cd.bbox.width(), item.shape_cd.bbox.height());
    let wiggle = item.allowed_rotation == RotationRange::Continuous;
    CDConfig {
        t_step_init: item_min_dim * PRE_REFINE_CD_TL_RATIOS.0,
        t_step_limit: item_min_dim * PRE_REFINE_CD_TL_RATIOS.1,
        r_step_init: PRE_REFINE_CD_R_STEPS.0,
        r_step_limit: PRE_REFINE_CD_R_STEPS.1,
        wiggle,
    }
}

fn final_refine_cd_config(item: &Item) -> CDConfig {
    let item_min_dim = f32::min(item.shape_cd.bbox.width(), item.shape_cd.bbox.height());
    let wiggle = item.allowed_rotation == RotationRange::Continuous;
    CDConfig {
        t_step_init: item_min_dim * SND_REFINE_CD_TL_RATIOS.0,
        t_step_limit: item_min_dim * SND_REFINE_CD_TL_RATIOS.1,
        r_step_init: SND_REFINE_CD_R_STEPS.0,
        r_step_limit: SND_REFINE_CD_R_STEPS.1,
        wiggle,
    }
}
