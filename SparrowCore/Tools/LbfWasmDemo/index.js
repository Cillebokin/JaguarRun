const createLbfModule = globalThis.LbfModule;

const status = document.getElementById("status");
const resultBox = document.getElementById("result");
const svgBox = document.getElementById("svgs");
const solveButton = document.getElementById("solve-button");

async function readJson(fileInput) {
  const file = fileInput.files?.[0];
  if (!file) return null;
  return JSON.parse(await file.text());
}

function showSvgs(entries) {
  svgBox.replaceChildren();
  for (const [filename, source] of entries) {
    const section = document.createElement("section");
    section.className = "svg-result";
    const heading = document.createElement("h3");
    heading.textContent = filename;
    section.append(heading);
    const parsed = new DOMParser().parseFromString(source, "image/svg+xml");
    if (parsed.querySelector("parsererror")) {
      throw new Error(`Invalid SVG returned for ${filename}`);
    }
    section.append(document.importNode(parsed.documentElement, true));
    svgBox.append(section);
  }
}

let lbfModule;
try {
  status.textContent = "Checking WebAssembly thread support…";
  if (typeof createLbfModule !== "function") {
    throw new Error("LBF WebAssembly module script did not load");
  }
  if (!crossOriginIsolated) {
    throw new Error("This demo needs a local server with COOP/COEP headers; start it with python serve.py");
  }
  status.textContent = "Initializing WebAssembly module…";
  lbfModule = await createLbfModule({
    locateFile: (path) => new URL(path, new URL("./build/", window.location.href)).href,
  });
  lbfModule.init_logger_wasm();
  status.textContent = "Ready";
} catch (error) {
  status.textContent = error instanceof Error ? error.message : String(error);
  solveButton.disabled = true;
}

solveButton.addEventListener("click", async () => {
  if (!lbfModule) return;
  solveButton.disabled = true;
  resultBox.textContent = "Solving…";
  svgBox.replaceChildren();
  try {
    const instance = await readJson(document.getElementById("instance-file"));
    const config = await readJson(document.getElementById("config-file"));
    const problem = document.getElementById("problem-type").value;
    const start = performance.now();
    const result = problem === "bpp"
      ? lbfModule.run_lbf_bpp_wasm(instance, config)
      : lbfModule.run_lbf_spp_wasm(instance, config);
    if (result?.error) throw new Error(result.error);
    resultBox.textContent = JSON.stringify(result.output, null, 2);
    showSvgs(result.svgs);
    status.textContent = `Solved in ${result.solve_time_ms} ms (wall ${Math.round(performance.now() - start)} ms)`;
  } catch (error) {
    resultBox.textContent = "No solution was produced.";
    status.textContent = error instanceof Error ? error.message : String(error);
  } finally {
    solveButton.disabled = false;
  }
});

document.getElementById("clear-button").addEventListener("click", () => {
  resultBox.textContent = "No result yet.";
  svgBox.replaceChildren();
  status.textContent = lbfModule ? "Ready" : status.textContent;
});
