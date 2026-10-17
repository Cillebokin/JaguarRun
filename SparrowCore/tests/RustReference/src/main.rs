use jagua_rs::entities::Item;
use jagua_rs::entities::Layout;
use jagua_rs::geometry::DTransformation;
use jagua_rs::geometry::primitives::Rect;
use jagua_rs::io::import::Importer;
use jagua_rs::probs::spp::entities::{SPPlacement, SPProblem};
use jagua_rs::probs::spp::io::import_instance;
use rand::rngs::Xoshiro256PlusPlus;
use rand::seq::SliceRandom;
use rand::{Rng, RngExt, SeedableRng};
use sparrow::config::DEFAULT_SPARROW_CONFIG;
use sparrow::config::ExplorationConfig;
use sparrow::eval::lbf_evaluator::LBFEvaluator;
use sparrow::eval::sample_eval::{SampleEval, SampleEvaluator};
use sparrow::eval::sep_evaluator::SeparationEvaluator;
use sparrow::optimizer::explore::exploration_phase;
use sparrow::optimizer::lbf::LBFBuilder;
use sparrow::optimizer::separator::{Separator, SeparatorConfig};
use sparrow::quantify::tracker::CollisionTracker;
use sparrow::sample::CDConfig;
use sparrow::sample::refine_coord_desc;
use sparrow::sample::search::{
    SampleConfig, SearchTraceEvent, SearchTracePhase, search_placement_with_trace,
};
use sparrow::sample::uniform_sampler::UniformBBoxSampler;
use sparrow::util::listener::{ReportType, SeparationProgress, SeparationResult, SolutionListener};
use sparrow::util::terminator::Terminator;
use std::cell::Cell;
use std::env;
use std::error::Error;
use std::fs;
use std::path::Path;
use std::time::Duration;

