(function () {
  "use strict";

  const fileInput = document.getElementById("instanceFile");
  const runButton = document.getElementById("runButton");
  const cancelButton = document.getElementById("cancelButton");
  const status = document.getElementById("status");
  const phase = document.getElementById("phase");
  const elapsed = document.getElementById("elapsed");
  const timeProgress = document.getElementById("timeProgress");
  const metrics = document.getElementById("metrics");
  const eventProgress = document.getElementById("eventProgress");
  const preview = document.getElementById("preview");
  const output = document.getElementById("output");
  const logEntries = document.getElementById("logEntries");
  const appScriptUrl = document.currentScript
    ? document.currentScript.src
    : window.location.href;
  const workerUrl = new URL("./SparrowWorker.js", appScriptUrl);
  let instanceText = "";
  let previewUrl = "";
  let activeWorker = null;
  let tickTimer = 0;
  let runStartedAt = 0;
  let phaseStartedAt = 0;
  let phaseBudgetSeconds = 1;

  function ClearPreview() {
    if (previewUrl) {
      URL.revokeObjectURL(previewUrl);
      previewUrl = "";
    }
    preview.removeAttribute("src");
    preview.hidden = true;
  }

  function StopTimer() {
    if (tickTimer) {
      window.clearInterval(tickTimer);
      tickTimer = 0;
    }
  }

  function ResetRunControls() {
    StopTimer();
    activeWorker = null;
    fileInput.disabled = false;
    runButton.disabled = instanceText.length === 0;
    cancelButton.disabled = true;
  }

  function AppendLog(level, message) {
    const entry = document.createElement("li");
    const timestamp = new Date().toLocaleTimeString();
    entry.textContent = "[" + timestamp + "] [" + level + "] " + message;
    logEntries.appendChild(entry);
    while (logEntries.childElementCount > 512) {
      logEntries.removeChild(logEntries.firstElementChild);
    }
    logEntries.scrollTop = logEntries.scrollHeight;
  }

  function PhaseName(value) {
    return ["Construction", "Exploration", "Compression", "Complete"][value]
      || "Unknown phase";
  }

  function SetPhase(value, elapsedSeconds) {
    phase.textContent = PhaseName(value);
    phaseStartedAt = performance.now();
    if (value === 1) {
      phaseBudgetSeconds = 540;
    } else if (value === 2) {
      phaseBudgetSeconds = 60;
    } else {
      phaseBudgetSeconds = 1;
    }
    timeProgress.max = phaseBudgetSeconds;
    timeProgress.value = 0;
    if (typeof elapsedSeconds === "number") {
      elapsed.textContent = elapsedSeconds.toFixed(1) + " s elapsed";
    }
  }

  function UpdateClock() {
    if (!runStartedAt) {
      return;
    }
    const totalSeconds = (performance.now() - runStartedAt) / 1000;
    elapsed.textContent = totalSeconds.toFixed(1) + " s elapsed";
    const phaseSeconds = Math.max(0, (performance.now() - phaseStartedAt) / 1000);
    timeProgress.value = Math.min(phaseSeconds, phaseBudgetSeconds);
  }

  function ReportName(value) {
    return [
      "Exploration feasible",
      "Exploration infeasible",
      "Exploration improving",
      "Compression feasible",
      "Final"
    ][value] || "Solution report";
  }

  function HandleProgress(event) {
    if (event.kind === 1) {
      SetPhase(event.phase, event.elapsedSeconds);
      AppendLog("INFO", "Entered " + PhaseName(event.phase) + " phase.");
      return;
    }

    if (event.kind === 2) {
      metrics.textContent =
        "Width " + event.stripWidth.toFixed(3)
        + " · density " + (event.density * 100).toFixed(3) + "%"
        + " · " + (event.feasible ? "feasible" : "infeasible")
        + " · collision loss " + event.collisionLoss;
      eventProgress.textContent = ReportName(event.report)
        + " · iteration " + event.iteration;
      if (event.svg) {
        ClearPreview();
        previewUrl = URL.createObjectURL(
          new Blob([event.svg], { type: "image/svg+xml" })
        );
        preview.src = previewUrl;
        preview.hidden = false;
      }
      AppendLog("INFO", ReportName(event.report)
        + ": width=" + event.stripWidth.toFixed(3)
        + ", density=" + (event.density * 100).toFixed(3) + "%."
      );
      if (event.phase === 3) {
        phase.textContent = "Complete";
      }
      return;
    }

    if (event.kind === 3) {
      eventProgress.textContent = "Separation iteration " + event.iteration
        + " · minimum loss " + event.collisionLoss
        + " · width " + event.stripWidth.toFixed(3);
      metrics.textContent = "Width " + event.stripWidth.toFixed(3)
        + " · density " + (event.density * 100).toFixed(3) + "%";
      return;
    }

    if (event.kind === 4) {
      const moves = event.value;
      const rate = event.durationSeconds > 0
        ? (event.count / event.durationSeconds).toFixed(1)
        : "n/a";
      const message = (event.report ? "Successful" : "Failed")
        + " separation: " + event.count + " evaluations, " + moves
        + " moves, " + event.iteration + " iterations (" + rate + " eval/s).";
      eventProgress.textContent = message;
      AppendLog("INFO", message);
      return;
    }

    if (event.kind === 5) {
      eventProgress.textContent = "Compression shrink step " + event.value;
      AppendLog("INFO", "Compression shrink step " + event.value + ".");
    }
  }

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
    if (parsed === null || typeof parsed !== "object" || "error" in parsed) {
      throw new Error(parsed && parsed.error
        ? String(parsed.error)
        : "result does not contain a solution object");
    }
    return parsed;
  }

  function FinishWithError(message) {
    ResetRunControls();
    status.textContent = "Optimization failed.";
    output.textContent = String(message);
    AppendLog("ERROR", String(message));
  }

  fileInput.addEventListener("change", async function () {
    const file = fileInput.files && fileInput.files[0];
    if (!file) {
      instanceText = "";
      runButton.disabled = true;
      status.textContent = "Waiting for a JSON instance.";
      output.textContent = "";
      metrics.textContent = "No solution reports yet.";
      eventProgress.textContent = "Waiting for an optimization run.";
      ClearPreview();
      return;
    }
    try {
      instanceText = await file.text();
      runButton.disabled = instanceText.length === 0 || activeWorker !== null;
      status.textContent = "Instance loaded. Ready to optimize.";
      output.textContent = "";
      metrics.textContent = "No solution reports yet.";
      eventProgress.textContent = "Ready.";
      logEntries.replaceChildren();
      ClearPreview();
    } catch (error) {
      instanceText = "";
      runButton.disabled = true;
      status.textContent = "Unable to read the selected file.";
      output.textContent = String(error);
    }
  });

  runButton.addEventListener("click", function () {
    if (!instanceText || activeWorker !== null) {
      return;
    }
    try {
      activeWorker = new Worker(workerUrl);
    } catch (error) {
      FinishWithError("Unable to start the Sparrow worker: " + error);
      return;
    }

    runStartedAt = performance.now();
    phaseStartedAt = runStartedAt;
    phaseBudgetSeconds = 600;
    timeProgress.max = phaseBudgetSeconds;
    timeProgress.value = 0;
    phase.textContent = "Starting";
    elapsed.textContent = "0.0 s elapsed";
    eventProgress.textContent = "Loading optimizer…";
    metrics.textContent = "Waiting for the first solution report.";
    output.textContent = "";
    logEntries.replaceChildren();
    ClearPreview();
    status.textContent = "Optimizing in a background worker…";
    fileInput.disabled = true;
    runButton.disabled = true;
    cancelButton.disabled = false;
    AppendLog("INFO", "Optimization started.");
    tickTimer = window.setInterval(UpdateClock, 100);

    const workerForRun = activeWorker;
    workerForRun.onmessage = function (message) {
      if (workerForRun !== activeWorker) {
        return;
      }
      const event = message.data || {};
      if (event.type === "sparrow-progress") {
        HandleProgress(event);
        return;
      }
      if (event.type === "error") {
        FinishWithError(event.message || "Unknown SparrowWasm worker error.");
        workerForRun.terminate();
        return;
      }
      if (event.type === "result") {
        try {
          const solution = ParseResult(event.json);
          if (typeof event.svg !== "string" || event.svg.length === 0) {
            throw new Error("SparrowWasm returned an empty SVG result.");
          }
          output.textContent = JSON.stringify(solution, null, 2);
          ClearPreview();
          previewUrl = URL.createObjectURL(
            new Blob([event.svg], { type: "image/svg+xml" })
          );
          preview.src = previewUrl;
          preview.hidden = false;
          ResetRunControls();
          status.textContent = "Optimization finished.";
          phase.textContent = "Complete";
          eventProgress.textContent = "Final JSON and SVG came from the same Optimize run.";
          AppendLog("INFO", "Optimization finished; final JSON and SVG are ready.");
        } catch (error) {
          FinishWithError(error);
        }
        workerForRun.terminate();
      }
    };
    workerForRun.onerror = function (error) {
      if (workerForRun === activeWorker) {
        FinishWithError(error.message || "SparrowWasm worker stopped unexpectedly.");
        workerForRun.terminate();
      }
    };
    workerForRun.postMessage({ type: "run", inputJson: instanceText });
  });

  cancelButton.addEventListener("click", function () {
    if (!activeWorker) {
      return;
    }
    activeWorker.terminate();
    activeWorker = null;
    ResetRunControls();
    ClearPreview();
    status.textContent = "Optimization cancelled; no final result was produced.";
    eventProgress.textContent = "The browser worker was terminated.";
    AppendLog("WARN", "Optimization cancelled by terminating the browser worker.");
  });
}());
