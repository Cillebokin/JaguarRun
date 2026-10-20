"use strict";

try {
  importScripts("./sparrow_wasm.js");
} catch (error) {
  self.postMessage({
    type: "error",
    message: "Unable to load sparrow_wasm.js. Build the Emscripten target and place its generated JS/WASM files beside the Demo files."
  });
}

let modulePromise = null;

function GetModule() {
  if (!modulePromise) {
    if (typeof self.SparrowWasmModule !== "function") {
      throw new Error("SparrowWasmModule is missing from sparrow_wasm.js.");
    }
    modulePromise = self.SparrowWasmModule();
  }
  return modulePromise;
}

// Emscripten starts pthread workers from this worker URL. Preserve its message handler there.
if (self.name !== "em-pthread") {
self.onmessage = async function (message) {
  if (!message.data || message.data.type !== "run") {
    return;
  }

  let wasm;
  let inputPointer = 0;
  let outputPointerPointer = 0;
  let jsonSizePointer = 0;
  let svgSizePointer = 0;
  let outputPointer = 0;
  try {
    wasm = await GetModule();
    const inputBytes = new TextEncoder().encode(message.data.inputJson);
    inputPointer = wasm._malloc(inputBytes.length + 1);
    outputPointerPointer = wasm._malloc(4);
    jsonSizePointer = wasm._malloc(4);
    svgSizePointer = wasm._malloc(4);
    if (!inputPointer || !outputPointerPointer || !jsonSizePointer || !svgSizePointer) {
      throw new Error("Unable to allocate WebAssembly input storage.");
    }

    wasm.HEAPU8.set(inputBytes, inputPointer);
    wasm.HEAPU8[inputPointer + inputBytes.length] = 0;
    wasm.HEAPU32[outputPointerPointer >>> 2] = 0;
    wasm.HEAPU32[jsonSizePointer >>> 2] = 0;
    wasm.HEAPU32[svgSizePointer >>> 2] = 0;

    const status = wasm._SparrowOptimizeWithSvg(
      inputPointer,
      inputBytes.length,
      outputPointerPointer,
      jsonSizePointer,
      svgSizePointer
    );
    outputPointer = wasm.HEAPU32[outputPointerPointer >>> 2];
    const jsonSize = wasm.HEAPU32[jsonSizePointer >>> 2];
    const svgSize = wasm.HEAPU32[svgSizePointer >>> 2];
    if (!outputPointer) {
      throw new Error("SparrowWasm failed to allocate its result (code " + status + ").");
    }

    const jsonText = new TextDecoder().decode(
      wasm.HEAPU8.slice(outputPointer, outputPointer + jsonSize)
    );
    if (status !== 0) {
      let detail = "SparrowWasm optimization failed (code " + status + ").";
      try {
        const payload = JSON.parse(jsonText);
        if (payload && payload.error) {
          detail = String(payload.error);
        }
      } catch (_) {
        // Keep the stable status message when the error payload is malformed.
      }
      throw new Error(detail);
    }

    const svgStart = outputPointer + jsonSize + 1;
    const svgText = new TextDecoder().decode(
      wasm.HEAPU8.slice(svgStart, svgStart + svgSize)
    );
    self.postMessage({ type: "result", json: jsonText, svg: svgText });
  } catch (error) {
    self.postMessage({
      type: "error",
      message: error instanceof Error ? error.message : String(error)
    });
  } finally {
    if (wasm) {
      if (outputPointer && typeof wasm._SparrowFreeOutput === "function") {
        wasm._SparrowFreeOutput(outputPointer);
      }
      if (inputPointer) wasm._free(inputPointer);
      if (outputPointerPointer) wasm._free(outputPointerPointer);
      if (jsonSizePointer) wasm._free(jsonSizePointer);
      if (svgSizePointer) wasm._free(svgSizePointer);
    }
  }
};
}