fn main() -> Result<(), Box<dyn Error>> {
    let mut raw_args = env::args().skip(1);
    let requested_mode = raw_args.next();
    if requested_mode.as_deref() == Some("--write-slice-shuffle-reference") {
        let output_path = raw_args
            .next()
            .unwrap_or_else(|| "../Fixtures/SparrowSliceShuffleRustReference.tsv".to_owned());
        if let Some(parent) = Path::new(&output_path).parent() {
            fs::create_dir_all(parent)?;
        }
        fs::write(output_path, slice_shuffle_reference_tsv())?;
        return Ok(());
    }
    if requested_mode.as_deref() == Some("--write-continuous-sampler-reference") {
        let output_path = raw_args
            .next()
            .unwrap_or_else(|| "../Fixtures/UniformSamplerContinuousGridRust.tsv".to_owned());
        if let Some(parent) = Path::new(&output_path).parent() {
            fs::create_dir_all(parent)?;
        }
        fs::write(
            output_path,
            uniform_sampler_continuous_grid_reference_tsv()?,
        )?;
        return Ok(());
    }

    let mut args = env::args().skip(1);
    let input_path = args
        .next()
        .unwrap_or_else(|| "../Fixtures/RustSppSingleItem.json".to_owned());
    let output_path = args
        .next()
        .unwrap_or_else(|| "../Fixtures/SparrowLbfRustReference.tsv".to_owned());
    let rng_output_path = args
        .next()
        .unwrap_or_else(|| "../Fixtures/LbfSeed42Reference.tsv".to_owned());
    let trace_output_path = args
        .next()
        .unwrap_or_else(|| "../Fixtures/SamplingCoordinateDescentRustReference.tsv".to_owned());
    let index_output_path = args
        .next()
        .unwrap_or_else(|| "../Fixtures/SparrowUniformIndexRustReference.tsv".to_owned());
    let search_trace_output_path = args
        .next()
        .unwrap_or_else(|| "../Fixtures/LbfSearchRustTrace.tsv".to_owned());
    let separator_input_path = args
        .next()
        .unwrap_or_else(|| "../Fixtures/SeparatorRustTrace.json".to_owned());
    let separator_trace_output_path = args
        .next()
        .unwrap_or_else(|| "../Fixtures/SeparatorSearchRustTrace.tsv".to_owned());
    let separator_round_input_path = args
        .next()
        .unwrap_or_else(|| "../Fixtures/SeparatorRoundRustTrace.json".to_owned());
    let separator_round_trace_output_path = args
        .next()
        .unwrap_or_else(|| "../Fixtures/SeparatorRoundRustTrace.tsv".to_owned());
    let separator_multi_worker_round_trace_output_path = args
        .next()
        .unwrap_or_else(|| "../Fixtures/SeparatorMultiWorkerRoundRustTrace.tsv".to_owned());
    let separator_three_worker_seed43_round_trace_output_path = args
        .next()
        .unwrap_or_else(|| "../Fixtures/SeparatorThreeWorkerSeed43RoundRustTrace.tsv".to_owned());
    let exploration_trace_output_path = args
        .next()
        .unwrap_or_else(|| "../Fixtures/ExplorationRustTrace.tsv".to_owned());
    let exploration_pool_input_path = args
        .next()
        .unwrap_or_else(|| "../Fixtures/ExplorationPool.json".to_owned());
    let exploration_pool_trace_output_path = args
        .next()
        .unwrap_or_else(|| "../Fixtures/ExplorationPoolRustTrace.tsv".to_owned());
    let exploration_contained_input_path = args
        .next()
        .unwrap_or_else(|| "../Fixtures/ExplorationContained.json".to_owned());
    let exploration_contained_trace_output_path = args
        .next()
        .unwrap_or_else(|| "../Fixtures/ExplorationContainedRustTrace.tsv".to_owned());
    let narrow_sampler_trace_output_path = args
        .next()
        .unwrap_or_else(|| "../Fixtures/UniformSamplerNarrowIntervalRust.tsv".to_owned());

    let (external_instance, _) = sparrow::util::io::read_spp_input(Path::new(&input_path))?;
    let config = DEFAULT_SPARROW_CONFIG;
    let importer = Importer::new(
        config.cde_config,
        config.poly_simpl_tolerance,
        config.narrow_concavity_cutoff_ratio,
    );
    let instance = import_instance(&importer, &external_instance)?;
    let (external_exploration_pool_instance, _) =
        sparrow::util::io::read_spp_input(Path::new(&exploration_pool_input_path))?;
    let exploration_pool_instance =
        import_instance(&importer, &external_exploration_pool_instance)?;
    let (external_exploration_contained_instance, _) =
        sparrow::util::io::read_spp_input(Path::new(&exploration_contained_input_path))?;
    let exploration_contained_instance =
        import_instance(&importer, &external_exploration_contained_instance)?;
    let trace_output = sampling_coordinate_descent_reference_tsv(instance.item(0));
    let sample_config = SampleConfig {
        n_container_samples: 64,
        n_focussed_samples: 0,
        n_coord_descents: 1,
    };
    let search_trace = lbf_search_trace_tsv(&instance);
    let separator_trace = separator_search_trace_tsv(Path::new(&separator_input_path))?;
    let separator_round_trace =
        separator_round_trace_tsv(Path::new(&separator_round_input_path), 1, 42)?;
    let separator_multi_worker_round_trace =
        separator_round_trace_tsv(Path::new(&separator_round_input_path), 2, 42)?;
    let separator_three_worker_seed43_round_trace =
        separator_round_trace_tsv(Path::new(&separator_round_input_path), 3, 43)?;
    let exploration_trace = exploration_trace_tsv(&instance)?;
    let exploration_pool_trace = exploration_pool_trace_tsv(&exploration_pool_instance)?;
    let exploration_contained_trace =
        exploration_contained_trace_tsv(&exploration_contained_instance)?;
    let narrow_sampler_trace = uniform_sampler_narrow_interval_reference_tsv(instance.item(0));
    let builder = LBFBuilder::new(
        instance,
        Xoshiro256PlusPlus::seed_from_u64(42),
        sample_config,
    )
    .construct()?;
    let solution = builder.prob.save();
    let placed = solution.layout_snapshot.placed_items.len();
    let remaining = builder.prob.item_demand_qtys.iter().sum::<usize>();
    let feasible = Layout::from_snapshot(&solution.layout_snapshot).is_feasible();
    let output = format!(
        "# generator=Sparrow Rust LBFBuilder; see this crate's Cargo.lock for resolved sources\n\
         # input=RustSppSingleItem.json seed=42 n_container_samples=64 n_focussed_samples=0 n_coord_descents=1\n\
         # fixture seed placed remaining width density feasible\n\
         rust_spp_single_item 42 {placed} {remaining} {:.9} {:.9} {feasible}\n",
        builder.prob.strip_width(),
        builder.prob.density(),
    );

    if let Some(parent) = Path::new(&output_path).parent() {
        fs::create_dir_all(parent)?;
    }
    fs::write(output_path, output)?;
    if let Some(parent) = Path::new(&rng_output_path).parent() {
        fs::create_dir_all(parent)?;
    }
    fs::write(rng_output_path, rng_reference_tsv())?;
    if let Some(parent) = Path::new(&trace_output_path).parent() {
        fs::create_dir_all(parent)?;
    }
    fs::write(trace_output_path, trace_output)?;
    if let Some(parent) = Path::new(&index_output_path).parent() {
        fs::create_dir_all(parent)?;
    }
    fs::write(index_output_path, uniform_index_reference_tsv())?;
    if let Some(parent) = Path::new(&search_trace_output_path).parent() {
        fs::create_dir_all(parent)?;
    }
    fs::write(search_trace_output_path, search_trace)?;
    if let Some(parent) = Path::new(&separator_trace_output_path).parent() {
        fs::create_dir_all(parent)?;
    }
    fs::write(separator_trace_output_path, separator_trace)?;
    if let Some(parent) = Path::new(&separator_round_trace_output_path).parent() {
        fs::create_dir_all(parent)?;
    }
    fs::write(separator_round_trace_output_path, separator_round_trace)?;
    if let Some(parent) = Path::new(&separator_multi_worker_round_trace_output_path).parent() {
        fs::create_dir_all(parent)?;
    }
    fs::write(
        separator_multi_worker_round_trace_output_path,
        separator_multi_worker_round_trace,
    )?;
    if let Some(parent) = Path::new(&separator_three_worker_seed43_round_trace_output_path).parent()
    {
        fs::create_dir_all(parent)?;
    }
    fs::write(
        separator_three_worker_seed43_round_trace_output_path,
        separator_three_worker_seed43_round_trace,
    )?;
    if let Some(parent) = Path::new(&exploration_trace_output_path).parent() {
        fs::create_dir_all(parent)?;
    }
    fs::write(exploration_trace_output_path, exploration_trace)?;
    if let Some(parent) = Path::new(&exploration_pool_trace_output_path).parent() {
        fs::create_dir_all(parent)?;
    }
    fs::write(exploration_pool_trace_output_path, exploration_pool_trace)?;
    if let Some(parent) = Path::new(&exploration_contained_trace_output_path).parent() {
        fs::create_dir_all(parent)?;
    }
    fs::write(
        exploration_contained_trace_output_path,
        exploration_contained_trace,
    )?;
    if let Some(parent) = Path::new(&narrow_sampler_trace_output_path).parent() {
        fs::create_dir_all(parent)?;
    }
    fs::write(narrow_sampler_trace_output_path, narrow_sampler_trace)?;
    Ok(())
}

fn slice_shuffle_reference_tsv() -> String {
    const SEEDS: [u64; 4] = [0, 1, 42, u64::MAX];
    const MAX_LEN: usize = 32;

    let mut output = String::from(
        "# generator=locked Sparrow RustReference rand 0.10.3 SliceRandom::shuffle\n\
         # seed length permutation next_u64_after_shuffle[0..3]\n",
    );
    for seed in SEEDS {
        for length in 0..=MAX_LEN {
            let mut rng = Xoshiro256PlusPlus::seed_from_u64(seed);
            let mut values: Vec<usize> = (0..length).collect();
            values.shuffle(&mut rng);
            let permutation = if values.is_empty() {
                "-".to_owned()
            } else {
                values
                    .iter()
                    .map(usize::to_string)
                    .collect::<Vec<_>>()
                    .join(",")
            };
            let following = [
                rng.next_u64(),
                rng.next_u64(),
                rng.next_u64(),
                rng.next_u64(),
            ];
            output.push_str(&format!(
                "shuffle {seed} {length} {permutation} {:016x} {:016x} {:016x} {:016x}\n",
                following[0], following[1], following[2], following[3]
            ));
        }
    }
    output
}

