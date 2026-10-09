use crate::FMT;
use crate::config::ExplorationConfig;
use crate::optimizer::separator::{Separator, SeparatorConfig};
use crate::sample::uniform_sampler::convert_sample_to_closest_feasible;
use crate::util::listener::{ReportType, SolutionListener};
use crate::util::terminator::Terminator;
use float_cmp::approx_eq;
use itertools::Itertools;
use jagua_rs::collision_detection::hazards::HazardEntity;
use jagua_rs::collision_detection::hazards::collector::BasicHazardCollector;
use jagua_rs::entities::{Layout, PItemKey};
use jagua_rs::geometry::DTransformation;
use jagua_rs::geometry::geo_traits::CollidesWith;
use jagua_rs::probs::spp::entities::SPSolution;
use log::{debug, info, warn};
use ordered_float::OrderedFloat;
use rand::Rng;
use rand::prelude::{Distribution, IteratorRandom};
use rand_distr::Normal;
use std::cmp::Reverse;

/// Algorithm 12 from https://doi.org/10.48550/arXiv.2509.13329
#[cfg(feature = "oracle-trace")]
#[derive(Clone, Debug, PartialEq)]
pub enum ExplorationTraceEvent {
    PoolInsert {
        failure_id: usize,
        insertion_index: usize,
        pool_order: Vec<usize>,
    },
    PoolSample {
        failure_id: usize,
        raw_sample: f32,
        mapped_sample: f32,
        selected_index: usize,
        consumed_u64_words: usize,
        first_layer_index: u8,
        following_u64: [u64; 4],
    },
    DisruptionMove {
        failure_id: usize,
        kind: DisruptionMoveKind,
        item_index: usize,
        related_item_index: usize,
        from: DTransformation,
        to: DTransformation,
    },
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DisruptionMoveKind {
    Swap,
    ContainedRelocation,
}

#[cfg_attr(not(feature = "oracle-trace"), allow(dead_code))]
#[derive(Clone, Debug)]
enum InternalTraceEvent {
    PoolInsert {
        failure_id: usize,
        insertion_index: usize,
        pool_order: Vec<usize>,
    },
    PoolSample {
        failure_id: usize,
        raw_sample: f32,
        mapped_sample: f32,
        selected_index: usize,
        consumed_u64_words: usize,
        first_layer_index: u8,
        following_u64: [u64; 4],
    },
    DisruptionMove {
        failure_id: usize,
        kind: DisruptionMoveKind,
        item_index: usize,
        related_item_index: usize,
        from: DTransformation,
        to: DTransformation,
    },
}

#[cfg(feature = "oracle-trace")]
impl From<InternalTraceEvent> for ExplorationTraceEvent {
    fn from(event: InternalTraceEvent) -> Self {
        match event {
            InternalTraceEvent::PoolInsert {
                failure_id,
                insertion_index,
                pool_order,
            } => Self::PoolInsert {
                failure_id,
                insertion_index,
                pool_order,
            },
            InternalTraceEvent::PoolSample {
                failure_id,
                raw_sample,
                mapped_sample,
                selected_index,
                consumed_u64_words,
                first_layer_index,
                following_u64,
            } => Self::PoolSample {
                failure_id,
                raw_sample,
                mapped_sample,
                selected_index,
                consumed_u64_words,
                first_layer_index,
                following_u64,
            },
            InternalTraceEvent::DisruptionMove {
                failure_id,
                kind,
                item_index,
                related_item_index,
                from,
                to,
            } => Self::DisruptionMove {
                failure_id,
                kind,
                item_index,
                related_item_index,
                from,
                to,
            },
        }
    }
}

pub fn exploration_phase(
    sep: &mut Separator,
    sol_listener: &mut impl SolutionListener,
    term: &impl Terminator,
    config: &ExplorationConfig,
) -> Vec<SPSolution> {
    exploration_phase_impl(sep, sol_listener, term, config, None)
}

#[cfg(feature = "oracle-trace")]
pub fn exploration_phase_with_trace<F: FnMut(ExplorationTraceEvent)>(
    sep: &mut Separator,
    sol_listener: &mut impl SolutionListener,
    term: &impl Terminator,
    config: &ExplorationConfig,
    mut observer: F,
) -> Vec<SPSolution> {
    let mut forward = |event: InternalTraceEvent| observer(event.into());
    exploration_phase_impl(sep, sol_listener, term, config, Some(&mut forward))
}

fn exploration_phase_impl(
    sep: &mut Separator,
    sol_listener: &mut impl SolutionListener,
    term: &impl Terminator,
    config: &ExplorationConfig,
    mut observer: Option<&mut dyn FnMut(InternalTraceEvent)>,
) -> Vec<SPSolution> {
    let min_width = super::minimum_strip_width(&sep.prob);
    let mut current_width = sep.prob.strip_width();
    let mut best_width = current_width;

    let mut feasible_sols = vec![sep.prob.save()];

    sol_listener.report(ReportType::ExplFeas, &feasible_sols[0]);
    info!(
        "[EXPL] starting optimization with initial width: {:.3} ({:.3}%)",
        current_width,
        sep.prob.density() * 100.0
    );

    let mut infeas_sol_pool: Vec<(SPSolution, f32)> = vec![];
    let mut trace_pool_order = Vec::new();
    let mut next_failure_id = 0_usize;

    while !term.kill() {
        // Attempt to separate the current layout
        let local_best = sep.separate(term, sol_listener);
        let total_loss = local_best.1.get_total_loss();

        if total_loss == 0.0 {
            // If successfully separated
            if current_width < best_width {
                info!(
                    "[EXPL] feasible solution found! (width: {:.3}, dens: {:.3}%)",
                    current_width,
                    sep.prob.density() * 100.0
                );
                best_width = current_width;
                feasible_sols.push(local_best.0.clone());
                sol_listener.report(ReportType::ExplFeas, &local_best.0);
            }
            // Shrink the strip width and clear the infeasible solution pool
            let next_width = current_width * (1.0 - config.shrink_step);
            if next_width < min_width || next_width >= current_width {
                info!("[EXPL] stopping at minimum strip width: {:.3}", min_width);
                break;
            }
            info!(
                "[EXPL] shrinking strip by {}%: {:.3} -> {:.3}",
                config.shrink_step * 100.0,
                current_width,
                next_width
            );
            sep.change_strip_width(next_width, None);
            current_width = next_width;
            infeas_sol_pool.clear();
            trace_pool_order.clear();
        } else {
            info!(
                "[EXPL] unable to reach feasibility (width: {:.3}, dens: {:.3}%, min loss: {})",
                current_width,
                sep.prob.density() * 100.0,
                FMT().fmt2(total_loss)
            );
            sol_listener.report(ReportType::ExplInfeas, &local_best.0);

            // Separation was not successful add it to the pool of infeasible solutions
            let insertion_index = match infeas_sol_pool
                .binary_search_by(|(_, o)| o.partial_cmp(&total_loss).unwrap())
            {
                Ok(idx) | Err(idx) => {
                    infeas_sol_pool.insert(idx, (local_best.0.clone(), total_loss));
                    idx
                }
            };
            let failure_id = next_failure_id;
            next_failure_id += 1;
            if let Some(trace_observer) = observer.as_deref_mut() {
                trace_pool_order.insert(insertion_index, failure_id);
                trace_observer(InternalTraceEvent::PoolInsert {
                    failure_id,
                    insertion_index,
                    pool_order: trace_pool_order.clone(),
                });
            }

            if infeas_sol_pool.len() >= config.max_conseq_failed_attempts.unwrap_or(usize::MAX) {
                info!(
                    "[EXPL] max consecutive failed attempts ({}), terminating",
                    infeas_sol_pool.len()
                );
                break;
            }

            // Restore to a random solution from the pool, with better solutions having more chance to be selected
            let selected_sol = {
                // Sample a value in range [0.0, 1.0[ from a normal distribution
                let distribution =
                    Normal::new(0.0, config.solution_pool_distribution_stddev).unwrap();
                let rng_before = observer.as_ref().map(|_| sep.rng.clone());
                let raw_sample = distribution.sample(&mut sep.rng);
                let sample = raw_sample.abs().min(0.999);
                // Map it to an index in the infeasible solution pool (better solutions are at the start of the pool)
                let selected_idx = (sample * infeas_sol_pool.len() as f32) as usize;

                if let (Some(rng_before), Some(trace_observer)) =
                    (rng_before, observer.as_deref_mut())
                {
                    let first_layer_index = (rng_before.clone().next_u64() & 0xff) as u8;
                    let consumed_u64_words = consumed_u64_words(&rng_before, &sep.rng);
                    let mut following = sep.rng.clone();
                    let following_u64 = std::array::from_fn(|_| following.next_u64());
                    trace_observer(InternalTraceEvent::PoolSample {
                        failure_id,
                        raw_sample,
                        mapped_sample: sample,
                        selected_index: selected_idx,
                        consumed_u64_words,
                        first_layer_index,
                        following_u64,
                    });
                }

                let (selected_sol, loss) = &infeas_sol_pool[selected_idx];
                info!(
                    "[EXPL] starting solution {}/{} selected from solution pool (l: {}) to disrupt",
                    selected_idx,
                    infeas_sol_pool.len(),
                    FMT().fmt2(*loss)
                );
                selected_sol
            };

            // Rollback to this solution and disrupt it.
            sep.rollback(selected_sol, None);
            disrupt_solution(sep, config, failure_id, &mut observer);
        }
    }

    info!(
        "[EXPL] finished, best feasible solution: width: {:.3} ({:.3}%)",
        best_width,
        feasible_sols.last().unwrap().density() * 100.0
    );

    feasible_sols
}

fn consumed_u64_words(
    before: &rand::rngs::Xoshiro256PlusPlus,
    after: &rand::rngs::Xoshiro256PlusPlus,
) -> usize {
    let mut probe = before.clone();
    for words in 0..=64 {
        if &probe == after {
            return words;
        }
        probe.next_u64();
    }
    panic!("normal sample consumed more than 64 u64 words");
}

fn disrupt_solution(
    sep: &mut Separator,
    config: &ExplorationConfig,
    failure_id: usize,
    observer: &mut Option<&mut dyn FnMut(InternalTraceEvent)>,
) {
    if sep.prob.layout.placed_items.len() < 2 {
        warn!("[DSRP] cannot disrupt solution with less than 2 items");
        return;
    }

    // The general idea is to disrupt a solution by swapping two 'large' items in the layout.
    // 'Large' items are those whose convex hull area falls within a certain top percentile
    // of the total convex hull area of all items in the layout.

    // Step 1: Define what constitutes a 'large' item.

    // Calculate the total convex hull area of all items, considering quantities.
    let total_convex_hull_area: f32 = sep
        .prob
        .instance
        .items
        .iter()
        .map(|(item, quantity)| item.shape_cd.surrogate().convex_hull_area * (*quantity as f32))
        .sum();

    let cutoff_threshold_area =
        total_convex_hull_area * config.large_item_ch_area_cutoff_percentile;

    // Sort items by convex hull area in descending order.
    let sorted_items_by_ch_area = sep
        .prob
        .instance
        .items
        .iter()
        .sorted_by_key(|(item, _)| {
            Reverse(OrderedFloat(item.shape_cd.surrogate().convex_hull_area))
        })
        .peekable();

    let mut cumulative_ch_area = 0.0;
    let mut ch_area_cutoff = 0.0;

    // Iterate through items, accumulating their convex hull areas until the cumulative sum
    // exceeds the cutoff_threshold_area. The convex hull area of the item that causes
    // this excess becomes the ch_area_cutoff.
    for (item, quantity) in sorted_items_by_ch_area {
        let item_ch_area = item.shape_cd.surrogate().convex_hull_area;
        cumulative_ch_area += item_ch_area * (*quantity as f32);
        if cumulative_ch_area > cutoff_threshold_area {
            ch_area_cutoff = item_ch_area;
            debug!(
                "[DSRP] cutoff ch area: {}, for item idx: {}, bbox: {:?}",
                ch_area_cutoff, item.idx, item.shape_cd.bbox
            );
            break;
        }
    }

    // Step 2: Select two 'large' items and 'swap' them.

    let large_items = sep
        .prob
        .layout
        .placed_items
        .iter()
        .filter(|(_, pi)| pi.shape.surrogate().convex_hull_area >= ch_area_cutoff);

    //Choose a first item with a large enough convex hull
    let (pk1, pi1) = large_items
        .clone()
        .choose(&mut sep.rng)
        .expect("[DSRP] failed to choose first item");

    //Choose a second item with a large enough convex hull and different enough from the first.
    //If no such item is found, choose a random one.
    let (pk2, pi2) = large_items.clone()
        .filter(|(_, pi)|
            // Ensure the second item is different from the first
            !approx_eq!(f32, pi.shape.area,pi1.shape.area, epsilon = pi1.shape.area * 0.01) &&
                !approx_eq!(f32, pi.shape.diameter, pi1.shape.diameter, epsilon = pi1.shape.diameter * 0.01)
        )
        .choose(&mut sep.rng)
        .or_else(|| {
            sep.prob.layout.placed_items.iter()
                .filter(|(pk, _)| *pk != pk1) // Ensure the second item is not the same as the first
                .choose(&mut sep.rng)
        }) // As a fallback, choose any item
        .expect("[EXPL] failed to choose second item for disruption");

    // Step 3: Swap the two items' positions in the layout.

    let dt1_old = pi1.d_transf;
    let dt2_old = pi2.d_transf;
    let item1_idx = pi1.item.idx;
    let item2_idx = pi2.item.idx;

    // Make sure the swaps do not violate feasibility (rotation).
    let dt1_new = convert_sample_to_closest_feasible(dt2_old, &pi1.item);
    let dt2_new = convert_sample_to_closest_feasible(dt1_old, &pi2.item);

    info!(
        "[EXPL] disrupting by swapping two large items (id: {} <-> {})",
        pi1.item.idx, pi2.item.idx
    );

    let pk1 = sep.move_item(pk1, dt1_new);
    if let Some(trace_observer) = observer.as_deref_mut() {
        trace_observer(InternalTraceEvent::DisruptionMove {
            failure_id,
            kind: DisruptionMoveKind::Swap,
            item_index: item1_idx,
            related_item_index: item2_idx,
            from: dt1_old,
            to: dt1_new,
        });
    }
    let pk2 = sep.move_item(pk2, dt2_new);
    if let Some(trace_observer) = observer.as_deref_mut() {
        trace_observer(InternalTraceEvent::DisruptionMove {
            failure_id,
            kind: DisruptionMoveKind::Swap,
            item_index: item2_idx,
            related_item_index: item1_idx,
            from: dt2_old,
            to: dt2_new,
        });
    }

    // Step 4: Move all items that are practically contained by one of the swapped items to the "empty space" created by the moved item.
    //         This is particularly important when huge items are swapped with smaller items.
    //         The huge item will create a large empty space and many of the items which previously
    //         surrounded the smaller one will be contained by the huge one.
    {
        // transformation to convert the contained items' position (relative to the old and new positions of the swapped items)
        let converting_transformation = dt1_new.compose().inverse().transform(&dt1_old.compose());

        for c1_pk in practically_contained_items(&sep.prob.layout, pk1)
            .into_iter()
            .filter(|c1_pk| *c1_pk != pk2)
        {
            let c1_pi = &sep.prob.layout.placed_items[c1_pk];
            let from = c1_pi.d_transf;
            let item_index = c1_pi.item.idx;

            let new_dt = c1_pi
                .d_transf
                .compose()
                .transform(&converting_transformation)
                .decompose();

            //Ensure the sure the new position is feasible
            let new_feasible_dt = convert_sample_to_closest_feasible(new_dt, &c1_pi.item);
            sep.move_item(c1_pk, new_feasible_dt);
            if let Some(trace_observer) = observer.as_deref_mut() {
                trace_observer(InternalTraceEvent::DisruptionMove {
                    failure_id,
                    kind: DisruptionMoveKind::ContainedRelocation,
                    item_index,
                    related_item_index: item1_idx,
                    from,
                    to: new_feasible_dt,
                });
            }
        }
    }

    // Do the same for the second item, but using the second transformation
    {
        let converting_transformation = dt2_new.compose().inverse().transform(&dt2_old.compose());

        for c2_pk in practically_contained_items(&sep.prob.layout, pk2)
            .into_iter()
            .filter(|c2_pk| *c2_pk != pk1)
        {
            let c2_pi = &sep.prob.layout.placed_items[c2_pk];
            let from = c2_pi.d_transf;
            let item_index = c2_pi.item.idx;
            let new_dt = c2_pi
                .d_transf
                .compose()
                .transform(&converting_transformation)
                .decompose();

            //make sure the new position is feasible
            let new_feasible_dt = convert_sample_to_closest_feasible(new_dt, &c2_pi.item);
            sep.move_item(c2_pk, new_feasible_dt);
            if let Some(trace_observer) = observer.as_deref_mut() {
                trace_observer(InternalTraceEvent::DisruptionMove {
                    failure_id,
                    kind: DisruptionMoveKind::ContainedRelocation,
                    item_index,
                    related_item_index: item2_idx,
                    from,
                    to: new_feasible_dt,
                });
            }
        }
    }
}

/// Collects all items which point of inaccessibility (POI) is contained by pk_c's shape.
fn practically_contained_items(layout: &Layout, pk_c: PItemKey) -> Vec<PItemKey> {
    let pi_c = &layout.placed_items[pk_c];
    // Detect all collisions with the item pk_c's shape.
    let mut collector = BasicHazardCollector::new();
    layout
        .cde()
        .collect_poly_collisions(&pi_c.shape, &mut collector);

    // Filter out the items that have their POI contained by pk_c's shape.
    collector
        .iter()
        .filter_map(|(_, he)| match he {
            HazardEntity::PlacedItem { pk, .. } => Some(*pk),
            _ => None,
        })
        .filter(|pk| *pk != pk_c) // Ensure we don't include the item itself
        .filter(|pk| {
            // Check if the POI of the item is contained by pk_c's shape
            let poi = layout.placed_items[*pk].shape.poi;
            pi_c.shape.collides_with(&poi.center)
        })
        .collect_vec()
}
