(function () {
  "use strict";

  const fileInput = document.getElementById("instanceFile");
  const runButton = document.getElementById("runButton");
  const svgButton = document.getElementById("svgButton");
  const status = document.getElementById("status");
  const preview = document.getElementById("preview");
  const output = document.getElementById("output");
  let instanceText = "";
  let previewUrl = "";

  function ClearPreview() {
    if (previewUrl) {
      URL.revokeObjectURL(previewUrl);
      previewUrl = "";
    }
    preview.removeAttribute("src");
    preview.hidden = true;
  }

  fileInput.addEventListener("change", async function () {
    const file = fileInput.files && fileInput.files[0];
    if (!file) {
      instanceText = "";
      runButton.disabled = true;
      svgButton.disabled = true;
      status.textContent = "Waiting for a JSON instance.";
      ClearPreview();
      return;
    }
    instanceText = await file.text();
    runButton.disabled = false;
    status.textContent = "Instance loaded. Ready to optimize.";
    output.textContent = "";
    svgButton.disabled = true;
    ClearPreview();
  });

  // The WASM C ABI reports failures as {"error":"<message>"} payloads. Treat
  // those, and any text that is not valid JSON, as errors instead of showing
  // them as a result.
  function ParseResult(text) {
    if (typeof text !== "string" || text.length === 0) {
      throw new Error("empty result from SparrowWasm");
    }
    let parsed;
    try {
      parsed = JSON.parse(text);
    } catch (parseError) {
      throw new Error("result is not valid JSON: " + parseError.message);
    }
    if (parsed !== null && typeof parsed === "object" && "error" in parsed) {
      throw new Error(String(parsed.error));
    }
    return parsed;
  }

  function HasBridge() {
    return window.SparrowWasm && typeof window.SparrowWasm.optimizeJson === "function";
  }

  // Runs the optimizer exactly once per click and shows the JSON solution.
  runButton.addEventListener("click", function () {
    if (!HasBridge()) {
      status.textContent = "Load the Emscripten bridge exposing SparrowWasm.optimizeJson first.";
      return;
    }
    status.textContent = "Optimizing...";
    ClearPreview();
    svgButton.disabled = true;
    try {
      const solution = ParseResult(window.SparrowWasm.optimizeJson(instanceText));
      output.textContent = JSON.stringify(solution, null, 2);
      svgButton.disabled = typeof window.SparrowWasm.optimizeSvg !== "function";
      status.textContent = "Optimization finished.";
    } catch (error) {
      status.textContent = "Optimization failed.";
      output.textContent = String(error);
    }
  });

  // Explicit, separate request: the bridge re-runs the same deterministic
  // optimization to produce the SVG, so it is kept out of the default path.
  svgButton.addEventListener("click", function () {
    if (!HasBridge() || typeof window.SparrowWasm.optimizeSvg !== "function") {
      status.textContent = "The bridge does not expose SparrowWasm.optimizeSvg.";
      return;
    }
    status.textContent = "Exporting SVG...";
    try {
      const svg = ParseSvgResult(window.SparrowWasm.optimizeSvg(instanceText));
      previewUrl = URL.createObjectURL(new Blob([svg], { type: "image/svg+xml" }));
      preview.src = previewUrl;
      preview.hidden = false;
      status.textContent = "SVG preview ready.";
    } catch (error) {
      status.textContent = "SVG export failed.";
      ClearPreview();
      output.textContent = String(error);
    }
  });

  function ParseSvgResult(text) {
    if (typeof text !== "string" || text.length === 0) {
      throw new Error("empty SVG result from SparrowWasm");
    }
    if (text.charAt(0) === "{") {
      ParseResult(text);
    }
    return text;
  }
}());