fn uniform_sampler_continuous_grid_reference_tsv() -> Result<String, Box<dyn Error>> {
    let (external, _) =
        sparrow::util::io::read_spp_input(Path::new("../Fixtures/CollisionLossFloorRust.json"))?;
    let config = DEFAULT_SPARROW_CONFIG;
    let importer = Importer::new(
        config.cde_config,
        config.poly_simpl_tolerance,
        config.narrow_concavity_cutoff_ratio,
    );
    let instance = import_instance(&importer, &external)?;
    let bounds = Rect::try_new(-100.0, -100.0, 100.0, 100.0)
        .expect("continuous sampler bounds must be valid");
    let sampler = UniformBBoxSampler::new(bounds, instance.item(0), bounds)
        .expect("continuous sampler must be constructible");

    let mut seen_angles = Vec::new();
    let mut output = String::from(
        "# generator=locked Sparrow RustReference rand 0.10.3 UniformBBoxSampler::sample\n\
         # input=CollisionLossFloorRust.json item[0], seed=first observed for each continuous angle\n\
         # bounds=sample_bbox=container_bbox=[-100,100]x[-100,100]\n\
         # seed expected_rotation_f32_bits next_u64_after_sample[0..3]\n",
    );
    for seed in 0..10_000_u64 {
        let mut rng = Xoshiro256PlusPlus::seed_from_u64(seed);
        let rotation_bits = sampler.sample(&mut rng).rotation().to_bits();
        if seen_angles.contains(&rotation_bits) {
            continue;
        }
        seen_angles.push(rotation_bits);
        let following = [
            rng.next_u64(),
            rng.next_u64(),
            rng.next_u64(),
            rng.next_u64(),
        ];
        output.push_str(&format!(
            "{seed} {rotation_bits:08x} {:016x} {:016x} {:016x} {:016x}\n",
            following[0], following[1], following[2], following[3]
        ));
        if seen_angles.len() == 16 {
            break;
        }
    }
    if seen_angles.len() != 16 {
        return Err("continuous sampler did not produce all 16 rotation entries".into());
    }
    Ok(output)
}

fn lbf_search_trace_tsv(instance: &jagua_rs::probs::spp::entities::SPInstance) -> String {
    let sample_config = SampleConfig {
        n_container_samples: 16,
        n_focussed_samples: 0,
        n_coord_descents: 2,
    };
    let mut builder = LBFBuilder::new(
        instance.clone(),
        Xoshiro256PlusPlus::seed_from_u64(TRACE_SEED),
        sample_config,
    );
    builder
        .prob
        .change_strip_width(10.0)
        .expect("trace strip width must form a valid container");
    let item = builder.instance.item(0);
    let evaluator = LBFEvaluator::new(&builder.prob.layout, item);
    let mut rng = Xoshiro256PlusPlus::seed_from_u64(TRACE_SEED);
    let mut output = String::from(
        "# generator=Sparrow Rust search_placement with LBFEvaluator\n\
         # input=RustSppSingleItem.json seed=42 container_width=10 n_container_samples=16 n_focussed_samples=0 n_coord_descents=2\n\
         # eval phase ordinal rotation_f32_bits x_f32_bits y_f32_bits kind loss_f32_bits\n\
         # report phase ordinal accepted rotation_f32_bits x_f32_bits y_f32_bits kind loss_f32_bits retained_count\n\
         # keep phase report_ordinal rank rotation_f32_bits x_f32_bits y_f32_bits kind loss_f32_bits\n\
         # result evaluation_count present rotation_f32_bits x_f32_bits y_f32_bits kind loss_f32_bits\n\
         # rng_after next_u64_after_search_hex\n",
    );
    let mut eval_ordinals = [0_usize; 5];
    let mut report_ordinals = [0_usize; 5];
    let _result = search_placement_with_trace(
        &builder.prob.layout,
        item,
        None,
        evaluator,
        sample_config,
        &mut rng,
        &mut |event| match event {
            SearchTraceEvent::Evaluation {
                phase,
                transform,
                evaluation,
            } => {
                let stage = search_phase_name(phase);
                let ordinal = &mut eval_ordinals[search_phase_index(phase)];
                let current = *ordinal;
                *ordinal += 1;
                append_search_trace_row(
                    &mut output,
                    "eval",
                    stage,
                    current,
                    transform,
                    evaluation,
                    None,
                );
            }
            SearchTraceEvent::Report {
                phase,
                transform,
                evaluation,
                accepted,
                retained,
            } => {
                let stage = search_phase_name(phase);
                let ordinal = &mut report_ordinals[search_phase_index(phase)];
                let current = *ordinal;
                *ordinal += 1;
                append_search_trace_row(
                    &mut output,
                    "report",
                    stage,
                    current,
                    transform,
                    evaluation,
                    Some((accepted, retained.len())),
                );
                for (rank, (retained_transform, retained_eval)) in retained.iter().enumerate() {
                    append_search_trace_row(
                        &mut output,
                        "keep",
                        stage,
                        current,
                        *retained_transform,
                        *retained_eval,
                        Some((true, rank)),
                    );
                }
            }
            SearchTraceEvent::Result {
                sample,
                evaluation_count,
            } => match sample {
                Some((transform, evaluation)) => append_search_trace_row(
                    &mut output,
                    "result",
                    "-",
                    evaluation_count,
                    transform,
                    evaluation,
                    None,
                ),
                None => output.push_str(&format!("result {evaluation_count} 0\n")),
            },
        },
    );
    output.push_str(&format!("rng_after {:016x}\n", rng.next_u64()));
    output
}

