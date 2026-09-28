mod best_samples;
mod coord_descent;
#[cfg(feature = "oracle-trace")]
pub use coord_descent::{CDConfig, refine_coord_desc};
pub mod search;
pub mod uniform_sampler;
