(function () {
  "use strict";

  const fileInput = document.getElementById("instanceFile");
  const runButton = document.getElementById("runButton");
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
      status.textContent = "Waiting for a JSON instance.";
      ClearPreview();
      return;
    }
    instanceText = await file.text();
    runButton.disabled = false;
    status.textContent = "Instance loaded. Ready to optimize.";
    output.textContent = "";
    ClearPreview();
  });

  runButton.addEventListener("click", function () {
    if (!window.SparrowWasm || typeof window.SparrowWasm.optimizeJson !== "function") {
      status.textContent = "Load the Emscripten bridge exposing SparrowWasm.optimizeJson first.";
      return;
    }
    status.textContent = "Optimizing...";
    try {
      const result = window.SparrowWasm.optimizeJson(instanceText);
      output.textContent = JSON.stringify(JSON.parse(result), null, 2);
      if (typeof window.SparrowWasm.optimizeSvg === "function") {
        const svg = window.SparrowWasm.optimizeSvg(instanceText);
        previewUrl = URL.createObjectURL(new Blob([svg], { type: "image/svg+xml" }));
        preview.src = previewUrl;
        preview.hidden = false;
      }
      status.textContent = "Optimization finished.";
    } catch (error) {
      status.textContent = "Optimization failed.";
      ClearPreview();
      output.textContent = String(error);
    }
  });
}());