fn separator_search_trace_tsv(input_path: &Path) -> Result<String, Box<dyn Error>> {
    let (external_instance, _) = sparrow::util::io::read_spp_input(input_path)?;
    let config = DEFAULT_SPARROW_CONFIG;
    let importer = Importer::new(
        config.cde_config,
        config.poly_simpl_tolerance,
        config.narrow_concavity_cutoff_ratio,
    );
    let instance = import_instance(&importer, &external_instance)?;
    let mut problem = SPProblem::new(instance)?;
    problem.change_strip_width(8.0)?;
    let initial_transform = DTransformation::new(0.0, (1.0, 1.0));
    let moving_key = problem.place_item(SPPlacement {
        item_idx: 0,
        d_transf: initial_transform,
    });
    problem.place_item(SPPlacement {
        item_idx: 0,
        d_transf: initial_transform,
    });
    let tracker = CollisionTracker::new(&problem.layout);
    let moving_item = &problem.layout.placed_items[moving_key].item;
    let evaluator = SeparationEvaluator::new(&problem.layout, moving_item, moving_key, &tracker);
    let sample_config = SampleConfig {
        n_container_samples: 16,
        n_focussed_samples: 8,
        n_coord_descents: 2,
    };
    let mut rng = Xoshiro256PlusPlus::seed_from_u64(TRACE_SEED);
    let mut output = String::from(
        "# generator=Sparrow Rust search_placement with SeparationEvaluator\n\
         # input=SeparatorRustTrace.json seed=42 strip_width=8 n_container_samples=16 n_focussed_samples=8 n_coord_descents=2\n\
         # cde=quadtree_depth:4 cd_threshold:64 poles:64@0,16@0.8,8@0.9 fail_fast_area_ratio:0.5\n\
         # eval phase ordinal rotation_f32_bits x_f32_bits y_f32_bits kind loss_f32_bits\n\
         # report phase ordinal accepted rotation_f32_bits x_f32_bits y_f32_bits kind loss_f32_bits retained_count\n\
         # keep phase report_ordinal rank rotation_f32_bits x_f32_bits y_f32_bits kind loss_f32_bits\n\
         # result evaluation_count present rotation_f32_bits x_f32_bits y_f32_bits kind loss_f32_bits\n\
         # rng_after next_u64_after_search_hex\n",
    );
    let mut eval_ordinals = [0_usize; 5];
    let mut report_ordinals = [0_usize; 5];
    let _result = search_placement_with_trace(
        &problem.layout,
        moving_item,
        Some(moving_key),
        evaluator,
        sample_config,
        &mut rng,
        &mut |event| match event {
            SearchTraceEvent::Evaluation {
                phase,
                transform,
                evaluation,
            } => {
                let stage = search_phase_name(phase);
                let ordinal = &mut eval_ordinals[search_phase_index(phase)];
                let current = *ordinal;
                *ordinal += 1;
                append_search_trace_row(
                    &mut output,
                    "eval",
                    stage,
                    current,
                    transform,
                    evaluation,
                    None,
                );
            }
            SearchTraceEvent::Report {
                phase,
                transform,
                evaluation,
                accepted,
                retained,
            } => {
                let stage = search_phase_name(phase);
                let ordinal = &mut report_ordinals[search_phase_index(phase)];
                let current = *ordinal;
                *ordinal += 1;
                append_search_trace_row(
                    &mut output,
                    "report",
                    stage,
                    current,
                    transform,
                    evaluation,
                    Some((accepted, retained.len())),
                );
                for (rank, (retained_transform, retained_eval)) in retained.iter().enumerate() {
                    append_search_trace_row(
                        &mut output,
                        "keep",
                        stage,
                        current,
                        *retained_transform,
                        *retained_eval,
                        Some((true, rank)),
                    );
                }
            }
            SearchTraceEvent::Result {
                sample,
                evaluation_count,
            } => match sample {
                Some((transform, evaluation)) => append_search_trace_row(
                    &mut output,
                    "result",
                    "-",
                    evaluation_count,
                    transform,
                    evaluation,
                    None,
                ),
                None => output.push_str(&format!("result {evaluation_count} 0\n")),
            },
        },
    );
    output.push_str(&format!("rng_after {:016x}\n", rng.next_u64()));
    Ok(output)
}

