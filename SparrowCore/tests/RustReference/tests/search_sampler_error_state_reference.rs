use jagua_rs::entities::Item;
use jagua_rs::geometry::DTransformation;
use jagua_rs::geometry::geo_enums::RotationRange;
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
use std::sync::Arc;

struct ClearEvaluator;

impl SampleEvaluator for ClearEvaluator {
    fn evaluate_sample(
        &mut self,
        _transform: DTransformation,
        _upper_bound: Option<SampleEval>,
    ) -> SampleEval {
        SampleEval::Clear { loss: 0.0 }
    }

    fn n_evals(&self) -> usize {
        0
    }
}

#[test]
fn separator_sampler_panic_preserves_layout_and_rng_after_reference_events() {
    let (external, _) =
        sparrow::util::io::read_spp_input(Path::new("../Fixtures/RustSppSingleItem.json"))
            .expect("read single-item fixture");
    let defaults = DEFAULT_SPARROW_CONFIG;
    let importer = jagua_rs::io::import::Importer::new(
        defaults.cde_config,
        defaults.poly_simpl_tolerance,
        defaults.narrow_concavity_cutoff_ratio,
    );
    let mut instance = import_instance(&importer, &external).expect("import single-item fixture");
    let mut item: Item = (**instance.item(0)).clone();
    item.allowed_rotation = RotationRange::Discrete(vec![f32::NAN]);
    instance.items[0].0 = Arc::new(item);

    let mut problem = SPProblem::new(instance.clone()).expect("create separator problem");
    problem
        .change_strip_width(8.0)
        .expect("resize separator strip");
    let moving_key = problem.place_item(SPPlacement {
        item_idx: 0,
        d_transf: DTransformation::new(0.0, (1.0, 1.0)),
    });
    let before = problem.layout.placed_items[moving_key].d_transf;
    let mut rng = Xoshiro256PlusPlus::seed_from_u64(42);
    let mut events = Vec::new();
    let panic = catch_unwind(AssertUnwindSafe(|| {
        search_placement_with_trace(
            &problem.layout,
            instance.item(0),
            Some(moving_key),
            ClearEvaluator,
            SampleConfig {
                n_container_samples: 1,
                n_focussed_samples: 1,
                n_coord_descents: 1,
            },
            &mut rng,
            &mut |event| events.push(event),
        )
    }));

    assert!(panic.is_err(), "non-finite focused rotation must panic");
    assert!(matches!(
        events.as_slice(),
        [
            SearchTraceEvent::Evaluation {
                phase: SearchTracePhase::ReferencePlacement,
                ..
            },
            SearchTraceEvent::Report {
                phase: SearchTracePhase::ReferencePlacement,
                accepted: true,
                ..
            }
        ]
    ));
    assert_eq!(
        problem.layout.placed_items[moving_key].d_transf.rotation(),
        before.rotation()
    );
    assert_eq!(
        problem.layout.placed_items[moving_key]
            .d_transf
            .translation(),
        before.translation()
    );
    assert_eq!(
        rng.next_u64(),
        15_021_278_609_987_233_951,
        "sampler construction panic must not advance the seed-42 RNG"
    );
}
