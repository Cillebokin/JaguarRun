use jagua_rs::geometry::DTransformation;
use jagua_rs::geometry::geo_enums::RotationRange;
use jagua_rs::io::import::Importer;
use jagua_rs::probs::spp::entities::{SPPlacement, SPProblem};
use jagua_rs::probs::spp::io::import_instance;
use rand::rngs::Xoshiro256PlusPlus;
use rand::{Rng, SeedableRng};
use sparrow::config::DEFAULT_SPARROW_CONFIG;
use sparrow::eval::sample_eval::{SampleEval, SampleEvaluator};
use sparrow::sample::search::{
    SampleConfig, SearchTraceEvent, SearchTracePhase, search_placement_with_trace,
};
use std::panic::{AssertUnwindSafe, catch_unwind};
use std::path::Path;

struct ClearEvaluator {
    evaluations: usize,
}

impl SampleEvaluator for ClearEvaluator {
    fn evaluate_sample(
        &mut self,
        _transform: DTransformation,
        _upper_bound: Option<SampleEval>,
    ) -> SampleEval {
        self.evaluations += 1;
        SampleEval::Clear { loss: 0.0 }
    }

    fn n_evals(&self) -> usize {
        self.evaluations
    }
}

fn build_instance() -> jagua_rs::probs::spp::entities::SPInstance {
    let (external, _) =
        sparrow::util::io::read_spp_input(Path::new("../Fixtures/RustSppSingleItem.json"))
            .expect("read zero-boundary sample instance");
    let defaults = DEFAULT_SPARROW_CONFIG;
    let importer = Importer::new(
        defaults.cde_config,
        defaults.poly_simpl_tolerance,
        defaults.narrow_concavity_cutoff_ratio,
    );
    import_instance(&importer, &external).expect("import zero-boundary sample instance")
}

#[test]
fn zero_sample_budgets_follow_the_reference_placement_path() {
    let instance = build_instance();
    let mut empty_problem = SPProblem::new(instance.clone()).expect("create empty sample problem");
    empty_problem
        .change_strip_width(6.0)
        .expect("widen empty sample problem");
    let mut cold_rng = Xoshiro256PlusPlus::seed_from_u64(42);
    let mut events = Vec::new();
    let (empty_result, empty_count) = search_placement_with_trace(
        &empty_problem.layout,
        instance.item(0),
        None,
        ClearEvaluator { evaluations: 0 },
        SampleConfig {
            n_container_samples: 0,
            n_focussed_samples: 0,
            n_coord_descents: 1,
        },
        &mut cold_rng,
        &mut |event| events.push(event),
    );
    assert!(empty_result.is_none());
    assert_eq!(empty_count, 0);
    assert!(matches!(
        events.as_slice(),
        [SearchTraceEvent::Result {
            evaluation_count: 0,
            sample: None
        }]
    ));
    assert_eq!(
        cold_rng.next_u64(),
        15_021_278_609_987_233_951,
        "zero-budget empty-pool search must leave the seed-42 stream untouched"
    );

    let mut warm_problem = SPProblem::new(instance.clone()).expect("create warm sample problem");
    warm_problem
        .change_strip_width(6.0)
        .expect("widen warm sample problem");
    let moving_key = warm_problem.place_item(SPPlacement {
        item_idx: 0,
        d_transf: DTransformation::new(0.0, (2.0, 2.0)),
    });
    let mut warm_rng = Xoshiro256PlusPlus::seed_from_u64(42);
    let mut warm_events = Vec::new();
    let (warm_result, warm_count) = search_placement_with_trace(
        &warm_problem.layout,
        instance.item(0),
        Some(moving_key),
        ClearEvaluator { evaluations: 0 },
        SampleConfig {
            n_container_samples: 0,
            n_focussed_samples: 0,
            n_coord_descents: 1,
        },
        &mut warm_rng,
        &mut |event| warm_events.push(event),
    );
    assert!(warm_result.is_some());
    assert!(warm_count > 0);
    assert!(!warm_events.iter().any(|event| matches!(
        event,
        SearchTraceEvent::Evaluation {
            phase: SearchTracePhase::FocusedSample | SearchTracePhase::ContainerSample,
            ..
        }
    )));
}

#[test]
fn nonfinite_discrete_rotation_panics_before_rng_consumption() {
    let instance = build_instance();
    let mut problem = SPProblem::new(instance.clone()).expect("create non-finite-rotation problem");
    problem
        .change_strip_width(6.0)
        .expect("widen non-finite-rotation problem");
    let mut item = (**instance.item(0)).clone();
    item.allowed_rotation = RotationRange::Discrete(vec![f32::NAN]);

    let mut rng = Xoshiro256PlusPlus::seed_from_u64(42);
    let mut events = Vec::new();
    let panic = catch_unwind(AssertUnwindSafe(|| {
        search_placement_with_trace(
            &problem.layout,
            &item,
            None,
            ClearEvaluator { evaluations: 0 },
            SampleConfig {
                n_container_samples: 1,
                n_focussed_samples: 0,
                n_coord_descents: 1,
            },
            &mut rng,
            &mut |event| events.push(event),
        )
    }));
    assert!(
        panic.is_err(),
        "non-finite rotation must panic during sampler construction"
    );
    assert!(events.is_empty(), "the panic precedes all search callbacks");
    assert_eq!(
        rng.next_u64(),
        15_021_278_609_987_233_951,
        "the sampler-construction panic must leave the seed-42 stream untouched"
    );
}

#[cfg(debug_assertions)]
#[test]
fn zero_coordinate_descent_capacity_panics_like_rust_best_samples() {
    let instance = build_instance();
    let mut problem = SPProblem::new(instance.clone()).expect("create zero-capacity problem");
    problem
        .change_strip_width(6.0)
        .expect("widen zero-capacity problem");
    let mut rng = Xoshiro256PlusPlus::seed_from_u64(42);
    let result = catch_unwind(AssertUnwindSafe(|| {
        search_placement_with_trace(
            &problem.layout,
            instance.item(0),
            None,
            ClearEvaluator { evaluations: 0 },
            SampleConfig {
                n_container_samples: 1,
                n_focussed_samples: 0,
                n_coord_descents: 0,
            },
            &mut rng,
            &mut |_event| {},
        )
    }));
    assert!(
        result.is_err(),
        "zero capacity must panic in the Rust reference profile"
    );
}

#[cfg(not(debug_assertions))]
#[test]
fn zero_coordinate_descent_capacity_preserves_rust_release_overflow_behavior() {
    let instance = build_instance();
    let mut problem = SPProblem::new(instance.clone()).expect("create zero-capacity problem");
    problem
        .change_strip_width(6.0)
        .expect("widen zero-capacity problem");
    let mut rng = Xoshiro256PlusPlus::seed_from_u64(42);
    let (result, evaluations) = search_placement_with_trace(
        &problem.layout,
        instance.item(0),
        None,
        ClearEvaluator { evaluations: 0 },
        SampleConfig {
            n_container_samples: 1,
            n_focussed_samples: 0,
            n_coord_descents: 0,
        },
        &mut rng,
        &mut |_event| {},
    );
    assert!(result.is_some());
    assert!(evaluations > 0);
}