fn separator_round_trace_tsv(
    input_path: &Path,
    worker_count: usize,
    seed: u64,
) -> Result<String, Box<dyn Error>> {
    let (external_instance, _) = sparrow::util::io::read_spp_input(input_path)?;
    let config = DEFAULT_SPARROW_CONFIG;
    let importer = Importer::new(
        config.cde_config,
        config.poly_simpl_tolerance,
        config.narrow_concavity_cutoff_ratio,
    );
    let instance = import_instance(&importer, &external_instance)?;
    let mut problem = SPProblem::new(instance)?;
    problem.change_strip_width(9.0)?;
    let initial_transform = DTransformation::new(0.0, (1.0, 1.0));
    for _ in 0..9 {
        problem.place_item(SPPlacement {
            item_idx: 0,
            d_transf: initial_transform,
        });
    }

    let mut separator_config: SeparatorConfig = DEFAULT_SPARROW_CONFIG.expl_cfg.separator_config;
    separator_config.iter_no_imprv_limit = 1;
    separator_config.strike_limit = 1;
    separator_config.n_workers = worker_count;
    separator_config.sample_config = SampleConfig {
        n_container_samples: 1,
        n_focussed_samples: 0,
        n_coord_descents: 1,
    };
    let mut separator = Separator::new(
        problem,
        Xoshiro256PlusPlus::seed_from_u64(seed),
        separator_config,
    );
    let mut listener = SeparatorRoundTraceListener::default();
    let terminator = StopAfterOneSeparatorAttempt::default();
    let (solution, _) = separator.separate(&terminator, &mut listener);
    let separation_result = listener
        .result
        .ok_or("Rust separator did not report a run result")?;

    let mut output = format!(
        "# generator=Sparrow Rust Separator::separate with {worker_count} workers\n\
         # input=SeparatorRoundRustTrace.json seed={seed} width=9 strike_limit=1 iter_no_imprv_limit=1 n_workers={worker_count} n_container_samples=1 n_focussed_samples=0 n_coord_descents=1\n\
         # progress ordinal iteration width density min_loss\n\
         # report kind placed_count\n\
         # result success total_evals total_moves iterations\n\
         # state total_loss total_weighted_loss separated feasible\n\
         # placement rank item_index rotation x y item_loss weighted_item_loss\n\
         # pair first_rank second_rank loss weight\n",
    );
    output.push_str(&listener.rows);
    output.push_str(&format!(
        "result {} {} {} {}\n",
        u8::from(separation_result.success),
        separation_result.total_evals,
        separation_result.total_moves,
        separation_result.iterations,
    ));

    let layout = &separator.prob.layout;
    let mut placements = layout
        .placed_items
        .iter()
        .map(|(key, placed_item)| {
            let (x, y) = placed_item.d_transf.translation();
            (
                placed_item.item.idx,
                placed_item.d_transf.rotation(),
                x,
                y,
                separator.ct.get_loss(key),
                separator.ct.get_weighted_loss(key),
                key,
            )
        })
        .collect::<Vec<_>>();
    placements.sort_by(|first, second| {
        first
            .0
            .cmp(&second.0)
            .then_with(|| first.1.total_cmp(&second.1))
            .then_with(|| first.2.total_cmp(&second.2))
            .then_with(|| first.3.total_cmp(&second.3))
    });
    let feasible = Layout::from_snapshot(&solution.layout_snapshot).is_feasible();
    let total_loss = separator.ct.get_total_loss();
    let total_weighted_loss = separator.ct.get_total_weighted_loss();
    output.push_str(&format!(
        "state {total_loss:.9} {total_weighted_loss:.9} {} {}\n",
        u8::from(total_loss == 0.0),
        u8::from(feasible),
    ));
    for (rank, placement) in placements.iter().enumerate() {
        output.push_str(&format!(
            "placement {rank} {} {:.9} {:.9} {:.9} {:.9} {:.9}\n",
            placement.0, placement.1, placement.2, placement.3, placement.4, placement.5,
        ));
    }
    for first_rank in 0..placements.len() {
        for second_rank in first_rank + 1..placements.len() {
            let first_key = placements[first_rank].6;
            let second_key = placements[second_rank].6;
            output.push_str(&format!(
                "pair {first_rank} {second_rank} {:.9} {:.9}\n",
                separator.ct.get_pair_loss(first_key, second_key),
                separator.ct.get_pair_weight(first_key, second_key),
            ));
        }
    }
    Ok(output)
}

fn exploration_trace_tsv(
    instance: &jagua_rs::probs::spp::entities::SPInstance,
) -> Result<String, Box<dyn Error>> {
    let cases = [
        (
            "repeated_shrink",
            6.0_f32,
            0.1_f32,
            2_usize,
            2.0_f32,
            2.0_f32,
            0_usize,
        ),
        (
            "minimum_width",
            4.0_f32,
            0.6_f32,
            2_usize,
            2.0_f32,
            2.0_f32,
            0_usize,
        ),
        (
            "failure_pool_limit",
            6.0_f32,
            0.416_666_66_f32,
            3_usize,
            2.9_f32,
            2.0_f32,
            2_usize,
        ),
    ];
    let mut output = String::from(
        "# generator=Sparrow Rust exploration_phase with a zero-strike separator\n\
         # input=RustSppSingleItem.json item=2x2-square; translation is the Rust centered-item origin\n\
         # case iteration_limit initial_width shrink_step rust_x rust_y failure_limit best_width density feasible bbox_x_min bbox_y_min bbox_x_max bbox_y_max reports progress_count progress_widths...\n",
    );

    for (name, initial_width, shrink_step, iteration_limit, rust_x, rust_y, failure_limit) in cases
    {
        let mut problem = SPProblem::new(instance.clone())?;
        problem.change_strip_width(initial_width)?;
        problem.place_item(SPPlacement {
            item_idx: 0,
            d_transf: DTransformation::new(0.0, (rust_x, rust_y)),
        });

        let mut config: ExplorationConfig = DEFAULT_SPARROW_CONFIG.expl_cfg;
        config.shrink_step = shrink_step;
        config.max_conseq_failed_attempts = (failure_limit > 0).then_some(failure_limit);
        config.separator_config.strike_limit = 0;
        config.separator_config.n_workers = 1;
        let mut separator = Separator::new(
            problem,
            Xoshiro256PlusPlus::seed_from_u64(42),
            config.separator_config,
        );
        let mut listener = ExplorationTraceListener::default();
        let terminator = StopAfterExplorationIterations {
            calls: Cell::new(0),
            iteration_limit,
        };
        let solutions = exploration_phase(&mut separator, &mut listener, &terminator, &config);
        let best = solutions
            .last()
            .ok_or("Rust exploration returned no feasible solution")?;
        let feasible = Layout::from_snapshot(&best.layout_snapshot).is_feasible();
        let placed_item = best
            .layout_snapshot
            .placed_items
            .values()
            .next()
            .ok_or("Rust exploration returned an empty solution")?;
        let bbox = placed_item.shape.bbox;
        output.push_str(&format!(
            "case {name} {iteration_limit} {initial_width:.9} {shrink_step:.9} {rust_x:.9} {rust_y:.9} {failure_limit} {:.9} {:.9} {} {:.9} {:.9} {:.9} {:.9} {} {}",
            best.strip_width(),
            best.density(),
            u8::from(feasible),
            bbox.x_min,
            bbox.y_min,
            bbox.x_max,
            bbox.y_max,
            if listener.reports.is_empty() {
                "none".to_owned()
            } else {
                listener.reports.join(",")
            },
            listener.progress_widths.len(),
        ));
        for width in listener.progress_widths {
            output.push_str(&format!(" {width:.9}"));
        }
        output.push('\n');
    }
    Ok(output)
}

