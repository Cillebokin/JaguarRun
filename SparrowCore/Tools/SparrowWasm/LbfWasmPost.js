(() => {
  if (typeof ENVIRONMENT_IS_PTHREAD !== "undefined" && ENVIRONMENT_IS_PTHREAD)
    return;

  const marker = "__jaguar_lbf_raw_serde_exception__";
  const wrap = (publicName) => {
    const run = Module[publicName];
    if (typeof run !== "function")
      throw new Error(`Missing WASM entry point: ${publicName}`);
    Module[publicName] = (...args) => {
      const result = run(...args);
      if (result !== null
          && typeof result === "object"
          && Object.prototype.hasOwnProperty.call(result, marker)) {
        throw result.value;
      }
      return result;
    };
  };

  wrap("run_lbf_spp_wasm");
  wrap("run_lbf_bpp_wasm");
})();