fn exploration_pool_trace_tsv(
    instance: &jagua_rs::probs::spp::entities::SPInstance,
) -> Result<String, Box<dyn Error>> {
    if instance.items.len() != 2 {
        return Err("exploration pool fixture must contain exactly two item types".into());
    }

    let seed = 43_u64;
    let initial_width = 8.0_f32;
    let shrink_step = 0.1875_f32;
    let iteration_limit = 5_usize;
    let pool_stddev = 1.0_f32;
    let mut problem = SPProblem::new(instance.clone())?;
    problem.change_strip_width(initial_width)?;
    problem.place_item(SPPlacement {
        item_idx: 0,
        d_transf: DTransformation::new(0.0, (2.6, 2.0)),
    });
    problem.place_item(SPPlacement {
        item_idx: 1,
        d_transf: DTransformation::new(0.0, (6.5, 2.0)),
    });

    let mut config: ExplorationConfig = DEFAULT_SPARROW_CONFIG.expl_cfg;
    config.shrink_step = shrink_step;
    config.solution_pool_distribution_stddev = pool_stddev;
    config.max_conseq_failed_attempts = None;
    config.separator_config.strike_limit = 0;
    config.separator_config.n_workers = 1;
    let mut separator = Separator::new(
        problem,
        Xoshiro256PlusPlus::seed_from_u64(seed),
        config.separator_config,
    );
    let mut listener = ExplorationPoolTraceListener::default();
    let terminator = StopAfterExplorationIterations {
        calls: Cell::new(0),
        iteration_limit,
    };
    exploration_phase(&mut separator, &mut listener, &terminator, &config);

    let mut output = String::from(
        "# generator=Sparrow Rust exploration_phase; two-item weighted failure-pool trace\n\
         # case seed initial_width shrink_step iteration_limit pool_stddev failures item_count\n\
         # failure ordinal total_loss [item_index x_min y_min x_max y_max]...\n",
    );
    output.push_str(&format!(
        "case weighted_pool {seed} {initial_width:.9} {shrink_step:.9} {iteration_limit} {pool_stddev:.9} {} {}\n",
        listener.failures.len(),
        instance.items.len(),
    ));
    for (ordinal, failure) in listener.failures.iter().enumerate() {
        output.push_str(&format!("failure {ordinal} {:.9}", failure.total_loss));
        for (item_index, bounds) in &failure.placements {
            output.push_str(&format!(
                " {item_index} {:.9} {:.9} {:.9} {:.9}",
                bounds[0], bounds[1], bounds[2], bounds[3],
            ));
        }
        output.push('\n');
    }
    Ok(output)
}

fn exploration_contained_trace_tsv(
    instance: &jagua_rs::probs::spp::entities::SPInstance,
) -> Result<String, Box<dyn Error>> {
    if instance.items.len() != 3 {
        return Err("contained relocation fixture must contain three item types".into());
    }

    let seed = 43_u64;
    let initial_width = 10.0_f32;
    let shrink_step = 0.1_f32;
    let iteration_limit = 3_usize;
    let pool_stddev = 0.25_f32;
    let mut problem = SPProblem::new(instance.clone())?;
    problem.change_strip_width(initial_width)?;
    problem.place_item(SPPlacement {
        item_idx: 0,
        d_transf: DTransformation::new(0.0, (2.5, 2.0)),
    });
    problem.place_item(SPPlacement {
        item_idx: 1,
        d_transf: DTransformation::new(0.0, (6.8, 2.0)),
    });
    problem.place_item(SPPlacement {
        item_idx: 2,
        d_transf: DTransformation::new(0.0, (9.2, 2.0)),
    });

    let mut config: ExplorationConfig = DEFAULT_SPARROW_CONFIG.expl_cfg;
    config.shrink_step = shrink_step;
    config.solution_pool_distribution_stddev = pool_stddev;
    config.max_conseq_failed_attempts = None;
    config.separator_config.strike_limit = 0;
    config.separator_config.n_workers = 1;
    let mut separator = Separator::new(
        problem,
        Xoshiro256PlusPlus::seed_from_u64(seed),
        config.separator_config,
    );
    let mut listener = ExplorationPoolTraceListener::default();
    let terminator = StopAfterExplorationIterations {
        calls: Cell::new(0),
        iteration_limit,
    };
    exploration_phase(&mut separator, &mut listener, &terminator, &config);

    let mut output = concat!(
        "# generator=Sparrow Rust exploration_phase; three-item contained-relocation trace\n",
        "# case seed initial_width shrink_step iteration_limit pool_stddev failures item_count\n",
        "# failure ordinal total_loss [item_index x_min y_min x_max y_max]...\n",
    )
    .to_owned();
    output.push_str(&format!(
        "case contained_relocation {seed} {initial_width:.9} {shrink_step:.9} {iteration_limit} {pool_stddev:.9} {} {}\n",
        listener.failures.len(),
        instance.items.len(),
    ));
    for (ordinal, failure) in listener.failures.iter().enumerate() {
        output.push_str(&format!("failure {ordinal} {:.9}", failure.total_loss));
        for (item_index, bounds) in &failure.placements {
            output.push_str(&format!(
                " {item_index} {:.9} {:.9} {:.9} {:.9}",
                bounds[0], bounds[1], bounds[2], bounds[3],
            ));
        }
        output.push('\n');
    }
    Ok(output)
}

#[derive(Default)]
struct ExplorationPoolTraceListener {
    failures: Vec<ExplorationPoolFailureTrace>,
}

struct ExplorationPoolFailureTrace {
    total_loss: f32,
    placements: Vec<(usize, [f32; 4])>,
}

impl SolutionListener for ExplorationPoolTraceListener {
    fn report(
        &mut self,
        report: ReportType,
        solution: &jagua_rs::probs::spp::entities::SPSolution,
    ) {
        if report != ReportType::ExplInfeas {
            return;
        }

        let layout = Layout::from_snapshot(&solution.layout_snapshot);
        let total_loss = CollisionTracker::new(&layout).get_total_loss();
        let mut placements = solution
            .layout_snapshot
            .placed_items
            .values()
            .map(|placed_item| {
                let bounds = placed_item.shape.bbox;
                (
                    placed_item.item.idx,
                    [bounds.x_min, bounds.y_min, bounds.x_max, bounds.y_max],
                )
            })
            .collect::<Vec<_>>();
        placements.sort_by_key(|placement| placement.0);
        self.failures.push(ExplorationPoolFailureTrace {
            total_loss,
            placements,
        });
    }
}

#[derive(Default)]
struct ExplorationTraceListener {
    reports: Vec<&'static str>,
    progress_widths: Vec<f32>,
}

impl SolutionListener for ExplorationTraceListener {
    fn report(
        &mut self,
        report: ReportType,
        _solution: &jagua_rs::probs::spp::entities::SPSolution,
    ) {
        self.reports.push(match report {
            ReportType::ExplFeas => "expl_feasible",
            ReportType::ExplInfeas => "expl_infeasible",
            ReportType::ExplImproving => "expl_improving",
            ReportType::CmprFeas => "compression_feasible",
            ReportType::Final => "final",
        });
    }

    fn report_separation_progress(&mut self, progress: SeparationProgress) {
        self.progress_widths.push(progress.strip_width);
    }
}

struct StopAfterExplorationIterations {
    calls: Cell<usize>,
    iteration_limit: usize,
}

impl Terminator for StopAfterExplorationIterations {
    fn kill(&self) -> bool {
        let call = self.calls.get();
        self.calls.set(call + 1);
        call >= self.iteration_limit
    }

    fn new_timeout(&mut self, _timeout: Duration) {}

    fn timeout_at(&self) -> Option<jagua_rs::Instant> {
        None
    }
}

#[derive(Default)]
struct SeparatorRoundTraceListener {
    rows: String,
    progress_ordinal: usize,
    result: Option<SeparationResult>,
}

impl SolutionListener for SeparatorRoundTraceListener {
    fn report(
        &mut self,
        report: ReportType,
        solution: &jagua_rs::probs::spp::entities::SPSolution,
    ) {
        let kind = match report {
            ReportType::ExplFeas => "expl_feasible",
            ReportType::ExplInfeas => "expl_infeasible",
            ReportType::ExplImproving => "expl_improving",
            ReportType::CmprFeas => "compression_feasible",
            ReportType::Final => "final",
        };
        self.rows.push_str(&format!(
            "report {kind} {}\n",
            solution.layout_snapshot.placed_items.len(),
        ));
    }

    fn report_separation_progress(&mut self, progress: SeparationProgress) {
        self.rows.push_str(&format!(
            "progress {} {} {:.9} {:.9} {:.9}\n",
            self.progress_ordinal,
            progress.iteration,
            progress.strip_width,
            progress.density,
            progress.min_loss,
        ));
        self.progress_ordinal += 1;
    }

    fn report_separation_result(&mut self, result: SeparationResult) {
        self.result = Some(result);
    }
}

#[derive(Default)]
struct StopAfterOneSeparatorAttempt {
    calls: Cell<usize>,
}

impl Terminator for StopAfterOneSeparatorAttempt {
    fn kill(&self) -> bool {
        let call = self.calls.get();
        self.calls.set(call + 1);
        call > 0
    }

    fn new_timeout(&mut self, _timeout: Duration) {}

    fn timeout_at(&self) -> Option<jagua_rs::Instant> {
        None
    }
}

fn search_phase_index(phase: SearchTracePhase) -> usize {
    match phase {
        SearchTracePhase::ReferencePlacement => 0,
        SearchTracePhase::FocusedSample => 1,
        SearchTracePhase::ContainerSample => 2,
        SearchTracePhase::PreRefine => 3,
        SearchTracePhase::FinalRefine => 4,
    }
}

fn search_phase_name(phase: SearchTracePhase) -> &'static str {
    match phase {
        SearchTracePhase::ReferencePlacement => "reference",
        SearchTracePhase::FocusedSample => "focused",
        SearchTracePhase::ContainerSample => "container",
        SearchTracePhase::PreRefine => "prerefine",
        SearchTracePhase::FinalRefine => "finalrefine",
    }
}

fn append_search_trace_row(
    output: &mut String,
    record: &str,
    phase: &str,
    ordinal: usize,
    transform: DTransformation,
    evaluation: SampleEval,
    report: Option<(bool, usize)>,
) {
    let (kind, loss_bits) = match evaluation {
        SampleEval::Clear { loss } => ("clear", loss.to_bits()),
        SampleEval::Collision { loss } => ("collision", loss.to_bits()),
        SampleEval::Invalid => ("invalid", 0),
    };
    let (x, y) = transform.translation();
    match (record, report) {
        ("report", Some((accepted, retained_count))) => output.push_str(&format!(
            "report {phase} {ordinal} {} {:08x} {:08x} {:08x} {kind} {loss_bits:08x} {retained_count}\n",
            u8::from(accepted),
            transform.rotation().to_bits(),
            x.to_bits(),
            y.to_bits(),
        )),
        ("keep", Some((_, rank))) => output.push_str(&format!(
            "keep {phase} {ordinal} {rank} {:08x} {:08x} {:08x} {kind} {loss_bits:08x}\n",
            transform.rotation().to_bits(),
            x.to_bits(),
            y.to_bits(),
        )),
        ("result", _) => output.push_str(&format!(
            "result {ordinal} 1 {:08x} {:08x} {:08x} {kind} {loss_bits:08x}\n",
            transform.rotation().to_bits(),
            x.to_bits(),
            y.to_bits(),
        )),
        _ => output.push_str(&format!(
            "eval {phase} {ordinal} {:08x} {:08x} {:08x} {kind} {loss_bits:08x}\n",
            transform.rotation().to_bits(),
            x.to_bits(),
            y.to_bits(),
        )),
    }
}

const TRACE_SEED: u64 = 42;
const UNIFORM_SAMPLE_COUNT: usize = 8;

struct TraceEvaluator {
    evaluations: Vec<(DTransformation, SampleEval)>,
}

impl TraceEvaluator {
    fn new() -> Self {
        Self {
            evaluations: Vec::new(),
        }
    }
}

impl SampleEvaluator for TraceEvaluator {
    fn evaluate_sample(
        &mut self,
        transform: DTransformation,
        _upper_bound: Option<SampleEval>,
    ) -> SampleEval {
        let evaluation = SampleEval::Clear {
            loss: trace_loss(transform),
        };
        self.evaluations.push((transform, evaluation));
        evaluation
    }

    fn n_evals(&self) -> usize {
        self.evaluations.len()
    }
}

fn trace_loss(transform: DTransformation) -> f32 {
    let (x, y) = transform.translation();
    (x - 3.0).abs() + 2.0 * (y - 4.0).abs() + 3.0 * (transform.rotation() - 0.2).abs()
}

fn append_trace_row(
    output: &mut String,
    stage: &str,
    ordinal: usize,
    transform: DTransformation,
    evaluation: SampleEval,
) {
    let (x, y) = transform.translation();
    let (kind, loss) = match evaluation {
        SampleEval::Clear { loss } => ("clear", loss),
        SampleEval::Collision { loss } => ("collision", loss),
        SampleEval::Invalid => ("invalid", f32::NAN),
    };
    output.push_str(&format!(
        "{stage} {TRACE_SEED} {ordinal} {:08x} {:08x} {:08x} {kind} {:08x}\n",
        transform.rotation().to_bits(),
        x.to_bits(),
        y.to_bits(),
        loss.to_bits(),
    ));
}

fn sampling_coordinate_descent_reference_tsv(item: &Item) -> String {
    let bounds = Rect::try_new(0.0, 0.0, 10.0, 10.0).expect("trace bounds are valid");
    let sampler = UniformBBoxSampler::new(bounds, item, bounds)
        .expect("trace item must fit the sample bounds");
    let mut output = String::from(
        "# generator=Sparrow Rust UniformBBoxSampler and refine_coord_desc\n\
         # input=RustSppSingleItem.json seed=42 uniform_samples=8\n\
         # stage seed ordinal rotation_f32_bits x_f32_bits y_f32_bits kind loss_f32_bits\n",
    );

    let mut sampling_rng = Xoshiro256PlusPlus::seed_from_u64(TRACE_SEED);
    for ordinal in 0..UNIFORM_SAMPLE_COUNT {
        let transform = sampler.sample(&mut sampling_rng);
        append_trace_row(
            &mut output,
            "sample",
            ordinal,
            transform,
            SampleEval::Clear {
                loss: trace_loss(transform),
            },
        );
    }

    let initial = DTransformation::new(0.5, (5.0, 6.0));
    let initial_evaluation = SampleEval::Clear {
        loss: trace_loss(initial),
    };
    let mut evaluator = TraceEvaluator::new();
    let mut descent_rng = Xoshiro256PlusPlus::seed_from_u64(TRACE_SEED);
    let config = CDConfig {
        t_step_init: 0.5,
        t_step_limit: 0.125,
        r_step_init: 0.25,
        r_step_limit: 0.0625,
        wiggle: true,
    };
    let final_state = refine_coord_desc(
        (initial, initial_evaluation),
        &mut evaluator,
        config,
        &mut descent_rng,
    );
    for (ordinal, (transform, evaluation)) in evaluator.evaluations.iter().enumerate() {
        append_trace_row(&mut output, "coordinate", ordinal, *transform, *evaluation);
    }
    append_trace_row(
        &mut output,
        "result",
        evaluator.n_evals(),
        final_state.0,
        final_state.1,
    );
    output
}

fn uniform_sampler_narrow_interval_reference_tsv(item: &Item) -> String {
    let low = f32::from_bits(0x3f80_0000);
    let high = f32::from_bits(0x3f80_0001);
    let sample_bounds =
        Rect::try_new(low, 1.0, high, 2.0).expect("one-ULP sample bounds are valid");
    let container_bounds =
        Rect::try_new(0.0, 0.0, 10.0, 10.0).expect("narrow-interval container bounds are valid");
    let sampler = UniformBBoxSampler::new(sample_bounds, item, container_bounds)
        .expect("one-ULP sample interval must fit");
    let mut output = String::from(
        "# generator=Sparrow Rust UniformBBoxSampler, one-ULP X interval\n\
         # input=RustSppSingleItem.json seed=42 samples=16\n\
         # sample seed ordinal x_f32_bits y_f32_bits\n\
         # rng_after next_u64_after_samples_hex\n",
    );
    let mut rng = Xoshiro256PlusPlus::seed_from_u64(TRACE_SEED);
    for ordinal in 0..16 {
        let (x, y) = sampler.sample(&mut rng).translation();
        output.push_str(&format!(
            "sample {TRACE_SEED} {ordinal} {:08x} {:08x}\n",
            x.to_bits(),
            y.to_bits(),
        ));
    }
    output.push_str(&format!("rng_after {:016x}\n", rng.next_u64()));
    output
}

fn rng_reference_tsv() -> String {
    let mut output = String::from(
        "# Rust rand 0.10 Xoshiro256++ seed_from_u64 and f32 random_range output\n\
         # seed primitive ordinal decimal_value hex_value_or_float\n",
    );
    let mut raw_rng = Xoshiro256PlusPlus::seed_from_u64(42);
    for ordinal in 0..4 {
        let value = raw_rng.next_u64();
        output.push_str(&format!("42 raw_u64 {ordinal} {value} {value:016x}\n"));
    }

    let mut uniform_rng = Xoshiro256PlusPlus::seed_from_u64(42);
    for ordinal in 0..4 {
        let value = uniform_rng.random_range(0.0_f32..1.0_f32);
        output.push_str(&format!(
            "42 uniform_f32 {ordinal} {:.17} -\n",
            f64::from(value)
        ));
    }
    output
}

fn uniform_index_reference_tsv() -> String {
    let mut output = String::from(
        "# Rust rand 0.10 i32 random_range integer mapping\n\
         # seed bound ordinal value\n",
    );
    for seed in [0_u64, 1, 2, 3, 10, 42] {
        for bound in [1_i32, 2, 3, 4, 6, 16] {
            let mut rng = Xoshiro256PlusPlus::seed_from_u64(seed);
            for ordinal in 0..4 {
                let value = rng.random_range(0..bound);
                output.push_str(&format!("{seed} {bound} {ordinal} {value}\n"));
            }
        }
    }
    output
}
