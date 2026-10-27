"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

const foreignRealmValue = (expression) =>
  vm.runInNewContext(expression);

function captureThrownValue(operation) {
  try {
    return { value: operation(), thrown: undefined };
  } catch (thrown) {
    return { value: undefined, thrown };
  }
}

function defineFixedToJSON(target, enumerable = true) {
  Object.defineProperty(target, "toJSON", {
    value() {
      throw new Error("Rust Serde decoding must not execute toJSON");
    },
    enumerable,
    configurable: false,
    writable: false,
  });
}

function defineToJSONGetter(target, shouldThrow, enumerable = true) {
  let reads = 0;
  Object.defineProperty(target, "toJSON", {
    configurable: true,
    enumerable,
    get() {
      reads += 1;
      if (shouldThrow) {
        throw new Error("Rust Vec and Map decoding must ignore toJSON getters");
      }
      return () => {
        throw new Error("Rust Vec and Map decoding must not execute toJSON");
      };
    },
  });
  return () => reads;
}

function defineInheritedToJSONGetter(target) {
  let reads = 0;
  const prototype = Object.create(Object.getPrototypeOf(target));
  Object.defineProperty(prototype, "toJSON", {
    configurable: true,
    get() {
      reads += 1;
      throw new Error("Rust Serde decoding must ignore inherited toJSON getters");
    },
  });
  Object.setPrototypeOf(target, prototype);
  return () => reads;
}

async function main() {
  const buildDirectory = process.argv[2];
  assert.ok(buildDirectory, "pass the directory containing lbf.js and lbf.wasm");

  const resolvedBuildDirectory = path.resolve(buildDirectory);
  const createModule = require(path.join(resolvedBuildDirectory, "lbf.js"));
  const warnings = [];
  const originalWarn = console.warn;
  console.warn = (...args) => warnings.push(args.map(String).join(" "));

  try {
    const module = await createModule({
      locateFile: (name) => path.join(resolvedBuildDirectory, name),
    });
    const invalidInstance = {
      name: "wasm-u64-config-regression",
      min_item_separation: -1,
      items: [],
      strip_height: 4,
    };
    const importerError =
      "Importer error: min_item_separation must be finite and nonnegative";
    const fullConfig = {
      cde_config: {
        quadtree_depth: 5,
        cd_threshold: 96,
        item_surrogate_config: {
          n_pole_limits: [[100, 0], [20, 0.75], [10, 0.9]],
          ff_pole_area_ratio: 0.5,
          n_ff_piers: 0,
        },
      },
      prng_seed: 18446744073709551615n,
      n_samples: 64,
    };

    const rootArrayGetterReads = { count: 0 };
    const rootArrayWithGetter = [];
    Object.defineProperty(rootArrayWithGetter, "0", {
      configurable: true,
      enumerable: true,
      get() {
        rootArrayGetterReads.count += 1;
        throw new Error("root sequence values must not be read for a struct");
      },
    });
    rootArrayWithGetter.length = 1;
    const rootSequenceCases = [
      ["empty array", []],
      ["nonempty array", [null, 1, "extra"]],
      ["array with ignored index getter", rootArrayWithGetter],
    ];
    for (const kind of ["SPP", "BPP"]) {
      for (const [name, rootSequence] of rootSequenceCases) {
        warnings.length = 0;
        rootArrayGetterReads.count = 0;
        const sequenceError = captureThrownValue(() =>
          kind === "SPP"
            ? module.run_lbf_spp_wasm(rootSequence, { n_samples: 1, ls_frac: 0 })
            : module.run_lbf_bpp_wasm(rootSequence, { n_samples: 1, ls_frac: 0 }),
        );
        assert.ok(
          sequenceError.thrown instanceof WebAssembly.RuntimeError,
          `${kind} ${name} must trap while deserializing the root struct`,
        );
        assert.equal(sequenceError.thrown.message, "unreachable");
        assert.deepEqual(warnings, [
          `Ext${kind === "SPP" ? "SP" : "BP"}Instance decode error: Error: missing field \`name\`, not using fallback!!`,
        ], `${kind} ${name} root sequence warning`);
        if (name === "array with ignored index getter")
          assert.equal(rootArrayGetterReads.count, 0, `${kind} ${name} must not read its element`);
      }
    }

    const rootArrayProxyReads = [];
    const rootArrayProxyTarget = [];
    for (const [key, value] of Object.entries({
      name: "array-root",
      min_item_separation: 0,
      items: new Map(),
      strip_height: 4,
      bins: new Uint8Array(),
    })) {
      Object.defineProperty(rootArrayProxyTarget, key, {
        configurable: true,
        enumerable: true,
        writable: true,
        value,
      });
    }
    const rootArrayProxy = new Proxy(rootArrayProxyTarget, {
      get(target, key, receiver) {
        rootArrayProxyReads.push(`get:${String(key)}`);
        return Reflect.get(target, key, receiver);
      },
      has(target, key) {
        rootArrayProxyReads.push(`has:${String(key)}`);
        return Reflect.has(target, key);
      },
    });
    const expectedRootArrayFields = {
      SPP: ["name", "min_item_separation", "items", "strip_height"],
      BPP: ["name", "min_item_separation", "items", "bins"],
    };
    for (const kind of ["SPP", "BPP"]) {
      warnings.length = 0;
      rootArrayProxyReads.length = 0;
      const decoratedArrayError = captureThrownValue(() =>
        kind === "SPP"
          ? module.run_lbf_spp_wasm(rootArrayProxy, { n_samples: 1, ls_frac: 0 })
          : module.run_lbf_bpp_wasm(rootArrayProxy, { n_samples: 1, ls_frac: 0 }),
      );
      assert.equal(
        decoratedArrayError.thrown,
        "Importer error: instance must have positive item demand",
        `${kind} decorated root array must deserialize its named fields`,
      );
      assert.deepEqual(warnings, [], `${kind} decorated root array warning`);
      assert.deepEqual(
        rootArrayProxyReads,
        expectedRootArrayFields[kind].map((key) => `get:${key}`),
        `${kind} decorated root array field read order`,
      );
    }

    const invalidRootArrayItems = [
      ["null", null, ["get:name", "get:min_item_separation", "get:items"]],
      [
        "undefined",
        undefined,
        ["get:name", "get:min_item_separation", "get:items", "has:items"],
      ],
      [
        "plain object",
        {},
        ["get:name", "get:min_item_separation", "get:items"],
      ],
    ];
    for (const kind of ["SPP", "BPP"]) {
      for (const [name, items, expectedReads] of invalidRootArrayItems) {
        const invalidItemsReads = [];
        const invalidItemsTarget = [];
        for (const [key, value] of Object.entries({
          name: "array-root",
          min_item_separation: 0,
          items,
          strip_height: 4,
          bins: [],
        })) {
          Object.defineProperty(invalidItemsTarget, key, {
            configurable: true,
            enumerable: true,
            value,
          });
        }
        const invalidItemsProxy = new Proxy(invalidItemsTarget, {
          get(target, key, receiver) {
            invalidItemsReads.push(`get:${String(key)}`);
            return Reflect.get(target, key, receiver);
          },
          has(target, key) {
            invalidItemsReads.push(`has:${String(key)}`);
            return Reflect.has(target, key);
          },
        });
        warnings.length = 0;
        const invalidItemsError = captureThrownValue(() =>
          kind === "SPP"
            ? module.run_lbf_spp_wasm(invalidItemsProxy, { n_samples: 1, ls_frac: 0 })
            : module.run_lbf_bpp_wasm(invalidItemsProxy, { n_samples: 1, ls_frac: 0 }),
        );
        assert.ok(invalidItemsError.thrown instanceof WebAssembly.RuntimeError);
        assert.equal(invalidItemsError.thrown.message, "unreachable");
        const expectedWarning = name === "plain object"
          ? "Error: invalid type: JsValue(Object({})), expected a sequence"
          : "TypeError: Reflect.get called on non-object";
        assert.deepEqual(warnings, [
          `Ext${kind === "SPP" ? "SP" : "BP"}Instance decode error: ${expectedWarning}, not using fallback!!`,
        ]);
        assert.deepEqual(
          invalidItemsReads,
          expectedReads,
          `${kind} ${name} root array item field reads`,
        );
      }
    }

    const statefulSequenceItem = {
      id: 1,
      demand: 1,
      orientation: { rotation: { mode: "continuous" } },
      shape: {
        type: "rectangle",
        data: { x_min: 0, y_min: 0, width: 1, height: 1 },
      },
    };
    for (const kind of ["SPP", "BPP"]) {
      const statefulSequenceEvents = [];
      let statefulNextReads = 0;
      let statefulNextCalls = 0;
      const statefulItems = {
        [Symbol.iterator]() {
          statefulSequenceEvents.push("iterator");
          return {
            get next() {
              statefulNextReads += 1;
              statefulSequenceEvents.push(`next-get:${statefulNextReads}`);
              if (statefulNextReads === 3) return null;
              return () => {
                statefulNextCalls += 1;
                statefulSequenceEvents.push(`next-call:${statefulNextCalls}`);
                return statefulNextCalls === 1
                  ? { done: false, value: statefulSequenceItem }
                  : { done: true };
              };
            },
          };
        },
      };
      const statefulRootArray = [];
      for (const [key, value] of Object.entries({
        name: "stateful-array-root",
        min_item_separation: 0,
        items: statefulItems,
        strip_height: 4,
        bins: [],
      })) {
        Object.defineProperty(statefulRootArray, key, {
          configurable: true,
          enumerable: true,
          value,
        });
      }
      warnings.length = 0;
      const statefulSequenceError = captureThrownValue(() =>
        kind === "SPP"
          ? module.run_lbf_spp_wasm(statefulRootArray, { n_samples: 1, ls_frac: 0 })
          : module.run_lbf_bpp_wasm(statefulRootArray, { n_samples: 1, ls_frac: 0 }),
      );
      assert.ok(statefulSequenceError.thrown instanceof WebAssembly.RuntimeError);
      assert.equal(statefulSequenceError.thrown.message, "unreachable");
      assert.deepEqual(warnings, [
        `Ext${kind === "SPP" ? "SP" : "BP"}Instance decode error: TypeError: arg0.next is not a function, not using fallback!!`,
      ]);
      assert.deepEqual(statefulSequenceEvents, [
        "iterator",
        "next-get:1",
        "next-get:2",
        "next-call:1",
        "next-get:3",
      ]);
    }

    const invalidRootArrayNames = [
      ["undefined", undefined, "invalid type: unit value, expected a string", ["get:name", "has:name"]],
      ["null", null, "invalid type: unit value, expected a string", ["get:name"]],
      ["number", 1, "invalid type: floating point `1.0`, expected a string", ["get:name"]],
    ];
    for (const kind of ["SPP", "BPP"]) {
      for (const [name, invalidName, message, expectedReads] of invalidRootArrayNames) {
        const invalidNameReads = [];
        const invalidNameTarget = [];
        Object.defineProperty(invalidNameTarget, "name", {
          configurable: true,
          enumerable: true,
          value: invalidName,
        });
        const invalidNameProxy = new Proxy(invalidNameTarget, {
          get(target, key, receiver) {
            invalidNameReads.push(`get:${String(key)}`);
            return Reflect.get(target, key, receiver);
          },
          has(target, key) {
            invalidNameReads.push(`has:${String(key)}`);
            return Reflect.has(target, key);
          },
        });
        warnings.length = 0;
        const invalidNameError = captureThrownValue(() =>
          kind === "SPP"
            ? module.run_lbf_spp_wasm(invalidNameProxy, { n_samples: 1, ls_frac: 0 })
            : module.run_lbf_bpp_wasm(invalidNameProxy, { n_samples: 1, ls_frac: 0 }),
        );
        assert.ok(invalidNameError.thrown instanceof WebAssembly.RuntimeError);
        assert.equal(invalidNameError.thrown.message, "unreachable");
        assert.deepEqual(warnings, [
          `Ext${kind === "SPP" ? "SP" : "BP"}Instance decode error: Error: ${message}, not using fallback!!`,
        ]);
        assert.deepEqual(invalidNameReads, expectedReads, `${kind} ${name} root array field reads`);
      }
    }

    const rootF32FunctionNameReads = [];
    const namedRootF32Function = function originalRootF32() {};
    Object.defineProperty(namedRootF32Function, "name", {
      configurable: true,
      get() {
        rootF32FunctionNameReads.push("name");
        return "f32-field";
      },
    });
    const rootF32ObjectProxyEvents = [];
    const rootF32ProxyObject = new Proxy({ a: 1 }, {
      get(target, key, receiver) {
        rootF32ObjectProxyEvents.push(`get:${String(key)}`);
        return Reflect.get(target, key, receiver);
      },
      getPrototypeOf(target) {
        rootF32ObjectProxyEvents.push("getPrototypeOf");
        return Reflect.getPrototypeOf(target);
      },
      ownKeys(target) {
        rootF32ObjectProxyEvents.push("ownKeys");
        return Reflect.ownKeys(target);
      },
      getOwnPropertyDescriptor(target, key) {
        rootF32ObjectProxyEvents.push(`descriptor:${String(key)}`);
        return Reflect.getOwnPropertyDescriptor(target, key);
      },
    });
    const rootF32CustomTagObject = {};
    Object.defineProperty(rootF32CustomTagObject, Symbol.toStringTag, {
      value: "Custom",
    });
    const fixedStackRootF32Error = new Error("probe");
    fixedStackRootF32Error.stack = "fixture stack";
    const invalidRootF32Values = [
      ["undefined", undefined, "invalid type: unit value, expected f32", true],
      ["null", null, "invalid type: unit value, expected f32", false],
      ["string", "0", 'invalid type: string "0", expected f32', false],
      ["boolean", false, "invalid type: boolean `false`, expected f32", false],
      ["object", {}, "invalid type: JsValue(Object({})), expected f32", false],
      ["array", [], "invalid type: JsValue([]), expected f32", false],
      ["boxed number", new Number(1), "invalid type: JsValue(Number), expected f32", false],
      ["boxed string", new String("x"), "invalid type: JsValue(String), expected f32", false],
      ["byte array", new Uint8Array([1]), "invalid type: byte array, expected f32", false],
      ["custom toStringTag", rootF32CustomTagObject, "invalid type: JsValue(Custom), expected f32", false],
      ["fixed-stack Error", fixedStackRootF32Error, "invalid type: JsValue(Error: probe\nfixture stack), expected f32", false],
      ["Proxy object", rootF32ProxyObject, 'invalid type: JsValue(Object({"a":1})), expected f32', false],
      ["symbol", Symbol("x"), "invalid type: JsValue(Symbol(x)), expected f32", false],
      ["bigint", 1n, "invalid type: JsValue(BigInt), expected f32", false],
      ["function", namedRootF32Function, "invalid type: JsValue(Function(f32-field)), expected f32", false],
    ];
    const rootF32Fields = [
      {
        field: "min_item_separation",
        kinds: ["SPP", "BPP"],
        readPrefix: ["get:name", "get:min_item_separation"],
      },
      {
        field: "strip_height",
        kinds: ["SPP"],
        readPrefix: [
          "get:name",
          "get:min_item_separation",
          "get:items",
          "get:strip_height",
        ],
      },
    ];
    for (const { field, kinds, readPrefix } of rootF32Fields) {
      for (const kind of kinds) {
        for (const [name, value, message, isUndefined] of invalidRootF32Values) {
          const rootF32Reads = [];
          rootF32FunctionNameReads.length = 0;
          rootF32ObjectProxyEvents.length = 0;
          const rootF32Target = [];
          for (const [key, member] of Object.entries({
            name: "array-root",
            min_item_separation: 0,
            items: [],
            strip_height: 4,
            bins: [],
            [field]: value,
          })) {
            Object.defineProperty(rootF32Target, key, {
              configurable: true,
              enumerable: true,
              value: member,
            });
          }
          const rootF32Proxy = new Proxy(rootF32Target, {
            get(target, key, receiver) {
              rootF32Reads.push(`get:${String(key)}`);
              return Reflect.get(target, key, receiver);
            },
            has(target, key) {
              rootF32Reads.push(`has:${String(key)}`);
              return Reflect.has(target, key);
            },
          });
          warnings.length = 0;
          const invalidRootF32Error = captureThrownValue(() =>
            kind === "SPP"
              ? module.run_lbf_spp_wasm(rootF32Proxy, { n_samples: 1, ls_frac: 0 })
              : module.run_lbf_bpp_wasm(rootF32Proxy, { n_samples: 1, ls_frac: 0 }),
          );
          assert.ok(invalidRootF32Error.thrown instanceof WebAssembly.RuntimeError);
          assert.equal(invalidRootF32Error.thrown.message, "unreachable");
          assert.deepEqual(warnings, [
            `Ext${kind === "SPP" ? "SP" : "BP"}Instance decode error: Error: ${message}, not using fallback!!`,
          ]);
          assert.deepEqual(
            rootF32Reads,
            isUndefined
              ? [...readPrefix, `has:${field}`]
              : readPrefix,
            `${kind} ${field} ${name} getter stop order`,
          );
          assert.deepEqual(
            rootF32FunctionNameReads,
            value === namedRootF32Function ? ["name"] : [],
            `${kind} ${field} ${name} function-name getter count`,
          );
          assert.deepEqual(
            rootF32ObjectProxyEvents,
            value === rootF32ProxyObject
              ? [
                  "getPrototypeOf",
                  "getPrototypeOf",
                  "get:Symbol(Symbol.toStringTag)",
                  "get:toJSON",
                  "ownKeys",
                  "descriptor:a",
                  "get:a",
                ]
              : [],
            `${kind} ${field} ${name} object Proxy access order`,
          );
        }
      }
    }

    const rootF32NameError = new Error("root f32 function name getter failed");
    const throwingRootF32NameReads = [];
    const throwingRootF32Function = function originalThrowingRootF32() {};
    Object.defineProperty(throwingRootF32Function, "name", {
      configurable: true,
      get() {
        throwingRootF32NameReads.push("name");
        throw rootF32NameError;
      },
    });
    const throwingRootF32Target = [];
    for (const [key, value] of Object.entries({
      name: "array-root",
      min_item_separation: throwingRootF32Function,
      items: [],
      strip_height: 4,
      bins: [],
    })) {
      Object.defineProperty(throwingRootF32Target, key, {
        configurable: true,
        enumerable: true,
        value,
      });
    }
    const throwingRootF32Reads = [];
    const throwingRootF32Proxy = new Proxy(throwingRootF32Target, {
      get(target, key, receiver) {
        throwingRootF32Reads.push(`get:${String(key)}`);
        return Reflect.get(target, key, receiver);
      },
      has(target, key) {
        throwingRootF32Reads.push(`has:${String(key)}`);
        return Reflect.has(target, key);
      },
    });
    warnings.length = 0;
    const throwingRootF32NameErrorResult = captureThrownValue(() =>
      module.run_lbf_spp_wasm(throwingRootF32Proxy, { n_samples: 1, ls_frac: 0 }),
    );
    assert.equal(throwingRootF32NameErrorResult.thrown, rootF32NameError);
    assert.deepEqual(warnings, []);
    assert.deepEqual(throwingRootF32NameReads, ["name"]);
    assert.deepEqual(throwingRootF32Reads, ["get:name", "get:min_item_separation"]);

    const rootF32TagError = { kind: "root-f32-tag-error" };
    const rootF32TagReads = [];
    const throwingRootF32TagProxy = new Proxy({}, {
      get(target, key, receiver) {
        rootF32TagReads.push(`get:${String(key)}`);
        if (key === Symbol.toStringTag) {
          throw rootF32TagError;
        }
        return Reflect.get(target, key, receiver);
      },
    });
    const throwingRootF32TagTarget = [];
    for (const [key, value] of Object.entries({
      name: "array-root",
      min_item_separation: throwingRootF32TagProxy,
      items: [],
      strip_height: 4,
      bins: [],
    })) {
      Object.defineProperty(throwingRootF32TagTarget, key, {
        configurable: true,
        enumerable: true,
        value,
      });
    }
    const throwingRootF32TagRootReads = [];
    const throwingRootF32TagRoot = new Proxy(throwingRootF32TagTarget, {
      get(target, key, receiver) {
        throwingRootF32TagRootReads.push(`get:${String(key)}`);
        return Reflect.get(target, key, receiver);
      },
      has(target, key) {
        throwingRootF32TagRootReads.push(`has:${String(key)}`);
        return Reflect.has(target, key);
      },
    });
    warnings.length = 0;
    const throwingRootF32TagResult = captureThrownValue(() =>
      module.run_lbf_spp_wasm(throwingRootF32TagRoot, { n_samples: 1, ls_frac: 0 }),
    );
    assert.equal(throwingRootF32TagResult.thrown, rootF32TagError);
    assert.deepEqual(warnings, []);
    assert.deepEqual(rootF32TagReads, ["get:Symbol(Symbol.toStringTag)"]);
    assert.deepEqual(throwingRootF32TagRootReads, ["get:name", "get:min_item_separation"]);

    for (const kind of ["SPP", "BPP"]) {
      const missingSeparationReads = [];
      const missingSeparationTarget = [];
      for (const [key, value] of Object.entries({
        name: "array-root",
        items: [],
        strip_height: 4,
        bins: [],
      })) {
        Object.defineProperty(missingSeparationTarget, key, {
          configurable: true,
          enumerable: true,
          value,
        });
      }
      const missingSeparationProxy = new Proxy(missingSeparationTarget, {
        get(target, key, receiver) {
          missingSeparationReads.push(`get:${String(key)}`);
          return Reflect.get(target, key, receiver);
        },
        has(target, key) {
          missingSeparationReads.push(`has:${String(key)}`);
          return Reflect.has(target, key);
        },
      });
      warnings.length = 0;
      const missingSeparationError = captureThrownValue(() =>
        kind === "SPP"
          ? module.run_lbf_spp_wasm(missingSeparationProxy, { n_samples: 1, ls_frac: 0 })
          : module.run_lbf_bpp_wasm(missingSeparationProxy, { n_samples: 1, ls_frac: 0 }),
      );
      assert.equal(
        missingSeparationError.thrown,
        "Importer error: instance must have positive item demand",
      );
      assert.deepEqual(warnings, []);
      assert.deepEqual(
        missingSeparationReads,
        [
          "get:name",
          "get:min_item_separation",
          "has:min_item_separation",
          "get:items",
          kind === "SPP" ? "get:strip_height" : "get:bins",
        ],
      );
    }

    const missingRootArrayReads = [];
    const missingRootArrayProxy = new Proxy([], {
      get(target, key, receiver) {
        missingRootArrayReads.push(`get:${String(key)}`);
        return Reflect.get(target, key, receiver);
      },
      has(target, key) {
        missingRootArrayReads.push(`has:${String(key)}`);
        return Reflect.has(target, key);
      },
    });
    const rawRootArrayThrown = new Proxy({}, {});
    const rawRootArrayReads = [];
    const throwingRootArrayProxy = new Proxy([], {
      get(target, key, receiver) {
        rawRootArrayReads.push(`get:${String(key)}`);
        if (key === "name") throw rawRootArrayThrown;
        return Reflect.get(target, key, receiver);
      },
    });
    for (const kind of ["SPP", "BPP"]) {
      warnings.length = 0;
      missingRootArrayReads.length = 0;
      const missingArrayError = captureThrownValue(() =>
        kind === "SPP"
          ? module.run_lbf_spp_wasm(missingRootArrayProxy, { n_samples: 1, ls_frac: 0 })
          : module.run_lbf_bpp_wasm(missingRootArrayProxy, { n_samples: 1, ls_frac: 0 }),
      );
      assert.ok(missingArrayError.thrown instanceof WebAssembly.RuntimeError);
      assert.equal(missingArrayError.thrown.message, "unreachable");
      assert.deepEqual(warnings, [
        `Ext${kind === "SPP" ? "SP" : "BP"}Instance decode error: Error: missing field \`name\`, not using fallback!!`,
      ]);
      assert.deepEqual(
        missingRootArrayReads,
        expectedRootArrayFields[kind].flatMap((key) => [`get:${key}`, `has:${key}`]),
        `${kind} missing root array get/has order`,
      );

      warnings.length = 0;
      rawRootArrayReads.length = 0;
      const thrownArrayError = captureThrownValue(() =>
        kind === "SPP"
          ? module.run_lbf_spp_wasm(throwingRootArrayProxy, { n_samples: 1, ls_frac: 0 })
          : module.run_lbf_bpp_wasm(throwingRootArrayProxy, { n_samples: 1, ls_frac: 0 }),
      );
      assert.equal(thrownArrayError.thrown, rawRootArrayThrown);
      assert.deepEqual(warnings, []);
      assert.deepEqual(rawRootArrayReads, ["get:name"]);
    }

    const emptyDemandInstance = { ...invalidInstance, min_item_separation: 0 };
    const emptyDemandError = captureThrownValue(() =>
      module.run_lbf_spp_wasm(emptyDemandInstance, {}),
    );
    assert.equal(
      emptyDemandError.thrown,
      "Importer error: instance must have positive item demand",
      "empty-demand failures must preserve the Importer error origin",
    );

    warnings.length = 0;
    const valid = captureThrownValue(
      () => module.run_lbf_spp_wasm(invalidInstance, fullConfig),
    );
    assert.equal(valid.thrown, importerError);
    assert.deepEqual(
      warnings,
      [],
      "valid BigInt u64 and safe-Number config fields must not trigger whole-config fallback",
    );

    const svgTheme = {
      stroke_width_multiplier: 2,
      container_fill: "#CC824A",
      item_fill: "#FFC879",
      hole_fill: "#2D2D2D",
      qz_fill: Array(10).fill("#CBFF00"),
      qz_stroke_opac: 0.5,
      collision_highlight_color: "#00FF00",
    };
    warnings.length = 0;
    const f32OverflowStrokeWidth = captureThrownValue(() =>
      module.run_lbf_spp_wasm(invalidInstance, {
        svg_draw_options: {
          theme: { ...svgTheme, stroke_width_multiplier: 1e100 },
        },
        n_samples: 1,
        ls_frac: 0,
      }),
    );
    assert.equal(f32OverflowStrokeWidth.thrown, importerError);
    assert.equal(
      warnings.length,
      1,
      "an SVG stroke width that overflows f32 must trigger config fallback before solving",
    );
    assert.match(warnings[0], /Invalid config, using default\./);

    const missingQualityColorsWithShortCollision = { ...svgTheme };
    delete missingQualityColorsWithShortCollision.qz_fill;
    missingQualityColorsWithShortCollision.collision_highlight_color = "#123";
    for (const [name, theme] of [
      ["short primary color", { ...svgTheme, container_fill: "#123" }],
      ["invalid primary hex", { ...svgTheme, item_fill: "#GGGGGG" }],
      ["short color in incomplete quality array", { ...svgTheme, qz_fill: ["#123"] }],
      ["short collision color after missing qz_fill", missingQualityColorsWithShortCollision],
    ]) {
      warnings.length = 0;
      const colorError = captureThrownValue(() =>
        module.run_lbf_spp_wasm(invalidInstance, {
          svg_draw_options: { theme },
          n_samples: 1,
          ls_frac: 0,
        }),
      );
      assert.ok(
        colorError.thrown instanceof WebAssembly.RuntimeError,
        `${name} must preserve Rust's color-deserialization trap`,
      );
      assert.equal(colorError.thrown.message, "unreachable");
      assert.deepEqual(warnings, [], `${name} must not fall back to the default config`);
    }

    warnings.length = 0;
    const nonStringColorError = captureThrownValue(() =>
      module.run_lbf_spp_wasm(invalidInstance, {
        svg_draw_options: {
          theme: { ...svgTheme, container_fill: 123, item_fill: "#123" },
        },
        n_samples: 1,
        ls_frac: 0,
      }),
    );
    assert.equal(nonStringColorError.thrown, importerError);
    assert.equal(warnings.length, 1, "non-string colors remain Serde config fallback errors");

    warnings.length = 0;
    const extraMalformedQualityColor = captureThrownValue(() =>
      module.run_lbf_spp_wasm(invalidInstance, {
        svg_draw_options: {
          theme: { ...svgTheme, qz_fill: [...svgTheme.qz_fill, "#123"] },
        },
        n_samples: 1,
        ls_frac: 0,
      }),
    );
    assert.equal(
      extraMalformedQualityColor.thrown,
      importerError,
      "serde checks the fixed array length without deserializing its 11th element",
    );
    assert.equal(warnings.length, 1, "an ignored 11th quality color remains a config fallback");
    warnings.length = 0;

    const fixture = JSON.parse(
      fs.readFileSync(path.join(__dirname, "Fixtures", "LbfSppSmall.json"), "utf8"),
    );
    const separationConsumesStripCases = [
      ["equal strip height", 4],
      ["positive but collapses the deflated strip", 3.9999998],
      ["just above strip height", 4.0000005],
      ["f32-rounded separation above strip height", 16777217],
    ];
    for (const [name, separation] of separationConsumesStripCases) {
      const source = structuredClone(fixture);
      source.min_item_separation = separation;
      for (const useRootArray of [false, true]) {
        const instance = useRootArray ? Object.assign([], source) : source;
        warnings.length = 0;
        const collapsedStripError = captureThrownValue(() =>
          module.run_lbf_spp_wasm(instance, { n_samples: 1, ls_frac: 0 }),
        );
        assert.ok(
          collapsedStripError.thrown instanceof WebAssembly.RuntimeError,
          `${name} ${useRootArray ? "root array" : "object"} must match Rust's strip-offset trap`,
        );
        assert.equal(collapsedStripError.thrown.message, "unreachable");
        assert.deepEqual(warnings, []);
      }
    }

    const stripHeightsWithoutPoleCases = [
      ["f32-rounded large strip height", 16777217],
      ["safe-integer large strip height", 9007199254740992],
    ];
    for (const [name, stripHeight] of stripHeightsWithoutPoleCases) {
      const source = structuredClone(fixture);
      source.strip_height = stripHeight;
      for (const useRootArray of [false, true]) {
        const instance = useRootArray ? Object.assign([], source) : source;
        warnings.length = 0;
        const missingPoleError = captureThrownValue(() =>
          module.run_lbf_spp_wasm(instance, { n_samples: 1, ls_frac: 0 }),
        );
        assert.ok(
          missingPoleError.thrown instanceof WebAssembly.RuntimeError,
          `${name} ${useRootArray ? "root array" : "object"} must match Rust's pole-search trap`,
        );
        assert.equal(missingPoleError.thrown.message, "unreachable");
        assert.deepEqual(warnings, []);
      }
    }

    const stripHeightGeometryTrapCases = [
      ["smallest positive f32 subnormal strip height", 1.401298464324817e-45],
      ["f32 maximum strip height", 3.4028234663852886e38],
    ];
    for (const [name, stripHeight] of stripHeightGeometryTrapCases) {
      const source = structuredClone(fixture);
      source.strip_height = stripHeight;
      for (const useRootArray of [false, true]) {
        const instance = useRootArray ? Object.assign([], source) : source;
        warnings.length = 0;
        const stripGeometryError = captureThrownValue(() =>
          module.run_lbf_spp_wasm(instance, { n_samples: 1, ls_frac: 0 }),
        );
        assert.ok(
          stripGeometryError.thrown instanceof WebAssembly.RuntimeError,
          `${name} ${useRootArray ? "root array" : "object"} must match Rust's strip-geometry trap`,
        );
        assert.equal(stripGeometryError.thrown.message, "unreachable");
        assert.deepEqual(warnings, []);
      }
    }

    const emptyOffsetSeparationCases = [
      ["safe-integer separation", 9007199254740992],
      ["maximum f32 separation", 3.4028234663852886e38],
    ];
    for (const [name, separation] of emptyOffsetSeparationCases) {
      const source = structuredClone(fixture);
      source.min_item_separation = separation;
      for (const useRootArray of [false, true]) {
        const instance = useRootArray ? Object.assign([], source) : source;
        warnings.length = 0;
        const emptyOffsetError = captureThrownValue(() =>
          module.run_lbf_spp_wasm(instance, { n_samples: 1, ls_frac: 0 }),
        );
        assert.equal(
          emptyOffsetError.thrown,
          "Importer error: Offset resulted in an empty polygon",
          `${name} ${useRootArray ? "root array" : "object"} must match pinned Rust Offset error`,
        );
        assert.deepEqual(warnings, []);
      }
    }

    const rustNonFiniteInputCases = [
      ["NaN min item separation", (instance) => {
        instance.min_item_separation = Number.NaN;
      }],
      ["positive infinite min item separation", (instance) => {
        instance.min_item_separation = Number.POSITIVE_INFINITY;
      }],
      ["negative infinite min item separation", (instance) => {
        instance.min_item_separation = Number.NEGATIVE_INFINITY;
      }],
      ["finite f32-overflowing min item separation", (instance) => {
        instance.min_item_separation = 1e100;
      }],
      ["NaN strip height", (instance) => {
        instance.min_item_separation = -1;
        instance.strip_height = Number.NaN;
      }],
      ["NaN rectangle width", (instance) => {
        instance.min_item_separation = -1;
        instance.items[0].shape.data.width = Number.NaN;
      }],
      ["infinite rotation angle", (instance) => {
        instance.min_item_separation = -1;
        instance.items[0].orientation.rotation.angles[0] = Number.POSITIVE_INFINITY;
      }],
    ];
    for (const [name, mutate] of rustNonFiniteInputCases) {
      const instance = structuredClone(fixture);
      mutate(instance);
      const nonFiniteError = captureThrownValue(() =>
        module.run_lbf_spp_wasm(instance, { n_samples: 1, ls_frac: 0 }),
      );
      assert.equal(
        nonFiniteError.thrown,
        importerError,
        `${name} must survive JSON adaptation and reach the Rust-compatible Importer error`,
      );
    }

    const positiveInfiniteStripHeight = structuredClone(fixture);
    positiveInfiniteStripHeight.min_item_separation = 0;
    positiveInfiniteStripHeight.strip_height = Number.POSITIVE_INFINITY;
    const positiveInfiniteStripHeightError = captureThrownValue(() =>
      module.run_lbf_spp_wasm(positiveInfiniteStripHeight, {
        n_samples: 1,
        ls_frac: 0,
      }),
    );
    assert.equal(
      positiveInfiniteStripHeightError.thrown,
      "Importer error: strip width must be positive",
      "positive infinite height yields a zero base width before Rust validates the strip",
    );

    const positiveInfiniteRectangleWidth = structuredClone(fixture);
    positiveInfiniteRectangleWidth.min_item_separation = 0;
    positiveInfiniteRectangleWidth.items[0].shape.data.width =
      Number.POSITIVE_INFINITY;
    const positiveInfiniteRectangleWidthError = captureThrownValue(() =>
      module.run_lbf_spp_wasm(positiveInfiniteRectangleWidth, {
        n_samples: 1,
        ls_frac: 0,
      }),
    );
    assert.ok(
      positiveInfiniteRectangleWidthError.thrown instanceof WebAssembly.RuntimeError,
      "positive infinite rectangle width reaches Rust polygon construction and traps",
    );
    assert.equal(positiveInfiniteRectangleWidthError.thrown.message, "unreachable");

    const positiveInfiniteRectangleHeight = structuredClone(fixture);
    positiveInfiniteRectangleHeight.min_item_separation = 0;
    positiveInfiniteRectangleHeight.items[0].shape.data.height =
      Number.POSITIVE_INFINITY;
    const positiveInfiniteRectangleHeightError = captureThrownValue(() =>
      module.run_lbf_spp_wasm(positiveInfiniteRectangleHeight, {
        n_samples: 1,
        ls_frac: 0,
      }),
    );
    assert.ok(
      positiveInfiniteRectangleHeightError.thrown instanceof WebAssembly.RuntimeError,
      "positive infinite rectangle height reaches Rust polygon construction and traps",
    );
    assert.equal(positiveInfiniteRectangleHeightError.thrown.message, "unreachable");

    const negativeInfiniteRectangleWidth = structuredClone(fixture);
    negativeInfiniteRectangleWidth.min_item_separation = 0;
    negativeInfiniteRectangleWidth.items[0].shape.data.width =
      Number.NEGATIVE_INFINITY;
    const negativeInfiniteRectangleWidthError = captureThrownValue(() =>
      module.run_lbf_spp_wasm(negativeInfiniteRectangleWidth, {
        n_samples: 1,
        ls_frac: 0,
      }),
    );
    assert.equal(
      negativeInfiniteRectangleWidthError.thrown,
      "Importer error: invalid rectangle, x_min: 0, x_max: -inf, y_min: 0, y_max: 2",
      "negative infinite width fails Rust's rectangle bound comparison instead of trapping",
    );

    const sppItemsArrayWithToJSON = structuredClone(fixture);
    sppItemsArrayWithToJSON.min_item_separation = -1;
    sppItemsArrayWithToJSON.items.toJSON = () => {
      throw new Error("Rust Vec decoding must ignore array toJSON properties");
    };
    const sppItemsArrayWithToJSONError = captureThrownValue(() =>
      module.run_lbf_spp_wasm(sppItemsArrayWithToJSON, { n_samples: 1, ls_frac: 0 }),
    );
    assert.equal(
      sppItemsArrayWithToJSONError.thrown,
      importerError,
      "an enumerable toJSON property on the SPP items Vec must be ignored like Rust",
    );

    const sppAnglesArrayWithToJSON = structuredClone(fixture);
    sppAnglesArrayWithToJSON.min_item_separation = -1;
    sppAnglesArrayWithToJSON.items[0].orientation.rotation.angles.toJSON = () => {
      throw new Error("Rust Vec decoding must ignore array toJSON properties");
    };
    const sppAnglesArrayWithToJSONError = captureThrownValue(() =>
      module.run_lbf_spp_wasm(sppAnglesArrayWithToJSON, { n_samples: 1, ls_frac: 0 }),
    );
    assert.equal(
      sppAnglesArrayWithToJSONError.thrown,
      importerError,
      "an enumerable toJSON property on a nested angle Vec must be ignored like Rust",
    );

    const sppPointListWithToJSON = structuredClone(fixture);
    sppPointListWithToJSON.min_item_separation = -1;
    sppPointListWithToJSON.items[0].shape = {
      type: "polygon",
      data: { outer: [[0, 0], [2, 0], [2, 2], [0, 2]], inner: [] },
    };
    sppPointListWithToJSON.items[0].shape.data.outer.toJSON = () => {
      throw new Error("Rust Vec decoding must ignore array toJSON properties");
    };
    const sppPointListWithToJSONError = captureThrownValue(() =>
      module.run_lbf_spp_wasm(sppPointListWithToJSON, { n_samples: 1, ls_frac: 0 }),
    );
    assert.equal(
      sppPointListWithToJSONError.thrown,
      importerError,
      "enumerable toJSON properties on polygon point Vecs must be ignored like Rust",
    );

    const bppArrayWithToJSON = JSON.parse(
      fs.readFileSync(path.join(__dirname, "Fixtures", "LbfBppSmall.json"), "utf8"),
    );
    bppArrayWithToJSON.min_item_separation = -1;
    bppArrayWithToJSON.bins.toJSON = () => {
      throw new Error("Rust Vec decoding must ignore array toJSON properties");
    };
    const bppArrayWithToJSONError = captureThrownValue(() =>
      module.run_lbf_bpp_wasm(bppArrayWithToJSON, { n_samples: 1, ls_frac: 0 }),
    );
    assert.equal(
      bppArrayWithToJSONError.thrown,
      importerError,
      "an enumerable toJSON property on the BPP bins Vec must be ignored like Rust",
    );

    const sppMapDataWithToJSON = structuredClone(fixture);
    sppMapDataWithToJSON.min_item_separation = -1;
    sppMapDataWithToJSON.items[0].shape.data = new Map(
      Object.entries(sppMapDataWithToJSON.items[0].shape.data),
    );
    sppMapDataWithToJSON.items[0].shape.data.toJSON = () => {
      throw new Error("Rust Map decoding must ignore own toJSON properties");
    };
    const sppMapDataWithToJSONError = captureThrownValue(() =>
      module.run_lbf_spp_wasm(sppMapDataWithToJSON, { n_samples: 1, ls_frac: 0 }),
    );
    assert.equal(
      sppMapDataWithToJSONError.thrown,
      importerError,
      "an enumerable toJSON property on nested string-key Map data must be ignored like Rust",
    );

    const sppItemsWithToJSONGetter = structuredClone(fixture);
    sppItemsWithToJSONGetter.min_item_separation = -1;
    const sppItemsToJSONGetterReads = defineToJSONGetter(
      sppItemsWithToJSONGetter.items,
      false,
    );
    const sppItemsWithToJSONGetterError = captureThrownValue(() =>
      module.run_lbf_spp_wasm(sppItemsWithToJSONGetter, { n_samples: 1, ls_frac: 0 }),
    );
    assert.equal(sppItemsWithToJSONGetterError.thrown, importerError);
    assert.equal(
      sppItemsToJSONGetterReads(),
      0,
      "Rust Vec decoding must not read an own enumerable toJSON getter",
    );

    const bppBinsWithToJSONGetter = JSON.parse(
      fs.readFileSync(path.join(__dirname, "Fixtures", "LbfBppSmall.json"), "utf8"),
    );
    bppBinsWithToJSONGetter.min_item_separation = -1;
    const bppBinsToJSONGetterReads = defineToJSONGetter(
      bppBinsWithToJSONGetter.bins,
      true,
    );
    const bppBinsWithToJSONGetterError = captureThrownValue(() =>
      module.run_lbf_bpp_wasm(bppBinsWithToJSONGetter, { n_samples: 1, ls_frac: 0 }),
    );
    assert.equal(bppBinsWithToJSONGetterError.thrown, importerError);
    assert.equal(
      bppBinsToJSONGetterReads(),
      0,
      "Rust BPP Vec decoding must not read or throw from an own toJSON getter",
    );

    const sppMapWithToJSONGetter = structuredClone(fixture);
    sppMapWithToJSONGetter.min_item_separation = -1;
    sppMapWithToJSONGetter.items[0].shape.data = new Map(
      Object.entries(sppMapWithToJSONGetter.items[0].shape.data),
    );
    const sppMapToJSONGetterReads = defineToJSONGetter(
      sppMapWithToJSONGetter.items[0].shape.data,
      true,
    );
    const sppMapWithToJSONGetterError = captureThrownValue(() =>
      module.run_lbf_spp_wasm(sppMapWithToJSONGetter, { n_samples: 1, ls_frac: 0 }),
    );
    assert.equal(sppMapWithToJSONGetterError.thrown, importerError);
    assert.equal(
      sppMapToJSONGetterReads(),
      0,
      "Rust Map decoding must not read or throw from an own toJSON getter",
    );

    const sppItemsWithHiddenToJSONGetter = structuredClone(fixture);
    sppItemsWithHiddenToJSONGetter.min_item_separation = -1;
    const hiddenItemsToJSONGetterReads = defineToJSONGetter(
      sppItemsWithHiddenToJSONGetter.items,
      true,
      false,
    );
    const sppItemsWithHiddenToJSONGetterError = captureThrownValue(() =>
      module.run_lbf_spp_wasm(sppItemsWithHiddenToJSONGetter, { n_samples: 1, ls_frac: 0 }),
    );
    assert.equal(sppItemsWithHiddenToJSONGetterError.thrown, importerError);
    assert.equal(
      hiddenItemsToJSONGetterReads(),
      0,
      "Rust Vec decoding must ignore a non-enumerable toJSON getter too",
    );

    const sppItemWithInheritedToJSONGetter = structuredClone(fixture);
    sppItemWithInheritedToJSONGetter.min_item_separation = -1;
    const inheritedItemToJSONGetterReads = defineInheritedToJSONGetter(
      sppItemWithInheritedToJSONGetter.items[0],
    );
    const sppItemWithInheritedToJSONGetterError = captureThrownValue(() =>
      module.run_lbf_spp_wasm(sppItemWithInheritedToJSONGetter, { n_samples: 1, ls_frac: 0 }),
    );
    assert.equal(sppItemWithInheritedToJSONGetterError.thrown, importerError);
    assert.equal(
      inheritedItemToJSONGetterReads(),
      0,
      "Rust struct decoding must ignore an inherited toJSON getter",
    );

    const sppOrientationWithInheritedToJSONGetter = structuredClone(fixture);
    sppOrientationWithInheritedToJSONGetter.min_item_separation = -1;
    const inheritedOrientationToJSONGetterReads = defineInheritedToJSONGetter(
      sppOrientationWithInheritedToJSONGetter.items[0].orientation,
    );
    const sppOrientationWithInheritedToJSONGetterError = captureThrownValue(() =>
      module.run_lbf_spp_wasm(sppOrientationWithInheritedToJSONGetter, {
        n_samples: 1,
        ls_frac: 0,
      }),
    );
    assert.equal(sppOrientationWithInheritedToJSONGetterError.thrown, importerError);
    assert.equal(
      inheritedOrientationToJSONGetterReads(),
      0,
      "Rust nested struct decoding must ignore an inherited toJSON getter",
    );

    const sppItemsWithFixedToJSON = structuredClone(fixture);
    sppItemsWithFixedToJSON.min_item_separation = -1;
    defineFixedToJSON(sppItemsWithFixedToJSON.items);
    const sppItemsWithFixedToJSONError = captureThrownValue(() =>
      module.run_lbf_spp_wasm(sppItemsWithFixedToJSON, { n_samples: 1, ls_frac: 0 }),
    );
    assert.equal(
      sppItemsWithFixedToJSONError.thrown,
      importerError,
      "a fixed enumerable toJSON property on an SPP Vec must be ignored like Rust",
    );

    const bppBinsWithFixedToJSON = JSON.parse(
      fs.readFileSync(path.join(__dirname, "Fixtures", "LbfBppSmall.json"), "utf8"),
    );
    bppBinsWithFixedToJSON.min_item_separation = -1;
    defineFixedToJSON(bppBinsWithFixedToJSON.bins);
    const bppBinsWithFixedToJSONError = captureThrownValue(() =>
      module.run_lbf_bpp_wasm(bppBinsWithFixedToJSON, { n_samples: 1, ls_frac: 0 }),
    );
    assert.equal(
      bppBinsWithFixedToJSONError.thrown,
      importerError,
      "a fixed enumerable toJSON property on BPP bins Vec must be ignored like Rust",
    );

    const sppMapWithFixedToJSON = structuredClone(fixture);
    sppMapWithFixedToJSON.min_item_separation = -1;
    sppMapWithFixedToJSON.items[0].shape.data = new Map(
      Object.entries(sppMapWithFixedToJSON.items[0].shape.data),
    );
    defineFixedToJSON(sppMapWithFixedToJSON.items[0].shape.data);
    const sppMapWithFixedToJSONError = captureThrownValue(() =>
      module.run_lbf_spp_wasm(sppMapWithFixedToJSON, { n_samples: 1, ls_frac: 0 }),
    );
    assert.equal(
      sppMapWithFixedToJSONError.thrown,
      importerError,
      "a fixed enumerable toJSON property on nested string-key Map data must be ignored like Rust",
    );

    const sppItemWithFixedToJSON = structuredClone(fixture);
    sppItemWithFixedToJSON.min_item_separation = -1;
    defineFixedToJSON(sppItemWithFixedToJSON.items[0]);
    const sppItemWithFixedToJSONError = captureThrownValue(() =>
      module.run_lbf_spp_wasm(sppItemWithFixedToJSON, { n_samples: 1, ls_frac: 0 }),
    );
    assert.ok(
      sppItemWithFixedToJSONError.thrown instanceof WebAssembly.RuntimeError,
      "a fixed enumerable toJSON property on an SPP struct must retain Rust's decode trap",
    );
    assert.equal(sppItemWithFixedToJSONError.thrown.message, "unreachable");

    const sppItemWithHiddenFixedToJSON = structuredClone(fixture);
    sppItemWithHiddenFixedToJSON.min_item_separation = -1;
    defineFixedToJSON(sppItemWithHiddenFixedToJSON.items[0], false);
    const sppItemWithHiddenFixedToJSONError = captureThrownValue(() =>
      module.run_lbf_spp_wasm(sppItemWithHiddenFixedToJSON, { n_samples: 1, ls_frac: 0 }),
    );
    assert.equal(
      sppItemWithHiddenFixedToJSONError.thrown,
      importerError,
      "a non-enumerable fixed toJSON property on an SPP struct must be ignored like Rust",
    );

    const lowBudgetSppTrap = captureThrownValue(() =>
      module.run_lbf_spp_wasm(fixture, {
        prng_seed: 0x12345678,
        n_samples: 2,
        ls_frac: 0,
      }),
    );
    assert.ok(
      lowBudgetSppTrap.thrown instanceof WebAssembly.RuntimeError,
      "SPP width runaway must preserve Rust's fail-fast trap channel",
    );
    assert.equal(lowBudgetSppTrap.thrown.message, "unreachable");

    const bigintSppNumericFields = structuredClone(fixture);
    bigintSppNumericFields.min_item_separation = -1;
    bigintSppNumericFields.items[0].id = 201n;
    bigintSppNumericFields.items[0].demand = 2n;
    bigintSppNumericFields.items[0].shape.data.width = 2n;
    const bigintSppNumericFieldsError = captureThrownValue(() =>
      module.run_lbf_spp_wasm(bigintSppNumericFields, {
        n_samples: 1,
        ls_frac: 0,
      }),
    );
    assert.equal(
      bigintSppNumericFieldsError.thrown,
      importerError,
      "SPP integer and shape-f32 BigInt fields accepted by Rust must reach the Importer",
    );

    const bigintSppStripHeight = structuredClone(fixture);
    bigintSppStripHeight.min_item_separation = -1;
    bigintSppStripHeight.strip_height = 4n;
    const bigintSppStripHeightError = captureThrownValue(() =>
      module.run_lbf_spp_wasm(bigintSppStripHeight, {
        n_samples: 1,
        ls_frac: 0,
      }),
    );
    assert.ok(
      bigintSppStripHeightError.thrown instanceof WebAssembly.RuntimeError,
      "BigInt in the pinned f32 strip_height field must preserve Rust's decode trap channel",
    );
    assert.equal(bigintSppStripHeightError.thrown.message, "unreachable");

    const inheritedSppStripHeight = structuredClone(fixture);
    inheritedSppStripHeight.min_item_separation = -1;
    const inheritedStripHeightValue = inheritedSppStripHeight.strip_height;
    delete inheritedSppStripHeight.strip_height;
    const inheritedStripHeightPrototype = {};
    Object.defineProperty(inheritedStripHeightPrototype, "strip_height", {
      value: inheritedStripHeightValue,
    });
    Object.setPrototypeOf(inheritedSppStripHeight, inheritedStripHeightPrototype);
    const inheritedSppStripHeightError = captureThrownValue(() =>
      module.run_lbf_spp_wasm(inheritedSppStripHeight, {
        n_samples: 1,
        ls_frac: 0,
      }),
    );
    assert.equal(
      inheritedSppStripHeightError.thrown,
      importerError,
      "non-enumerable inherited root strip_height must remain visible to Rust Serde",
    );

    const undefinedSppMinSeparation = structuredClone(fixture);
    undefinedSppMinSeparation.min_item_separation = undefined;
    const undefinedSppMinSeparationError = captureThrownValue(() =>
      module.run_lbf_spp_wasm(undefinedSppMinSeparation, {
        n_samples: 1,
        ls_frac: 0,
      }),
    );
    assert.ok(
      undefinedSppMinSeparationError.thrown instanceof WebAssembly.RuntimeError,
      "explicit undefined in the defaulted SPP f32 field must preserve Rust's decode trap",
    );
    assert.equal(undefinedSppMinSeparationError.thrown.message, "unreachable");

    const optionalUndefinedInstance = structuredClone(fixture);
    optionalUndefinedInstance.items[0].demand = 1;
    const optionalUndefinedResult = captureThrownValue(() =>
      module.run_lbf_spp_wasm(optionalUndefinedInstance, {
        prng_seed: undefined,
        n_samples: 4,
        ls_frac: 0,
        poly_simpl_tolerance: undefined,
        narrow_concavity_cutoff: undefined,
      }),
    );
    assert.equal(optionalUndefinedResult.thrown, undefined);
    assert.equal(optionalUndefinedResult.value.svgs.length, 1);
    for (const name of [
      "poly_simpl_tolerance",
      "narrow_concavity_cutoff",
      "prng_seed",
    ]) {
      assert.equal(
        Object.hasOwn(optionalUndefinedResult.value.output.config, name),
        true,
        `an absent Rust Option value must remain an own ${name} property`,
      );
      assert.equal(
        optionalUndefinedResult.value.output.config[name],
        undefined,
        `an absent Rust Option value must serialize to undefined for ${name}`,
      );
    }

    const inheritedConfig = Object.create({
      prng_seed: 42,
      n_samples: 4,
      ls_frac: 0,
    });
    const inheritedConfigResult = captureThrownValue(() =>
      module.run_lbf_spp_wasm(optionalUndefinedInstance, inheritedConfig),
    );
    assert.equal(inheritedConfigResult.thrown, undefined);
    assert.equal(inheritedConfigResult.value.output.config.prng_seed, 42);
    assert.equal(inheritedConfigResult.value.output.config.n_samples, 4);

    const typedArrayBaseConfig = {
      ...fullConfig,
      prng_seed: 42,
      n_samples: 1,
      ls_frac: 0,
    };
    const acceptedTypedArrayConfigs = [
      [
        "Float32Array narrow-concavity tuple",
        {
          ...typedArrayBaseConfig,
          narrow_concavity_cutoff: new Float32Array([0.01, 0.02]),
        },
      ],
      [
        "Float64Array pole-limit tuple",
        {
          ...typedArrayBaseConfig,
          cde_config: {
            ...fullConfig.cde_config,
            item_surrogate_config: {
              ...fullConfig.cde_config.item_surrogate_config,
              n_pole_limits: [
                new Float64Array([100, 0]),
                [20, 0.75],
                [10, 0.9],
              ],
            },
          },
        },
      ],
    ];
    for (const [name, config] of acceptedTypedArrayConfigs) {
      warnings.length = 0;
      const result = captureThrownValue(
        () => module.run_lbf_spp_wasm(invalidInstance, config),
      );
      assert.equal(result.thrown, importerError, name + " should reach the Importer");
      assert.deepEqual(warnings, [], name + " should not trigger config fallback");
    }
    for (const field of ["name", "items", "strip_height"]) {
      for (const state of ["missing", "undefined"]) {
        const sppRequiredFieldInput = structuredClone(fixture);
        if (state === "missing") delete sppRequiredFieldInput[field];
        else sppRequiredFieldInput[field] = undefined;
        const result = captureThrownValue(() =>
          module.run_lbf_spp_wasm(sppRequiredFieldInput, {
            n_samples: 1,
            ls_frac: 0,
          }),
        );
        assert.ok(
          result.thrown instanceof WebAssembly.RuntimeError,
          `SPP required field ${field} (${state}) must preserve Rust's decode trap`,
        );
        assert.equal(
          result.thrown.message,
          "unreachable",
          `SPP required field ${field} (${state}) must use Rust's trap message`,
        );
      }
    }

    const duplicateIdInstance = structuredClone(fixture);
    duplicateIdInstance.items.push(structuredClone(duplicateIdInstance.items[0]));
    const invalidHeightInstance = { ...structuredClone(fixture), strip_height: 0 };
    const invalidRectangleInstance = structuredClone(fixture);
    invalidRectangleInstance.items[0].shape.data.width = 0;
    const fractionalRectangleInstance = structuredClone(fixture);
    Object.assign(fractionalRectangleInstance.items[0].shape.data, {
      x_min: 0.1,
      y_min: 0.2,
      width: 0,
      height: 0.3,
    });
    const invalidRotationInstance = structuredClone(fixture);
    invalidRotationInstance.items[0].orientation.rotation.angles = [];
    for (const [name, instance, expected] of [
      ["duplicate item id", duplicateIdInstance, "Importer error: item IDs must be unique"],
      ["nonpositive strip height", invalidHeightInstance, "Importer error: strip height must be positive"],
      [
        "zero-width rectangle",
        invalidRectangleInstance,
        "Importer error: invalid rectangle, x_min: 0, x_max: 0, y_min: 0, y_max: 2",
      ],
      [
        "fractional zero-width rectangle",
        fractionalRectangleInstance,
        "Importer error: invalid rectangle, x_min: 0.1, x_max: 0.1, y_min: 0.2, y_max: 0.5",
      ],
      [
        "empty discrete rotation",
        invalidRotationInstance,
        "Importer error: discrete rotation requires 1..=65536 angles",
      ],
    ]) {
      const result = captureThrownValue(() =>
        module.run_lbf_spp_wasm(instance, { n_samples: 1, ls_frac: 0 }),
      );
      assert.equal(result.thrown, expected, `${name} must match the pinned Rust Importer text`);
    }

    const shapeTypeMutatesData = structuredClone(fixture);
    shapeTypeMutatesData.min_item_separation = 0;
    const shapeReadEvents = [];
    let rectangleData = { x_min: 0, y_min: 0, width: 2, height: 2 };
    const shapeWithGetter = {};
    Object.defineProperty(shapeWithGetter, "type", {
      enumerable: true,
      get() {
        shapeReadEvents.push("type");
        rectangleData.width = 0;
        return "rectangle";
      },
    });
    Object.defineProperty(shapeWithGetter, "data", {
      enumerable: true,
      get() {
        shapeReadEvents.push("data");
        return rectangleData;
      },
    });
    shapeTypeMutatesData.items[0].shape = shapeWithGetter;
    const shapeGetterMutationError = captureThrownValue(() =>
      module.run_lbf_spp_wasm(shapeTypeMutatesData, {
        n_samples: 64,
        ls_frac: 0,
      }),
    );
    assert.equal(
      shapeGetterMutationError.thrown,
      "Importer error: invalid rectangle, x_min: 0, x_max: 0, y_min: 0, y_max: 2",
      "the shape data getter must observe the earlier type getter mutation",
    );
    assert.deepEqual(shapeReadEvents, ["type", "data"]);

    const rotationModeMutatesAngles = structuredClone(fixture);
    rotationModeMutatesAngles.min_item_separation = 0;
    const rotationReadEvents = [];
    let permittedAngles = [0];
    const rotationWithGetter = {};
    Object.defineProperty(rotationWithGetter, "mode", {
      enumerable: true,
      get() {
        rotationReadEvents.push("mode");
        permittedAngles = [];
        return "discrete";
      },
    });
    Object.defineProperty(rotationWithGetter, "angles", {
      enumerable: true,
      get() {
        rotationReadEvents.push("angles");
        return permittedAngles;
      },
    });
    rotationModeMutatesAngles.items[0].orientation.rotation = rotationWithGetter;
    const rotationGetterMutationError = captureThrownValue(() =>
      module.run_lbf_spp_wasm(rotationModeMutatesAngles, {
        n_samples: 64,
        ls_frac: 0,
      }),
    );
    assert.equal(
      rotationGetterMutationError.thrown,
      "Importer error: discrete rotation requires 1..=65536 angles",
      "the rotation angles getter must observe the earlier mode getter mutation",
    );
    assert.deepEqual(rotationReadEvents, ["mode", "angles"]);

    warnings.length = 0;
    const maxSeedResult = captureThrownValue(() =>
      module.run_lbf_spp_wasm(fixture, {
        prng_seed: 18446744073709551615n,
        n_samples: 64,
        ls_frac: 0.25,
      }),
    );
    assert.equal(
      maxSeedResult.thrown,
      "Result encode error: Error: 18446744073709551615 can't be represented as a JavaScript number",
      "an accepted BigInt u64 seed must follow Rust's default JS-number serialization failure",
    );
    assert.deepEqual(warnings, []);

    for (const [name, config] of [
      ["u64 overflow", { prng_seed: 18446744073709551616n }],
      ["negative u64", { prng_seed: -1n }],
      ["u8 BigInt", { cde_config: { ...fullConfig.cde_config, quadtree_depth: 5n } }],
      ["usize BigInt", { n_samples: 64n }],
      ["u8 overflow", { cde_config: { ...fullConfig.cde_config, quadtree_depth: 256 } }],
      ["wasm32 usize overflow", { n_samples: 4294967296 }],
      ["unsafe Number", { prng_seed: Number.MAX_SAFE_INTEGER + 1 }],
      ["undefined config", undefined],
      ["null config", null],
      ["undefined n_samples", { prng_seed: 42, n_samples: undefined }],
      ["undefined ls_frac", { prng_seed: 42, ls_frac: undefined }],
      ["undefined CDE config", { prng_seed: 42, cde_config: undefined }],
      ["undefined SVG options", { prng_seed: 42, svg_draw_options: undefined }],
      ["undefined SVG flag", { prng_seed: 42, svg_draw_options: { quadtree: undefined } }],
      ["undefined SVG theme", { prng_seed: 42, svg_draw_options: { theme: undefined } }],
      ["DataView tuple", { ...fullConfig, narrow_concavity_cutoff: new DataView(new ArrayBuffer(8)) }],
      ["BigInt typed-array tuple", { ...fullConfig, narrow_concavity_cutoff: new BigInt64Array([1n, 1n]) }],
    ]) {
      warnings.length = 0;
      const result = captureThrownValue(
        () => module.run_lbf_spp_wasm(invalidInstance, config),
      );
      assert.equal(result.thrown, importerError, `${name} should continue with defaults`);
      assert.equal(warnings.length, 1, `${name} should warn on whole-config fallback`);
      assert.match(warnings[0], /Invalid config, using default\./);
    }

    const bppFixture = JSON.parse(
      fs.readFileSync(path.join(__dirname, "Fixtures", "LbfBppSmall.json"), "utf8"),
    );
    let sppIgnoredBinsGets = 0;
    const sppUnknownFieldInput = structuredClone(fixture);
    sppUnknownFieldInput.min_item_separation = -1;
    const sppWithThrowingBppOnlyField = new Proxy(sppUnknownFieldInput, {
      get(target, key, receiver) {
        if (key === "bins") {
          sppIgnoredBinsGets += 1;
          throw new Error("Rust SPP instance decoding must ignore bins");
        }
        return Reflect.get(target, key, receiver);
      },
    });
    const sppIgnoredBppFieldResult = captureThrownValue(() =>
      module.run_lbf_spp_wasm(sppWithThrowingBppOnlyField, {
        n_samples: 1,
        ls_frac: 0,
      }),
    );
    assert.equal(
      sppIgnoredBppFieldResult.thrown,
      importerError,
      "SPP instance decoding must not read the BPP-only bins field",
    );
    assert.equal(sppIgnoredBinsGets, 0, "SPP must not trigger the root bins getter");

    let bppIgnoredStripHeightGets = 0;
    const bppUnknownFieldInput = structuredClone(bppFixture);
    bppUnknownFieldInput.min_item_separation = -1;
    const bppWithThrowingSppOnlyField = new Proxy(bppUnknownFieldInput, {
      get(target, key, receiver) {
        if (key === "strip_height") {
          bppIgnoredStripHeightGets += 1;
          throw new Error("Rust BPP instance decoding must ignore strip_height");
        }
        return Reflect.get(target, key, receiver);
      },
    });
    const bppIgnoredSppFieldResult = captureThrownValue(() =>
      module.run_lbf_bpp_wasm(bppWithThrowingSppOnlyField, {
        n_samples: 1,
        ls_frac: 0,
      }),
    );
    assert.equal(
      bppIgnoredSppFieldResult.thrown,
      importerError,
      "BPP instance decoding must not read the SPP-only strip_height field",
    );
    assert.equal(
      bppIgnoredStripHeightGets,
      0,
      "BPP must not trigger the root strip_height getter",
    );

    const bppSimplePolygonErrors = [
      [
        "two points",
        [[0, 0], [1, 0]],
        "Importer error: Simple polygon must have at least 3 points: [Point(0.0, 0.0), Point(1.0, 0.0)]",
      ],
      [
        "zero area",
        [[0, 0], [1, 0], [2, 0]],
        "Importer error: Simple polygon has no area: [Point(0.0, 0.0), Point(1.0, 0.0), Point(2.0, 0.0)]",
      ],
      [
        "fractional zero area",
        [[0.1, 0], [0.2, 0], [0.3, 0]],
        "Importer error: Simple polygon has no area: [Point(0.1, 0.0), Point(0.2, 0.0), Point(0.3, 0.0)]",
      ],
      [
        "f32 duplicate collapse and exponent formatting",
        [[0, 0], [1e-7, 0], [2e-7, 0]],
        "Importer error: Simple polygon must have at least 3 points: [Point(2e-7, 0.0)]",
      ],
      [
        "intersecting edges",
        [[0, 0], [2, 2], [0, 2], [2, 0]],
        "Importer error: Simple polygon contains intersecting edges 0 and 2: [Point(0.0, 0.0), Point(2.0, 2.0), Point(0.0, 2.0), Point(2.0, 0.0)]",
      ],
    ];
    for (const [name, points, expectedError] of bppSimplePolygonErrors) {
      const invalidPolygonItem = structuredClone(bppFixture);
      invalidPolygonItem.items[0].shape = {
        type: "simple_polygon",
        data: points,
      };
      const result = captureThrownValue(() =>
        module.run_lbf_bpp_wasm(invalidPolygonItem, {
          n_samples: 1,
          ls_frac: 0,
        }),
      );
      assert.equal(result.thrown, expectedError, `Rust text mismatch for ${name}`);
    }

    const invalidPointTupleCases = [
      ["SPP Float32Array point", "SPP", "simple_polygon", () => new Float32Array([2, 0]), "invalid type: JsValue(Float32Array), expected any value"],
      ["SPP Uint8Array point", "SPP", "simple_polygon", () => new Uint8Array([2, 0]), "invalid type: byte array, expected a tuple of size 2"],
      ["BPP Map point", "BPP", "polygon", () => new Map([[0, 2], [1, 0]]), "invalid type: map, expected a tuple of size 2"],
      ["BPP DataView point", "BPP", "polygon", () => new DataView(new ArrayBuffer(8)), "invalid type: map, expected a tuple of size 2"],
    ];
    for (const [name, kind, shapeKind, makePoint, expectedError] of invalidPointTupleCases) {
      const instance = structuredClone(kind === "SPP" ? fixture : bppFixture);
      const points = [[0, 0], makePoint(), [2, 2], [0, 2]];
      instance.items[0].shape = shapeKind === "simple_polygon"
        ? { type: shapeKind, data: points }
        : { type: shapeKind, data: { outer: points, inner: [] } };
      warnings.length = 0;
      const result = captureThrownValue(() =>
        kind === "SPP"
          ? module.run_lbf_spp_wasm(instance, { n_samples: 1, ls_frac: 0 })
          : module.run_lbf_bpp_wasm(instance, { n_samples: 1, ls_frac: 0 }),
      );
      assert.ok(result.thrown instanceof WebAssembly.RuntimeError, `${name} should trap`);
      assert.equal(result.thrown.message, "unreachable", `${name} trap text`);
      assert.deepEqual(warnings, [
        `Ext${kind === "SPP" ? "SP" : "BP"}Instance decode error: Error: ${expectedError}, not using fallback!!`,
      ], `${name} Serde warning`);
    }
    warnings.length = 0;

    const invalidPointSequenceCases = [
      [
        "SPP simple_polygon Set container",
        "SPP",
        { type: "simple_polygon", data: new Set([[0, 0], [2, 0]]) },
        "invalid type: JsValue(Set), expected any value",
      ],
      [
        "BPP polygon outer Map container",
        "BPP",
        { type: "polygon", data: { outer: new Map([[0, [0, 0]]]), inner: [] } },
        "invalid type: map, expected a sequence",
      ],
      [
        "BPP polygon inner null container",
        "BPP",
        { type: "polygon", data: { outer: [[0, 0], [2, 0], [2, 2]], inner: null } },
        "invalid type: unit value, expected a sequence",
      ],
    ];
    for (const [name, kind, shape, expectedError] of invalidPointSequenceCases) {
      const instance = structuredClone(kind === "SPP" ? fixture : bppFixture);
      if (kind === "SPP")
        instance.items[0].shape = shape;
      else
        instance.bins[0].shape = shape;
      warnings.length = 0;
      const result = captureThrownValue(() =>
        kind === "SPP"
          ? module.run_lbf_spp_wasm(instance, { n_samples: 1, ls_frac: 0 })
          : module.run_lbf_bpp_wasm(instance, { n_samples: 1, ls_frac: 0 }),
      );
      assert.ok(result.thrown instanceof WebAssembly.RuntimeError, `${name} should trap`);
      assert.equal(result.thrown.message, "unreachable", `${name} trap text`);
      assert.deepEqual(warnings, [
        `Ext${kind === "SPP" ? "SP" : "BP"}Instance decode error: Error: ${expectedError}, not using fallback!!`,
      ], `${name} Serde warning`);
    }
    warnings.length = 0;

    const invalidPointCoordinateCases = [
      [
        "SPP empty point tuple",
        "SPP",
        () => [],
        "invalid length 0, expected a tuple of size 2",
      ],
      [
        "SPP malformed first coordinate precedes missing tuple element",
        "SPP",
        () => [true],
        "invalid type: boolean `true`, expected f32",
      ],
      [
        "BPP short point tuple",
        "BPP",
        () => [2],
        "invalid length 1, expected a tuple of size 2",
      ],
      [
        "SPP long point tuple",
        "SPP",
        () => [2, 0, 3],
        "invalid length 3, expected 2 elements in sequence",
      ],
      [
        "BPP malformed first coordinate precedes extra tuple element",
        "BPP",
        () => [null, 0, 3],
        "invalid type: unit value, expected f32",
      ],
      [
        "BPP null point coordinate",
        "BPP",
        () => [null, 0],
        "invalid type: unit value, expected f32",
      ],
      [
        "SPP boolean point coordinate",
        "SPP",
        () => [true, 0],
        "invalid type: boolean `true`, expected f32",
      ],
      [
        "BPP Map point coordinate",
        "BPP",
        () => [new Map(), 0],
        "invalid type: map, expected f32",
      ],
      [
        "SPP Symbol point coordinate",
        "SPP",
        () => [Symbol("coordinate"), 0],
        "invalid type: JsValue(Symbol(coordinate)), expected any value",
      ],
    ];
    for (const [name, kind, makePoint, expectedError] of invalidPointCoordinateCases) {
      const instance = structuredClone(kind === "SPP" ? fixture : bppFixture);
      const points = [[0, 0], makePoint(), [2, 2], [0, 2]];
      const shape = { type: "simple_polygon", data: points };
      if (kind === "SPP")
        instance.items[0].shape = shape;
      else
        instance.bins[0].shape = shape;
      warnings.length = 0;
      const result = captureThrownValue(() =>
        kind === "SPP"
          ? module.run_lbf_spp_wasm(instance, { n_samples: 1, ls_frac: 0 })
          : module.run_lbf_bpp_wasm(instance, { n_samples: 1, ls_frac: 0 }),
      );
      assert.ok(result.thrown instanceof WebAssembly.RuntimeError, `${name} should trap`);
      assert.equal(result.thrown.message, "unreachable", `${name} trap text`);
      assert.deepEqual(warnings, [
        `Ext${kind === "SPP" ? "SP" : "BP"}Instance decode error: Error: ${expectedError}, not using fallback!!`,
      ], `${name} Serde warning`);
    }
    warnings.length = 0;

    const sppBigIntPointCoordinate = structuredClone(fixture);
    sppBigIntPointCoordinate.items[0].shape = {
      type: "simple_polygon",
      data: [[0, 0], [2n, 0], [2, 2], [0, 2]],
    };
    const sppBigIntPointCoordinateError = captureThrownValue(() =>
      module.run_lbf_spp_wasm(sppBigIntPointCoordinate, {
        n_samples: 1,
        ls_frac: 0,
      }),
    );
    assert.ok(
      sppBigIntPointCoordinateError.thrown instanceof WebAssembly.RuntimeError,
      "SPP BigInt point coordinates should continue through the Rust-compatible numeric decode path",
    );
    assert.equal(sppBigIntPointCoordinateError.thrown.message, "unreachable");
    assert.deepEqual(
      warnings,
      [],
      "BigInt point coordinates must not be rejected as a Serde type warning",
    );

    const makePrototypeSwitchPoint = (
      accesses,
      reads,
      prototypes,
      iteratorMethod = function* () { yield 1; },
      options = {},
    ) => {
      const target = {
        value: 1,
        [Symbol.iterator]: iteratorMethod,
      };
      const third = new Proxy(target, {
        has(target, key) {
          reads.thirdProxy.push(`has:${String(key)}`);
          return Reflect.has(target, key);
        },
        getPrototypeOf(target) {
          reads.thirdProxy.push("prototype");
          return prototypes.length > 0
            ? prototypes.shift()
            : Reflect.getPrototypeOf(target);
        },
        ownKeys(target) {
          reads.thirdProxy.push("ownKeys");
          return Reflect.ownKeys(target);
        },
        getOwnPropertyDescriptor(target, key) {
          reads.thirdProxy.push(`descriptor:${String(key)}`);
          return Reflect.getOwnPropertyDescriptor(target, key);
        },
        get(target, key, receiver) {
          reads.thirdProxy.push(`get:${String(key)}`);
          if (key === "length" && options.lengthThrowsAt !== undefined) {
            reads.thirdLengthReads = (reads.thirdLengthReads ?? 0) + 1;
            if (reads.thirdLengthReads === options.lengthThrowsAt)
              throw new Error(`length getter failed at ${reads.thirdLengthReads}`);
          }
          if (key === Symbol.iterator && options.iteratorGetterThrows)
            throw new Error("iterator getter failed");
          return Reflect.get(target, key, receiver);
        },
      });
      return new Proxy([2, 0, third], {
        get(target, key, receiver) {
          accesses.push(String(key));
          return Reflect.get(target, key, receiver);
        },
      });
    };
    const makePointProxyWithMapIterator = (accesses, reads, makeIterator) =>
      makePrototypeSwitchPoint(
        accesses,
        reads,
        [Object.prototype, Object.prototype, Map.prototype],
        function () {
          reads.thirdProxy.push("iterator-call");
          return makeIterator();
        },
      );
    const makeNextGetterIterator = (reads, makeNext) => {
      let nextReads = 0;
      const iterator = {};
      Object.defineProperty(iterator, "next", {
        get() {
          const read = ++nextReads;
          reads.thirdProxy.push(`next-get:${read}`);
          return makeNext(read);
        },
      });
      return iterator;
    };
    const makePointProxyWithMapEntry = (
      accesses,
      reads,
      makeKey,
      makeValue = () => 1,
    ) => makePointProxyWithMapIterator(
      accesses,
      reads,
      () => {
        let calls = 0;
        const iterator = makeNextGetterIterator(reads, () => () => {
          reads.thirdProxy.push("next-call");
          if (++calls > 1) {
            const result = {};
            Object.defineProperty(result, "done", {
              get() {
                reads.thirdProxy.push("done-get:end");
                return true;
              },
            });
            return result;
          }
          const pair = {};
          Object.defineProperty(pair, "0", {
            get() {
              reads.thirdProxy.push("pair-key-get");
              return makeKey();
            },
          });
          Object.defineProperty(pair, "1", {
            get() {
              reads.thirdProxy.push("pair-value-get");
              return makeValue();
            },
          });
          const result = {};
          Object.defineProperty(result, "done", {
            get() {
              reads.thirdProxy.push("done-get");
              return false;
            },
          });
          Object.defineProperty(result, "value", {
            get() {
              reads.thirdProxy.push("value-get");
              return pair;
            },
          });
          return result;
        });
        Object.defineProperty(iterator, "return", {
          get() {
            reads.thirdProxy.push("iterator-return-get");
            return function () {
              reads.thirdProxy.push("iterator-return-call");
              throw new Error("iterator return must not run");
            };
          },
        });
        return iterator;
      },
    );
    const makeFunctionKeyWithName = (reads, getName, proxyName = false) => {
      const key = function originalKey() {};
      if (proxyName) {
        return new Proxy(key, {
          get(target, property, receiver) {
            if (property === "name") {
              reads.thirdProxy.push("function-proxy-name-get");
              return getName();
            }
            return Reflect.get(target, property, receiver);
          },
        });
      }
      Object.defineProperty(key, "name", {
        configurable: true,
        get() {
          reads.thirdProxy.push("function-name-get");
          return getName();
        },
      });
      return key;
    };
    const makeOpaqueFunctionNameProxy = (reads) => new Proxy({}, {
      get(target, property) {
        reads.thirdProxy.push(`function-name-value-get:${String(property)}`);
        throw new Error("function name value must not be coerced");
      },
      getPrototypeOf() {
        reads.thirdProxy.push("function-name-value-prototype");
        throw new Error("function name value must not be inspected");
      },
      ownKeys() {
        reads.thirdProxy.push("function-name-value-ownKeys");
        throw new Error("function name value must not be inspected");
      },
    });
    const functionNameReadEvents = (...events) => [
      "prototype",
      "prototype",
      "has:Symbol(Symbol.iterator)",
      "prototype",
      "get:Symbol(Symbol.iterator)",
      "iterator-call",
      "next-get:1",
      "next-get:2",
      "next-call",
      "done-get",
      "value-get",
      "pair-key-get",
      "pair-value-get",
      ...events,
    ];
    const rawNameThrownProxyEvents = [];
    const rawNameThrownProxy = new Proxy({}, {
      get(target, property) {
        rawNameThrownProxyEvents.push(`get:${String(property)}`);
        throw new Error("raw name getter exception must stay opaque");
      },
      getPrototypeOf() {
        rawNameThrownProxyEvents.push("getPrototypeOf");
        throw new Error("raw name getter exception must stay opaque");
      },
      ownKeys() {
        rawNameThrownProxyEvents.push("ownKeys");
        throw new Error("raw name getter exception must stay opaque");
      },
      getOwnPropertyDescriptor(target, property) {
        rawNameThrownProxyEvents.push(`getOwnPropertyDescriptor:${String(property)}`);
        throw new Error("raw name getter exception must stay opaque");
      },
      has(target, property) {
        rawNameThrownProxyEvents.push(`has:${String(property)}`);
        throw new Error("raw name getter exception must stay opaque");
      },
    });
    const pointTupleProxyCases = [
      [
        "index getter mutates next coordinate",
        (accesses) => new Proxy([2, 0], {
          get(target, key, receiver) {
            accesses.push(String(key));
            if (key === "0")
              target[1] = "bad";
            return Reflect.get(target, key, receiver);
          },
        }),
        ["length", "0", "1"],
        "invalid type: string \"bad\", expected f32",
      ],
      [
        "index getter shrinks tuple after iterator length snapshot",
        (accesses) => new Proxy([2, 0], {
          get(target, key, receiver) {
            accesses.push(String(key));
            if (key === "0")
              target.length = 1;
            return Reflect.get(target, key, receiver);
          },
        }),
        ["length", "0", "1"],
        "invalid type: unit value, expected f32",
      ],
      [
        "extra tuple evaluates nested third value before length rejection",
        (accesses, reads) => {
          const third = {
            get value() {
              reads.thirdValue += 1;
              throw new Error("ignored tuple element must not be inspected");
            },
          };
          return new Proxy([2, 0, third], {
            get(target, key, receiver) {
              accesses.push(String(key));
              return Reflect.get(target, key, receiver);
            },
          });
        },
        ["length", "0", "1", "2"],
        "ignored tuple element must not be inspected",
      ],
      [
        "extra tuple plain object Proxy follows Serde materialization order",
        (accesses, reads) => {
          const third = new Proxy({ value: 1 }, {
            has(target, key) {
              reads.thirdProxy.push(`has:${String(key)}`);
              return Reflect.has(target, key);
            },
            getPrototypeOf(target) {
              reads.thirdProxy.push("prototype");
              return Reflect.getPrototypeOf(target);
            },
            ownKeys(target) {
              reads.thirdProxy.push("ownKeys");
              return Reflect.ownKeys(target);
            },
            getOwnPropertyDescriptor(target, key) {
              reads.thirdProxy.push(`descriptor:${String(key)}`);
              return Reflect.getOwnPropertyDescriptor(target, key);
            },
            get(target, key, receiver) {
              reads.thirdProxy.push(`get:${String(key)}`);
              return Reflect.get(target, key, receiver);
            },
          });
          return new Proxy([2, 0, third], {
            get(target, key, receiver) {
              accesses.push(String(key));
              return Reflect.get(target, key, receiver);
            },
          });
        },
        ["length", "0", "1", "2"],
        "invalid length 3, expected 2 elements in sequence",
      ],
      [
        "extra tuple custom iterable Proxy preserves JsValue debug order",
        (accesses, reads) => {
          const third = new Proxy({
            value: 1,
            [Symbol.iterator]: function* () { yield 1; },
          }, {
            has(target, key) {
              reads.thirdProxy.push(`has:${String(key)}`);
              return Reflect.has(target, key);
            },
            getPrototypeOf(target) {
              reads.thirdProxy.push("prototype");
              return Reflect.getPrototypeOf(target);
            },
            ownKeys(target) {
              reads.thirdProxy.push("ownKeys");
              return Reflect.ownKeys(target);
            },
            getOwnPropertyDescriptor(target, key) {
              reads.thirdProxy.push(`descriptor:${String(key)}`);
              return Reflect.getOwnPropertyDescriptor(target, key);
            },
            get(target, key, receiver) {
              reads.thirdProxy.push(`get:${String(key)}`);
              return Reflect.get(target, key, receiver);
            },
          });
          return new Proxy([2, 0, third], {
            get(target, key, receiver) {
              accesses.push(String(key));
              return Reflect.get(target, key, receiver);
            },
          });
        },
        ["length", "0", "1", "2"],
        "invalid type: JsValue(Object({\"value\":1})), expected any value",
      ],
      [
        "extra tuple iterable Proxy catches prototype errors during classification",
        (accesses, reads) => {
          const third = new Proxy({
            value: 1,
            [Symbol.iterator]: function* () { yield 1; },
          }, {
            has(target, key) {
              reads.thirdProxy.push(`has:${String(key)}`);
              return Reflect.has(target, key);
            },
            getPrototypeOf() {
              reads.thirdProxy.push("prototype");
              throw new Error("prototype trap failed");
            },
            ownKeys(target) {
              reads.thirdProxy.push("ownKeys");
              return Reflect.ownKeys(target);
            },
            getOwnPropertyDescriptor(target, key) {
              reads.thirdProxy.push(`descriptor:${String(key)}`);
              return Reflect.getOwnPropertyDescriptor(target, key);
            },
            get(target, key, receiver) {
              reads.thirdProxy.push(`get:${String(key)}`);
              return Reflect.get(target, key, receiver);
            },
          });
          return new Proxy([2, 0, third], {
            get(target, key, receiver) {
              accesses.push(String(key));
              return Reflect.get(target, key, receiver);
            },
          });
        },
        ["length", "0", "1", "2"],
        "invalid type: JsValue(Object({\"value\":1})), expected any value",
      ],
      [
        "extra tuple Proxy changing prototype to Uint8Array preserves byte reads",
        (accesses, reads) => makePrototypeSwitchPoint(
          accesses,
          reads,
          [Uint8Array.prototype],
        ),
        ["length", "0", "1", "2"],
        "invalid length 3, expected 2 elements in sequence",
        ["prototype", "get:length", "get:length", "get:length", "get:length"],
      ],
      [
        "extra tuple Proxy changing prototype to ArrayBuffer preserves byte iteration",
        (accesses, reads) => makePrototypeSwitchPoint(
          accesses,
          reads,
          [Object.prototype, ArrayBuffer.prototype],
        ),
        ["length", "0", "1", "2"],
        "invalid length 3, expected 2 elements in sequence",
        ["prototype", "prototype", "get:Symbol(Symbol.iterator)"],
      ],
      [
        "extra tuple Proxy changing prototype to Map buffers its iterator",
        (accesses, reads) => makePrototypeSwitchPoint(
          accesses,
          reads,
          [Object.prototype, Object.prototype, Map.prototype],
        ),
        ["length", "0", "1", "2"],
        "invalid length 3, expected 2 elements in sequence",
        [
          "prototype",
          "prototype",
          "has:Symbol(Symbol.iterator)",
          "prototype",
          "get:Symbol(Symbol.iterator)",
        ],
      ],
      [
        "extra tuple Proxy Map iterator errors precede tuple length",
        (accesses, reads) => makePrototypeSwitchPoint(
          accesses,
          reads,
          [Object.prototype, Object.prototype, Map.prototype],
          function () {
            return {
              next() {
                reads.thirdProxy.push("next");
                throw new Error("iterator next failed");
              },
            };
          },
        ),
        ["length", "0", "1", "2"],
        "iterator next failed",
        [
          "prototype",
          "prototype",
          "has:Symbol(Symbol.iterator)",
          "prototype",
          "get:Symbol(Symbol.iterator)",
          "next",
        ],
      ],
      [
        "extra tuple Proxy Uint8Array second length getter error stays raw",
        (accesses, reads) => makePrototypeSwitchPoint(
          accesses,
          reads,
          [Uint8Array.prototype],
          undefined,
          { lengthThrowsAt: 2 },
        ),
        ["length", "0", "1", "2"],
        "length getter failed at 2",
        ["prototype", "get:length", "get:length"],
        { rawError: true },
      ],
      [
        "extra tuple Proxy Uint8Array fourth length getter error stays raw",
        (accesses, reads) => makePrototypeSwitchPoint(
          accesses,
          reads,
          [Uint8Array.prototype],
          undefined,
          { lengthThrowsAt: 4 },
        ),
        ["length", "0", "1", "2"],
        "length getter failed at 4",
        ["prototype", "get:length", "get:length", "get:length", "get:length"],
        { rawError: true },
      ],
      [
        "extra tuple Proxy ArrayBuffer iterator getter error stays raw",
        (accesses, reads) => makePrototypeSwitchPoint(
          accesses,
          reads,
          [Object.prototype, ArrayBuffer.prototype],
          undefined,
          { iteratorGetterThrows: true },
        ),
        ["length", "0", "1", "2"],
        "iterator getter failed",
        ["prototype", "prototype", "get:Symbol(Symbol.iterator)"],
        { rawError: true },
      ],
      [
        "extra tuple Proxy ArrayBuffer iterator method error stays raw",
        (accesses, reads) => makePrototypeSwitchPoint(
          accesses,
          reads,
          [Object.prototype, ArrayBuffer.prototype],
          function () {
            reads.thirdProxy.push("iterator-call");
            throw new Error("iterator method failed");
          },
        ),
        ["length", "0", "1", "2"],
        "iterator method failed",
        [
          "prototype",
          "prototype",
          "get:Symbol(Symbol.iterator)",
          "iterator-call",
        ],
        { rawError: true },
      ],
      [
        "extra tuple Proxy ArrayBuffer next getter error stays raw",
        (accesses, reads) => makePrototypeSwitchPoint(
          accesses,
          reads,
          [Object.prototype, ArrayBuffer.prototype],
          function () {
            reads.thirdProxy.push("iterator-call");
            const iterator = {};
            Object.defineProperty(iterator, "next", {
              get() {
                reads.thirdProxy.push("next-get");
                throw new Error("iterator next getter failed");
              },
            });
            return iterator;
          },
        ),
        ["length", "0", "1", "2"],
        "iterator next getter failed",
        [
          "prototype",
          "prototype",
          "get:Symbol(Symbol.iterator)",
          "iterator-call",
          "next-get",
        ],
        { rawError: true },
      ],
      [
        "extra tuple Proxy Map iterator getter error warns and traps",
        (accesses, reads) => makePrototypeSwitchPoint(
          accesses,
          reads,
          [Object.prototype, Object.prototype, Map.prototype],
          undefined,
          { iteratorGetterThrows: true },
        ),
        ["length", "0", "1", "2"],
        "iterator getter failed",
        [
          "prototype",
          "prototype",
          "has:Symbol(Symbol.iterator)",
          "prototype",
          "get:Symbol(Symbol.iterator)",
        ],
      ],
      [
        "extra tuple Proxy Map iterator method error warns and traps",
        (accesses, reads) => makePrototypeSwitchPoint(
          accesses,
          reads,
          [Object.prototype, Object.prototype, Map.prototype],
          function () {
            reads.thirdProxy.push("iterator-call");
            throw new Error("iterator method failed");
          },
        ),
        ["length", "0", "1", "2"],
        "iterator method failed",
        [
          "prototype",
          "prototype",
          "has:Symbol(Symbol.iterator)",
          "prototype",
          "get:Symbol(Symbol.iterator)",
          "iterator-call",
        ],
      ],
      [
        "extra tuple Proxy Map next getter error stays raw",
        (accesses, reads) => makePrototypeSwitchPoint(
          accesses,
          reads,
          [Object.prototype, Object.prototype, Map.prototype],
          function () {
            reads.thirdProxy.push("iterator-call");
            const iterator = {};
            Object.defineProperty(iterator, "next", {
              get() {
                reads.thirdProxy.push("next-get");
                throw new Error("iterator next getter failed");
              },
            });
            return iterator;
          },
        ),
        ["length", "0", "1", "2"],
        "iterator next getter failed",
        [
          "prototype",
          "prototype",
          "has:Symbol(Symbol.iterator)",
          "prototype",
          "get:Symbol(Symbol.iterator)",
          "iterator-call",
          "next-get",
        ],
        { rawError: true },
      ],
      [
        "extra tuple Proxy Map nonfunction first next falls through to tuple length",
        (accesses, reads) => makePointProxyWithMapIterator(
          accesses,
          reads,
          () => makeNextGetterIterator(reads, () => 0),
        ),
        ["length", "0", "1", "2"],
        "invalid length 3, expected 2 elements in sequence",
        [
          "prototype",
          "prototype",
          "has:Symbol(Symbol.iterator)",
          "prototype",
          "get:Symbol(Symbol.iterator)",
          "iterator-call",
          "next-get:1",
        ],
      ],
      [
        "extra tuple Proxy Map second next getter nonfunction warns and traps",
        (accesses, reads) => makePointProxyWithMapIterator(
          accesses,
          reads,
          () => makeNextGetterIterator(reads, read => read === 1 ? () => {} : 0),
        ),
        ["length", "0", "1", "2"],
        "arg0.next is not a function",
        [
          "prototype",
          "prototype",
          "has:Symbol(Symbol.iterator)",
          "prototype",
          "get:Symbol(Symbol.iterator)",
          "iterator-call",
          "next-get:1",
          "next-get:2",
        ],
        { warningName: "TypeError" },
      ],
      [
        "extra tuple Proxy Map IteratorResult done getter error stays raw",
        (accesses, reads) => makePointProxyWithMapIterator(
          accesses,
          reads,
          () => makeNextGetterIterator(reads, () => () => {
            reads.thirdProxy.push("next-call");
            const result = {};
            Object.defineProperty(result, "done", {
              get() {
                reads.thirdProxy.push("done-get");
                throw new Error("IteratorResult done getter failed");
              },
            });
            return result;
          }),
        ),
        ["length", "0", "1", "2"],
        "IteratorResult done getter failed",
        [
          "prototype",
          "prototype",
          "has:Symbol(Symbol.iterator)",
          "prototype",
          "get:Symbol(Symbol.iterator)",
          "iterator-call",
          "next-get:1",
          "next-get:2",
          "next-call",
          "done-get",
        ],
        { rawError: true },
      ],
      [
        "extra tuple Proxy Map IteratorResult value getter follows done",
        (accesses, reads) => makePointProxyWithMapIterator(
          accesses,
          reads,
          () => makeNextGetterIterator(reads, () => () => {
            reads.thirdProxy.push("next-call");
            const result = {};
            Object.defineProperty(result, "done", {
              get() {
                reads.thirdProxy.push("done-get");
                return false;
              },
            });
            Object.defineProperty(result, "value", {
              get() {
                reads.thirdProxy.push("value-get");
                throw new Error("IteratorResult value getter failed");
              },
            });
            return result;
          }),
        ),
        ["length", "0", "1", "2"],
        "IteratorResult value getter failed",
        [
          "prototype",
          "prototype",
          "has:Symbol(Symbol.iterator)",
          "prototype",
          "get:Symbol(Symbol.iterator)",
          "iterator-call",
          "next-get:1",
          "next-get:2",
          "next-call",
          "done-get",
          "value-get",
        ],
        { rawError: true },
      ],
      [
        "extra tuple Proxy Map pair key getter precedes value access",
        (accesses, reads) => makePointProxyWithMapIterator(
          accesses,
          reads,
          () => makeNextGetterIterator(reads, () => () => {
            reads.thirdProxy.push("next-call");
            const pair = {};
            Object.defineProperty(pair, "0", {
              get() {
                reads.thirdProxy.push("pair-key-get");
                throw new Error("pair key getter failed");
              },
            });
            const result = {};
            Object.defineProperty(result, "done", {
              get() {
                reads.thirdProxy.push("done-get");
                return false;
              },
            });
            Object.defineProperty(result, "value", {
              get() {
                reads.thirdProxy.push("value-get");
                return pair;
              },
            });
            return result;
          }),
        ),
        ["length", "0", "1", "2"],
        "pair key getter failed",
        [
          "prototype",
          "prototype",
          "has:Symbol(Symbol.iterator)",
          "prototype",
          "get:Symbol(Symbol.iterator)",
          "iterator-call",
          "next-get:1",
          "next-get:2",
          "next-call",
          "done-get",
          "value-get",
          "pair-key-get",
        ],
        { rawError: true },
      ],
      [
        "extra tuple Proxy Map pair value getter follows key",
        (accesses, reads) => makePointProxyWithMapIterator(
          accesses,
          reads,
          () => makeNextGetterIterator(reads, () => () => {
            reads.thirdProxy.push("next-call");
            const pair = {};
            Object.defineProperty(pair, "0", {
              get() {
                reads.thirdProxy.push("pair-key-get");
                return "key";
              },
            });
            Object.defineProperty(pair, "1", {
              get() {
                reads.thirdProxy.push("pair-value-get");
                throw new Error("pair value getter failed");
              },
            });
            const result = {};
            Object.defineProperty(result, "done", {
              get() {
                reads.thirdProxy.push("done-get");
                return false;
              },
            });
            Object.defineProperty(result, "value", {
              get() {
                reads.thirdProxy.push("value-get");
                return pair;
              },
            });
            return result;
          }),
        ),
        ["length", "0", "1", "2"],
        "pair value getter failed",
        [
          "prototype",
          "prototype",
          "has:Symbol(Symbol.iterator)",
          "prototype",
          "get:Symbol(Symbol.iterator)",
          "iterator-call",
          "next-get:1",
          "next-get:2",
          "next-call",
          "done-get",
          "value-get",
          "pair-key-get",
          "pair-value-get",
        ],
        { rawError: true },
      ],
      [
        "extra tuple Proxy Map null IteratorResult keeps native TypeError",
        (accesses, reads) => makePointProxyWithMapIterator(
          accesses,
          reads,
          () => makeNextGetterIterator(reads, () => () => {
            reads.thirdProxy.push("next-call");
            return null;
          }),
        ),
        ["length", "0", "1", "2"],
        "Cannot read properties of null (reading 'done')",
        [
          "prototype",
          "prototype",
          "has:Symbol(Symbol.iterator)",
          "prototype",
          "get:Symbol(Symbol.iterator)",
          "iterator-call",
          "next-get:1",
          "next-get:2",
          "next-call",
        ],
        { rawError: true, rawErrorType: "TypeError" },
      ],
      [
        "extra tuple Proxy Map terminal done skips value getter",
        (accesses, reads) => makePointProxyWithMapIterator(
          accesses,
          reads,
          () => makeNextGetterIterator(reads, () => () => {
            reads.thirdProxy.push("next-call");
            const result = {};
            Object.defineProperty(result, "done", {
              get() {
                reads.thirdProxy.push("done-get");
                return true;
              },
            });
            Object.defineProperty(result, "value", {
              get() {
                reads.thirdProxy.push("value-get");
                throw new Error("terminal value must not be read");
              },
            });
            return result;
          }),
        ),
        ["length", "0", "1", "2"],
        "invalid length 3, expected 2 elements in sequence",
        [
          "prototype",
          "prototype",
          "has:Symbol(Symbol.iterator)",
          "prototype",
          "get:Symbol(Symbol.iterator)",
          "iterator-call",
          "next-get:1",
          "next-get:2",
          "next-call",
          "done-get",
        ],
      ],
      [
        "extra tuple Proxy Map Symbol entry key warns before requesting next pair",
        (accesses, reads) => makePointProxyWithMapIterator(
          accesses,
          reads,
          () => {
            let calls = 0;
            return makeNextGetterIterator(reads, () => () => {
            if (++calls > 1)
              return { done: true };
            reads.thirdProxy.push("next-call");
            const pair = {};
            Object.defineProperty(pair, "0", {
              get() {
                reads.thirdProxy.push("pair-key-get");
                return Symbol("key");
              },
            });
            Object.defineProperty(pair, "1", {
              get() {
                reads.thirdProxy.push("pair-value-get");
                return 1;
              },
            });
            const result = {};
            Object.defineProperty(result, "done", {
              get() {
                reads.thirdProxy.push("done-get");
                return false;
              },
            });
            Object.defineProperty(result, "value", {
              get() {
                reads.thirdProxy.push("value-get");
                return pair;
              },
            });
            return result;
            });
          },
        ),
        ["length", "0", "1", "2"],
        "invalid type: JsValue(Symbol(key)), expected any value",
        [
          "prototype",
          "prototype",
          "has:Symbol(Symbol.iterator)",
          "prototype",
          "get:Symbol(Symbol.iterator)",
          "iterator-call",
          "next-get:1",
          "next-get:2",
          "next-call",
          "done-get",
          "value-get",
          "pair-key-get",
          "pair-value-get",
        ],
      ],
      [
        "extra tuple Proxy Map Function entry key reports its debug name",
        (accesses, reads) => makePointProxyWithMapIterator(
          accesses,
          reads,
          () => {
            let calls = 0;
            return makeNextGetterIterator(reads, () => () => {
            if (++calls > 1)
              return { done: true };
            reads.thirdProxy.push("next-call");
            const pair = {};
            Object.defineProperty(pair, "0", {
              get() {
                reads.thirdProxy.push("pair-key-get");
                return function namedKey() {};
              },
            });
            Object.defineProperty(pair, "1", {
              get() {
                reads.thirdProxy.push("pair-value-get");
                return 1;
              },
            });
            const result = {};
            Object.defineProperty(result, "done", {
              get() {
                reads.thirdProxy.push("done-get");
                return false;
              },
            });
            Object.defineProperty(result, "value", {
              get() {
                reads.thirdProxy.push("value-get");
                return pair;
              },
            });
            return result;
            });
          },
        ),
        ["length", "0", "1", "2"],
        "invalid type: JsValue(Function(namedKey)), expected any value",
        [
          "prototype",
          "prototype",
          "has:Symbol(Symbol.iterator)",
          "prototype",
          "get:Symbol(Symbol.iterator)",
          "iterator-call",
          "next-get:1",
          "next-get:2",
          "next-call",
          "done-get",
          "value-get",
          "pair-key-get",
          "pair-value-get",
        ],
      ],
      [
        "extra tuple Proxy Map Function key reads its name getter",
        (accesses, reads) => makePointProxyWithMapEntry(
          accesses,
          reads,
          () => {
            const key = function originalKey() {};
            Object.defineProperty(key, "name", {
              configurable: true,
              get() {
                reads.thirdProxy.push("function-name-get");
                return "renamedKey";
              },
            });
            return key;
          },
        ),
        ["length", "0", "1", "2"],
        "invalid type: JsValue(Function(renamedKey)), expected any value",
        [
          "prototype",
          "prototype",
          "has:Symbol(Symbol.iterator)",
          "prototype",
          "get:Symbol(Symbol.iterator)",
          "iterator-call",
          "next-get:1",
          "next-get:2",
          "next-call",
          "done-get",
          "value-get",
          "pair-key-get",
          "pair-value-get",
          "function-name-get",
        ],
      ],
      [
        "extra tuple Proxy Map Function name getter error stays raw",
        (accesses, reads) => makePointProxyWithMapEntry(
          accesses,
          reads,
          () => {
            const key = function originalKey() {};
            Object.defineProperty(key, "name", {
              configurable: true,
              get() {
                reads.thirdProxy.push("function-name-get");
                throw new Error("function name getter failed");
              },
            });
            return key;
          },
        ),
        ["length", "0", "1", "2"],
        "function name getter failed",
        [
          "prototype",
          "prototype",
          "has:Symbol(Symbol.iterator)",
          "prototype",
          "get:Symbol(Symbol.iterator)",
          "iterator-call",
          "next-get:1",
          "next-get:2",
          "next-call",
          "done-get",
          "value-get",
          "pair-key-get",
          "pair-value-get",
          "function-name-get",
        ],
        { rawError: true },
      ],
      [
        "extra tuple Proxy Map empty Function name reports generic debug type",
        (accesses, reads) => makePointProxyWithMapEntry(
          accesses,
          reads,
          () => makeFunctionKeyWithName(reads, () => ""),
        ),
        ["length", "0", "1", "2"],
        "invalid type: JsValue(Function), expected any value",
        [
          "prototype",
          "prototype",
          "has:Symbol(Symbol.iterator)",
          "prototype",
          "get:Symbol(Symbol.iterator)",
          "iterator-call",
          "next-get:1",
          "next-get:2",
          "next-call",
          "done-get",
          "value-get",
          "pair-key-get",
          "pair-value-get",
          "function-name-get",
        ],
      ],
      [
        "extra tuple Proxy Map nonstring Function name reports generic debug type",
        (accesses, reads) => makePointProxyWithMapEntry(
          accesses,
          reads,
          () => makeFunctionKeyWithName(reads, () => 42),
        ),
        ["length", "0", "1", "2"],
        "invalid type: JsValue(Function), expected any value",
        [
          "prototype",
          "prototype",
          "has:Symbol(Symbol.iterator)",
          "prototype",
          "get:Symbol(Symbol.iterator)",
          "iterator-call",
          "next-get:1",
          "next-get:2",
          "next-call",
          "done-get",
          "value-get",
          "pair-key-get",
          "pair-value-get",
          "function-name-get",
        ],
      ],
      ...[
        ["Symbol", () => Symbol("function-name")],
        ["BigInt", () => 7n],
        ["boxed String", () => new String("boxedName")],
        ["null", () => null],
        ["opaque object Proxy", (reads) => makeOpaqueFunctionNameProxy(reads)],
      ].map(([nameKind, getName]) => [
        `extra tuple Proxy Map Function name getter returning ${nameKind} reports generic debug type`,
        (accesses, reads) => makePointProxyWithMapEntry(
          accesses,
          reads,
          () => makeFunctionKeyWithName(reads, () => getName(reads)),
        ),
        ["length", "0", "1", "2"],
        "invalid type: JsValue(Function), expected any value",
        functionNameReadEvents("function-name-get"),
      ]),
      [
        "extra tuple Proxy Map Function Proxy name getter customizes debug type",
        (accesses, reads) => makePointProxyWithMapEntry(
          accesses,
          reads,
          () => makeFunctionKeyWithName(reads, () => "proxyKey", true),
        ),
        ["length", "0", "1", "2"],
        "invalid type: JsValue(Function(proxyKey)), expected any value",
        [
          "prototype",
          "prototype",
          "has:Symbol(Symbol.iterator)",
          "prototype",
          "get:Symbol(Symbol.iterator)",
          "iterator-call",
          "next-get:1",
          "next-get:2",
          "next-call",
          "done-get",
          "value-get",
          "pair-key-get",
          "pair-value-get",
          "function-proxy-name-get",
        ],
      ],
      ...[
        ["opaque object Proxy", (reads) => makeOpaqueFunctionNameProxy(reads), "invalid type: JsValue(Function), expected any value"],
        ["Unicode string", () => "名😊", "invalid type: JsValue(Function(名😊)), expected any value"],
      ].map(([nameKind, getName, expectedError]) => [
        `extra tuple Proxy Map Function Proxy name getter returning ${nameKind} is handled without coercion`,
        (accesses, reads) => makePointProxyWithMapEntry(
          accesses,
          reads,
          () => makeFunctionKeyWithName(reads, () => getName(reads), true),
        ),
        ["length", "0", "1", "2"],
        expectedError,
        functionNameReadEvents("function-proxy-name-get"),
      ]),
      ...[
        [
          "Function own name getter receives the Function",
          (reads) => {
            const key = function originalKey() {};
            Object.defineProperty(key, "name", {
              configurable: true,
              get() {
                reads.thirdProxy.push("function-name-get");
                return this === key ? "ownReceiver" : "wrongReceiver";
              },
            });
            return key;
          },
          "ownReceiver",
          ["function-name-get"],
        ],
        [
          "Function inherited name getter receives the Function",
          (reads) => {
            const key = function originalKey() {};
            delete key.name;
            const prototype = Object.create(Function.prototype);
            Object.defineProperty(prototype, "name", {
              configurable: true,
              get() {
                reads.thirdProxy.push("function-inherited-name-get");
                return this === key ? "inheritedReceiver" : "wrongReceiver";
              },
            });
            Object.setPrototypeOf(key, prototype);
            return key;
          },
          "inheritedReceiver",
          ["function-inherited-name-get"],
        ],
        [
          "Function Proxy name trap receives the Proxy",
          (reads) => {
            let key;
            key = new Proxy(function originalKey() {}, {
              get(target, property, receiver) {
                if (property === "name") {
                  reads.thirdProxy.push("function-proxy-name-get");
                  return receiver === key ? "proxyReceiver" : "wrongReceiver";
                }
                return Reflect.get(target, property, receiver);
              },
            });
            return key;
          },
          "proxyReceiver",
          ["function-proxy-name-get"],
        ],
        [
          "Function Proxy forwards its receiver to the target name getter",
          (reads) => {
            const target = function originalKey() {};
            let key;
            Object.defineProperty(target, "name", {
              configurable: true,
              get() {
                reads.thirdProxy.push("function-target-name-get");
                return this === key ? "forwardedProxyReceiver" : "wrongReceiver";
              },
            });
            key = new Proxy(target, {
              get(target, property, receiver) {
                if (property === "name")
                  reads.thirdProxy.push("function-proxy-name-get");
                return Reflect.get(target, property, receiver);
              },
            });
            return key;
          },
          "forwardedProxyReceiver",
          ["function-proxy-name-get", "function-target-name-get"],
        ],
      ].map(([nameKind, makeKey, expectedName, nameEvents]) => [
        `extra tuple Proxy Map ${nameKind}`,
        (accesses, reads) => makePointProxyWithMapEntry(
          accesses,
          reads,
          () => makeKey(reads),
        ),
        ["length", "0", "1", "2"],
        `invalid type: JsValue(Function(${expectedName})), expected any value`,
        functionNameReadEvents(...nameEvents),
      ]),
      [
        "extra tuple Proxy Map Function Proxy name getter error stays raw",
        (accesses, reads) => makePointProxyWithMapEntry(
          accesses,
          reads,
          () => makeFunctionKeyWithName(
            reads,
            () => { throw new Error("function proxy name getter failed"); },
            true,
          ),
        ),
        ["length", "0", "1", "2"],
        "function proxy name getter failed",
        [
          "prototype",
          "prototype",
          "has:Symbol(Symbol.iterator)",
          "prototype",
          "get:Symbol(Symbol.iterator)",
          "iterator-call",
          "next-get:1",
          "next-get:2",
          "next-call",
          "done-get",
          "value-get",
          "pair-key-get",
          "pair-value-get",
          "function-proxy-name-get",
        ],
        { rawError: true },
      ],
      ...[
        ["null", null],
        ["undefined", undefined],
        ["string", "raw name getter value"],
        ["Symbol", Symbol("raw-name-getter")],
        ["BigInt", 9n],
        ["plain object", Object.freeze({ kind: "raw-name-getter" })],
        ["opaque Proxy", rawNameThrownProxy],
      ].map(([thrownKind, thrownValue]) => [
        `extra tuple Proxy Map Function Proxy name getter throws raw ${thrownKind}`,
        (accesses, reads) => makePointProxyWithMapEntry(
          accesses,
          reads,
          () => makeFunctionKeyWithName(
            reads,
            () => { throw thrownValue; },
            true,
          ),
        ),
        ["length", "0", "1", "2"],
        undefined,
        functionNameReadEvents("function-proxy-name-get"),
        { rawError: true, rawValue: thrownValue },
      ]),
      [
        "extra tuple Proxy Map object key leaves opaque key and value untouched",
        (accesses, reads) => makePointProxyWithMapEntry(
          accesses,
          reads,
          () => new Proxy({}, {
            getPrototypeOf() {
              reads.thirdProxy.push("opaque-key-prototype");
              throw new Error("opaque key must not be inspected");
            },
            get(target, key, receiver) {
              reads.thirdProxy.push(`opaque-key-get:${String(key)}`);
              throw new Error("opaque key must not be inspected");
            },
            ownKeys() {
              reads.thirdProxy.push("opaque-key-ownKeys");
              throw new Error("opaque key must not be inspected");
            },
          }),
          () => new Proxy({}, {
            get(target, key) {
              reads.thirdProxy.push(`opaque-value-get:${String(key)}`);
              throw new Error("opaque value must not be inspected");
            },
            ownKeys() {
              reads.thirdProxy.push("opaque-value-ownKeys");
              throw new Error("opaque value must not be inspected");
            },
          }),
        ),
        ["length", "0", "1", "2"],
        "invalid length 3, expected 2 elements in sequence",
        [
          "prototype",
          "prototype",
          "has:Symbol(Symbol.iterator)",
          "prototype",
          "get:Symbol(Symbol.iterator)",
          "iterator-call",
          "next-get:1",
          "next-get:2",
          "next-call",
          "done-get",
          "value-get",
          "pair-key-get",
          "pair-value-get",
          "next-get:3",
          "next-call",
          "done-get:end",
        ],
      ],
      [
        "extra tuple Symbol is rejected while buffering shape data",
        (accesses) => new Proxy([2, 0, Symbol("extra")], {
          get(target, key, receiver) {
            accesses.push(String(key));
            return Reflect.get(target, key, receiver);
          },
        }),
        ["length", "0", "1", "2"],
        "invalid type: JsValue(Symbol(extra)), expected any value",
      ],
      [
        "extra tuple Set is rejected while buffering shape data",
        (accesses) => new Proxy([2, 0, new Set([1])], {
          get(target, key, receiver) {
            accesses.push(String(key));
            return Reflect.get(target, key, receiver);
          },
        }),
        ["length", "0", "1", "2"],
        "invalid type: JsValue(Set), expected any value",
      ],
      [
        "extra tuple Float32Array is rejected while buffering shape data",
        (accesses) => new Proxy([2, 0, new Float32Array([1])], {
          get(target, key, receiver) {
            accesses.push(String(key));
            return Reflect.get(target, key, receiver);
          },
        }),
        ["length", "0", "1", "2"],
        "invalid type: JsValue(Float32Array), expected any value",
      ],
      [
        "extra tuple Uint8Array is buffered before tuple length rejection",
        (accesses) => new Proxy([2, 0, new Uint8Array([1])], {
          get(target, key, receiver) {
            accesses.push(String(key));
            return Reflect.get(target, key, receiver);
          },
        }),
        ["length", "0", "1", "2"],
        "invalid length 3, expected 2 elements in sequence",
      ],
      [
        "extra tuple custom iterable is rejected while buffering shape data",
        (accesses) => new Proxy([2, 0, { *[Symbol.iterator]() { yield 1; } }], {
          get(target, key, receiver) {
            accesses.push(String(key));
            return Reflect.get(target, key, receiver);
          },
        }),
        ["length", "0", "1", "2"],
        "invalid type: JsValue(Object({})), expected any value",
      ],
      [
        "toJSON and iterator getters are ignored",
        (accesses, reads) => {
          const target = [true, 0];
          Object.defineProperty(target, "toJSON", {
            get() {
              reads.toJSON += 1;
              throw new Error("tuple toJSON getter must be ignored");
            },
          });
          Object.defineProperty(target, Symbol.iterator, {
            get() {
              reads.iterator += 1;
              throw new Error("array iterator getter must be ignored");
            },
          });
          return new Proxy(target, {
            get(target, key, receiver) {
              accesses.push(typeof key === "symbol" ? `Symbol(${key.description})` : String(key));
              return Reflect.get(target, key, receiver);
            },
          });
        },
        ["length", "0"],
        "invalid type: boolean `true`, expected f32",
      ],
    ];
    for (const kind of ["SPP", "BPP"]) {
      for (const [name, makePoint, expectedAccesses, expectedError, expectedNestedEvents, expectations = {}] of pointTupleProxyCases) {
        const instance = structuredClone(kind === "SPP" ? fixture : bppFixture);
        const accesses = [];
        const reads = { toJSON: 0, iterator: 0, thirdValue: 0, thirdProxy: [] };
        const point = makePoint(accesses, reads);
        const shape = {
          type: "simple_polygon",
          data: [[0, 0], point, [2, 2], [0, 2]],
        };
        if (kind === "SPP")
          instance.items[0].shape = shape;
        else
          instance.bins[0].shape = shape;
        warnings.length = 0;
        let didThrow = false;
        const result = captureThrownValue(() => {
          try {
            return kind === "SPP"
              ? module.run_lbf_spp_wasm(instance, { n_samples: 1, ls_frac: 0 })
              : module.run_lbf_bpp_wasm(instance, { n_samples: 1, ls_frac: 0 });
          } catch (thrown) {
            didThrow = true;
            throw thrown;
          }
        });
        assert.deepEqual(accesses, expectedAccesses, `${kind} ${name} array access order`);
        if (name === "extra tuple evaluates nested third value before length rejection") {
          assert.equal(result.thrown?.message, expectedError, `${kind} ${name} raw getter error`);
          assert.deepEqual(warnings, [], `${kind} ${name} should fail before a Serde warning`);
          assert.equal(reads.thirdValue, 1, `${kind} ${name} nested getter count`);
        } else if (expectations.rawError) {
          assert.ok(didThrow, `${kind} ${name} should throw`);
          if (Object.hasOwn(expectations, "rawValue"))
            assert.strictEqual(result.thrown, expectations.rawValue, `${kind} ${name} raw thrown value`);
          else
            assert.equal(result.thrown?.message, expectedError, `${kind} ${name} raw error`);
          if (expectations.rawValue === rawNameThrownProxy)
            assert.deepEqual(rawNameThrownProxyEvents, [], `${kind} ${name} must not inspect the thrown Proxy`);
          if (expectations.rawErrorType !== undefined)
            assert.equal(result.thrown?.name, expectations.rawErrorType, `${kind} ${name} raw error type`);
          assert.deepEqual(warnings, [], `${kind} ${name} should fail before a Serde warning`);
        } else {
          assert.ok(result.thrown instanceof WebAssembly.RuntimeError, `${kind} ${name} should trap`);
          assert.equal(result.thrown.message, "unreachable", `${kind} ${name} trap text`);
          assert.deepEqual(warnings, [
            `Ext${kind === "SPP" ? "SP" : "BP"}Instance decode error: ${expectations.warningName ?? "Error"}: ${expectedError}, not using fallback!!`,
          ], `${kind} ${name} Serde warning`);
        }
        if (name === "extra tuple plain object Proxy follows Serde materialization order") {
          assert.deepEqual(reads.thirdProxy, [
            "prototype",
            "prototype",
            "has:Symbol(Symbol.iterator)",
            "get:Symbol(Symbol.iterator)",
            "ownKeys",
            "descriptor:value",
            "get:value",
          ], `${kind} ${name} nested Proxy order`);
        }
        if (name === "extra tuple custom iterable Proxy preserves JsValue debug order") {
          assert.deepEqual(reads.thirdProxy, [
            "prototype",
            "prototype",
            "has:Symbol(Symbol.iterator)",
            "prototype",
            "prototype",
            "prototype",
            "get:Symbol(Symbol.toStringTag)",
            "get:toJSON",
            "ownKeys",
            "descriptor:value",
            "get:value",
          ], `${kind} ${name} nested Proxy order`);
        }
        if (name === "extra tuple iterable Proxy catches prototype errors during classification") {
          assert.deepEqual(reads.thirdProxy, [
            "prototype",
            "prototype",
            "has:Symbol(Symbol.iterator)",
            "prototype",
            "prototype",
            "prototype",
            "get:Symbol(Symbol.toStringTag)",
            "get:toJSON",
            "ownKeys",
            "descriptor:value",
            "get:value",
          ], `${kind} ${name} nested Proxy order`);
        }
        if (expectedNestedEvents !== undefined) {
          assert.deepEqual(reads.thirdProxy, expectedNestedEvents, `${kind} ${name} nested Proxy order`);
        }
        if (name === "toJSON and iterator getters are ignored")
          assert.deepEqual(reads, {
            toJSON: 0,
            iterator: 0,
            thirdValue: 0,
            thirdProxy: [],
          }, `${kind} ignored tuple hooks`);
      }
    }
    warnings.length = 0;

    const bppContainerSimplePolygonErrors = [
      ["simple polygon outer", { type: "simple_polygon", data: [[0, 0], [1, 0]] }],
      [
        "polygon outer",
        { type: "polygon", data: { outer: [[0, 0], [1, 0]], inner: [] } },
      ],
      [
        "polygon hole",
        {
          type: "polygon",
          data: {
            outer: [[0, 0], [6, 0], [6, 6], [0, 6]],
            inner: [[[0, 0], [1, 0]]],
          },
        },
      ],
    ];
    for (const [name, shape] of bppContainerSimplePolygonErrors) {
      const invalidContainerPolygon = structuredClone(bppFixture);
      invalidContainerPolygon.bins[0].shape = shape;
      const result = captureThrownValue(() =>
        module.run_lbf_bpp_wasm(invalidContainerPolygon, {
          n_samples: 1,
          ls_frac: 0,
        }),
      );
      assert.equal(
        result.thrown,
        "Importer error: Simple polygon must have at least 3 points: [Point(0.0, 0.0), Point(1.0, 0.0)]",
        `container ${name} should preserve the pinned Rust point-list diagnostic`,
      );
    }

    const qualityZoneSimplePolygonErrors = [
      [
        "two points",
        [[0, 0], [1, 0]],
        "Importer error: Simple polygon must have at least 3 points: [Point(0.0, 0.0), Point(1.0, 0.0)]",
      ],
      [
        "intersecting edges",
        [[0, 0], [2, 2], [0, 2], [2, 0]],
        "Importer error: Simple polygon contains intersecting edges 0 and 2: [Point(0.0, 0.0), Point(2.0, 2.0), Point(0.0, 2.0), Point(2.0, 0.0)]",
      ],
    ];
    for (const [name, points, expectedError] of qualityZoneSimplePolygonErrors) {
      const invalidQualityZone = structuredClone(bppFixture);
      invalidQualityZone.bins[0].zones = [
        { quality: 0, shape: { type: "simple_polygon", data: points } },
      ];
      const result = captureThrownValue(() =>
        module.run_lbf_bpp_wasm(invalidQualityZone, {
          n_samples: 1,
          ls_frac: 0,
        }),
      );
      assert.equal(
        result.thrown,
        expectedError,
        `quality-zone ${name} should preserve the pinned Rust point-list diagnostic`,
      );
    }

    const bppItemWithHole = structuredClone(bppFixture);
    bppItemWithHole.items[0].shape = {
      type: "polygon",
      data: {
        outer: [[0, 0], [2, 0], [2, 2], [0, 2]],
        inner: [[[0.5, 0.5], [1.5, 0.5], [1.5, 1.5], [0.5, 1.5]]],
      },
    };
    const bppItemHoleError = captureThrownValue(() =>
      module.run_lbf_bpp_wasm(bppItemWithHole, { n_samples: 1, ls_frac: 0 }),
    );
    assert.equal(
      bppItemHoleError.thrown,
      "Importer error: item 301 has unsupported holes",
      "BPP item-hole Importer errors must preserve the pinned Rust text",
    );

    const bppMultiPolygonItem = structuredClone(bppFixture);
    bppMultiPolygonItem.items[0].shape = {
      type: "multi_polygon",
      data: [{ outer: [[0, 0], [2, 0], [2, 2], [0, 2]], inner: [] }],
    };
    const bppMultiPolygonError = captureThrownValue(() =>
      module.run_lbf_bpp_wasm(bppMultiPolygonItem, { n_samples: 1, ls_frac: 0 }),
    );
    assert.equal(
      bppMultiPolygonError.thrown,
      "Importer error: No support for multipolygons yet",
      "BPP multipolygon Importer errors must preserve the pinned Rust text",
    );

    const bppContainerMultiPolygon = structuredClone(bppFixture);
    bppContainerMultiPolygon.bins[0].shape = {
      type: "multi_polygon",
      data: [
        { outer: [[0, 0], [2, 0], [2, 2], [0, 2]], inner: [] },
        { outer: [[4, 4], [5, 4], [5, 5], [4, 5]], inner: [] },
      ],
    };
    const bppContainerMultiPolygonError = captureThrownValue(() =>
      module.run_lbf_bpp_wasm(bppContainerMultiPolygon, { n_samples: 1, ls_frac: 0 }),
    );
    assert.equal(
      bppContainerMultiPolygonError.thrown,
      "Importer error: No support for multipolygon shapes yet",
      "BPP container multipolygon errors must preserve the pinned Rust Importer text",
    );

    const duplicateBppBinId = structuredClone(bppFixture);
    duplicateBppBinId.bins.push(structuredClone(duplicateBppBinId.bins[0]));
    const duplicateBppBinIdError = captureThrownValue(() =>
      module.run_lbf_bpp_wasm(duplicateBppBinId, { n_samples: 1, ls_frac: 0 }),
    );
    assert.equal(
      duplicateBppBinIdError.thrown,
      "Importer error: bin IDs must be unique",
      "duplicate BPP bin IDs must preserve the pinned Rust Importer text",
    );

    const wasmUsizeOverflowStock = structuredClone(bppFixture);
    wasmUsizeOverflowStock.bins[0].stock = 4294967296;
    const wasmUsizeOverflowStockError = captureThrownValue(() =>
      module.run_lbf_bpp_wasm(wasmUsizeOverflowStock, {
        n_samples: 1,
        ls_frac: 0,
      }),
    );
    assert.ok(
      wasmUsizeOverflowStockError.thrown instanceof WebAssembly.RuntimeError,
      "bin stock outside wasm32 usize must preserve Rust's decode trap channel",
    );
    assert.equal(wasmUsizeOverflowStockError.thrown.message, "unreachable");

    const bigintNumericFields = structuredClone(bppFixture);
    bigintNumericFields.min_item_separation = -1;
    bigintNumericFields.items[0].id = 18446744073709551615n;
    bigintNumericFields.items[0].demand = 18446744073709551615n;
    bigintNumericFields.items[0].min_quality = 0n;
    bigintNumericFields.items[0].shape.data.width = 2n;
    bigintNumericFields.bins[0].id = 18446744073709551614n;
    bigintNumericFields.bins[0].stock = 1n;
    bigintNumericFields.bins[0].cost = 18446744073709551615n;
    bigintNumericFields.bins[0].zones = [
      {
        quality: 0n,
        shape: {
          type: "rectangle",
          data: { x_min: 3, y_min: 3, width: 1, height: 1 },
        },
      },
    ];
    const bigintNumericFieldError = captureThrownValue(() =>
      module.run_lbf_bpp_wasm(bigintNumericFields, {
        n_samples: 1,
        ls_frac: 0,
      }),
    );
    assert.equal(
      bigintNumericFieldError.thrown,
      "Importer error: min_item_separation must be finite and nonnegative",
      "serde-wasm-bindgen BigInt values in numeric instance fields must preserve exact C++ input serialization",
    );

    const bigintMinItemSeparation = structuredClone(bppFixture);
    bigintMinItemSeparation.min_item_separation = 0n;
    const bigintMinItemSeparationError = captureThrownValue(() =>
      module.run_lbf_bpp_wasm(bigintMinItemSeparation, {
        n_samples: 1,
        ls_frac: 0,
      }),
    );
    assert.ok(
      bigintMinItemSeparationError.thrown instanceof WebAssembly.RuntimeError,
      "BigInt in the pinned f32 min_item_separation field must keep Rust's decode trap channel",
    );
    assert.equal(bigintMinItemSeparationError.thrown.message, "unreachable");

    const bigintStringField = structuredClone(bppFixture);
    bigintStringField.name = 1n;
    const bigintStringFieldError = captureThrownValue(() =>
      module.run_lbf_bpp_wasm(bigintStringField, {
        n_samples: 1,
        ls_frac: 0,
      }),
    );
    assert.ok(
      bigintStringFieldError.thrown instanceof WebAssembly.RuntimeError,
      "BigInt in a Rust string field must preserve the decode trap channel",
    );
    assert.equal(bigintStringFieldError.thrown.message, "unreachable");

    const nestedMapCases = [
      ["item", (instance) => {
        instance.items[0] = new Map(Object.entries(instance.items[0]));
      }],
      ["bin", (instance) => {
        instance.bins[0] = new Map(Object.entries(instance.bins[0]));
      }],
      ["shape", (instance) => {
        instance.items[0].shape = new Map(Object.entries(instance.items[0].shape));
      }],
    ];
    for (const [name, convert] of nestedMapCases) {
      const nestedMapInstance = structuredClone(bppFixture);
      nestedMapInstance.min_item_separation = -1;
      convert(nestedMapInstance);
      const result = captureThrownValue(() =>
        module.run_lbf_bpp_wasm(nestedMapInstance, {
          n_samples: 1,
          ls_frac: 0,
        }),
      );
      assert.equal(
        result.thrown,
        "Importer error: min_item_separation must be finite and nonnegative",
        `nested ${name} Maps accepted by serde-wasm-bindgen must serialize as records`,
      );
    }

    const topLevelMap = new Map(Object.entries(bppFixture));
    const topLevelMapError = captureThrownValue(() =>
      module.run_lbf_bpp_wasm(topLevelMap, { n_samples: 1, ls_frac: 0 }),
    );
    assert.ok(
      topLevelMapError.thrown instanceof WebAssembly.RuntimeError,
      "a top-level Map must retain Rust's struct-deserialization trap",
    );
    assert.equal(topLevelMapError.thrown.message, "unreachable");

    const nestedInheritedToJsonCases = [
      ["item", (instance) => {
        Object.setPrototypeOf(instance.items[0], { toJSON: () => ({}) });
      }],
      ["shape", (instance) => {
        Object.setPrototypeOf(instance.items[0].shape, { toJSON: () => ({}) });
      }],
      ["shape data", (instance) => {
        Object.setPrototypeOf(instance.items[0].shape.data, {
          toJSON: () => ({}),
        });
      }],
      ["bin", (instance) => {
        Object.setPrototypeOf(instance.bins[0], { toJSON: () => ({}) });
      }],
    ];
    for (const [name, addToJson] of nestedInheritedToJsonCases) {
      const instance = structuredClone(bppFixture);
      instance.min_item_separation = -1;
      addToJson(instance);
      const result = captureThrownValue(() =>
        module.run_lbf_bpp_wasm(instance, { n_samples: 1, ls_frac: 0 }),
      );
      assert.equal(
        result.thrown,
        "Importer error: min_item_separation must be finite and nonnegative",
        `inherited ${name} toJSON must not replace Rust's nested struct input`,
      );
    }

    const ownItemToJson = structuredClone(bppFixture);
    ownItemToJson.min_item_separation = -1;
    const ownItem = ownItemToJson.items[0];
    Object.defineProperty(ownItem, "toJSON", {
      enumerable: true,
      value: () => {
        const result = { ...ownItem };
        delete result.toJSON;
        return result;
      },
    });
    const ownItemToJsonError = captureThrownValue(() =>
      module.run_lbf_bpp_wasm(ownItemToJson, { n_samples: 1, ls_frac: 0 }),
    );
    assert.ok(
      ownItemToJsonError.thrown instanceof WebAssembly.RuntimeError,
      "an own enumerable nested toJSON function must retain Rust's decode trap",
    );
    assert.equal(ownItemToJsonError.thrown.message, "unreachable");

    const ownOrientationToJson = structuredClone(bppFixture);
    ownOrientationToJson.min_item_separation = -1;
    const orientationWithOwnToJson = ownOrientationToJson.items[0].orientation;
    Object.defineProperty(orientationWithOwnToJson, "toJSON", {
      enumerable: true,
      value: () => ({ rotation: orientationWithOwnToJson.rotation }),
    });
    const ownOrientationToJsonError = captureThrownValue(() =>
      module.run_lbf_bpp_wasm(ownOrientationToJson, {
        n_samples: 1,
        ls_frac: 0,
      }),
    );
    assert.ok(
      ownOrientationToJsonError.thrown instanceof WebAssembly.RuntimeError,
      "an own enumerable toJSON function on a deny-unknown-fields struct must trap",
    );
    assert.equal(ownOrientationToJsonError.thrown.message, "unreachable");

    const rootToJson = structuredClone(bppFixture);
    rootToJson.min_item_separation = -1;
    Object.defineProperty(rootToJson, "toJSON", {
      enumerable: true,
      value: () => ({ ...rootToJson }),
    });
    const rootToJsonError = captureThrownValue(() =>
      module.run_lbf_bpp_wasm(rootToJson, { n_samples: 1, ls_frac: 0 }),
    );
    assert.equal(
      rootToJsonError.thrown,
      "Importer error: min_item_separation must be finite and nonnegative",
      "the root instance toJSON property remains ignored as in Rust",
    );

    let rootOwnKeysCalls = 0;
    const rootOwnKeysProxy = new Proxy(structuredClone(bppFixture), {
      ownKeys() {
        rootOwnKeysCalls += 1;
        throw new Error("Rust struct decoding must not enumerate root keys");
      },
    });
    const rootOwnKeysProxyResult = captureThrownValue(() =>
      module.run_lbf_bpp_wasm(rootOwnKeysProxy, {
        n_samples: 1,
        ls_frac: 0,
      }),
    );
    assert.equal(
      rootOwnKeysProxyResult.thrown,
      undefined,
      "root struct decoding must read known fields without enumerating proxy keys",
    );
    assert.ok(
      rootOwnKeysProxyResult.value &&
        typeof rootOwnKeysProxyResult.value === "object",
      "root ownKeys proxy should still produce a WASM result",
    );
    assert.equal(
      rootOwnKeysCalls,
      0,
      "known root fields must be read without invoking the proxy ownKeys trap",
    );

    let rootItemsHasCalls = 0;
    const rootHasProxy = new Proxy(structuredClone(bppFixture), {
      has(target, key) {
        if (key === "items") {
          rootItemsHasCalls += 1;
          return false;
        }
        return Reflect.has(target, key);
      },
    });
    const rootHasProxyResult = captureThrownValue(() =>
      module.run_lbf_bpp_wasm(rootHasProxy, {
        n_samples: 1,
        ls_frac: 0,
      }),
    );
    assert.equal(
      rootHasProxyResult.thrown,
      undefined,
      "the Rust struct reader must still read items when a proxy has trap hides it",
    );
    assert.equal(
      rootItemsHasCalls,
      0,
      "required root fields must be read without consulting proxy has traps",
    );

    let rootMinSeparationHasCalls = 0;
    const rootMinSeparationHasFalse = new Proxy(structuredClone(bppFixture), {
      get(target, key, receiver) {
        if (key === "min_item_separation") return -1;
        return Reflect.get(target, key, receiver);
      },
      has(target, key) {
        if (key === "min_item_separation") {
          rootMinSeparationHasCalls += 1;
          return false;
        }
        return Reflect.has(target, key);
      },
    });
    const rootMinSeparationHasFalseError = captureThrownValue(() =>
      module.run_lbf_bpp_wasm(rootMinSeparationHasFalse, {
        n_samples: 1,
        ls_frac: 0,
      }),
    );
    assert.equal(
      rootMinSeparationHasFalseError.thrown,
      "Importer error: min_item_separation must be finite and nonnegative",
      "a defaulted root field's concrete get value must remain effective when has returns false",
    );
    assert.equal(
      rootMinSeparationHasCalls,
      0,
      "Rust only checks has for the defaulted field when its get value is undefined",
    );

    const nestedToJSONDescriptorCases = [
      ["item", (instance) => [instance.items, 0]],
      ["orientation", (instance) => [instance.items[0], "orientation"]],
      ["shape", (instance) => [instance.items[0], "shape"]],
      ["shape data", (instance) => [instance.items[0].shape, "data"]],
      ["bin", (instance) => [instance.bins, 0]],
    ];
    for (const [name, select] of nestedToJSONDescriptorCases) {
      const instance = structuredClone(bppFixture);
      instance.min_item_separation = -1;
      const [parent, key] = select(instance);
      const target = parent[key];
      let descriptorReads = 0;
      parent[key] = new Proxy(target, {
        getOwnPropertyDescriptor(object, field) {
          if (field === "toJSON") {
            descriptorReads += 1;
            throw new Error("Rust must not probe absent nested toJSON fields");
          }
          return Reflect.getOwnPropertyDescriptor(object, field);
        },
      });
      const result = captureThrownValue(() =>
        module.run_lbf_bpp_wasm(instance, { n_samples: 1, ls_frac: 0 }),
      );
      assert.equal(
        result.thrown,
        importerError,
        `${name} decoding must not query an absent toJSON descriptor`,
      );
      assert.equal(
        descriptorReads,
        0,
        `${name} must not invoke getOwnPropertyDescriptor for absent toJSON`,
      );
    }

    const nestedGetPrototypeOfCases = [
      ["item", (instance) => [instance.items, 0]],
      ["orientation", (instance) => [instance.items[0], "orientation"]],
      ["shape", (instance) => [instance.items[0], "shape"]],
      ["shape data", (instance) => [instance.items[0].shape, "data"]],
      ["bin", (instance) => [instance.bins, 0]],
    ];
    const rootGetPrototypeOf = new Proxy(structuredClone(bppFixture), {
      getPrototypeOf() {
        throw new Error("Rust must not inspect the root prototype chain");
      },
    });
    const rootPrototypeResult = captureThrownValue(() =>
      module.run_lbf_bpp_wasm(rootGetPrototypeOf, {
        n_samples: 1,
        ls_frac: 0,
      }),
    );
    assert.equal(
      rootPrototypeResult.thrown,
      undefined,
      "root struct decoding must not inspect its prototype chain",
    );
    for (const [name, select] of nestedGetPrototypeOfCases) {
      const instance = structuredClone(bppFixture);
      const [parent, key] = select(instance);
      let prototypeReads = 0;
      parent[key] = new Proxy(parent[key], {
        getPrototypeOf() {
          prototypeReads += 1;
          throw new Error("Rust must not inspect nested prototype chains");
        },
      });
      const result = captureThrownValue(() =>
        module.run_lbf_bpp_wasm(instance, { n_samples: 1, ls_frac: 0 }),
      );
      assert.equal(
        result.thrown,
        undefined,
        `${name} decoding must not inspect its prototype chain`,
      );
      assert.equal(
        prototypeReads,
        0,
        `${name} must not invoke getPrototypeOf during Map detection`,
      );
    }

    const mapProxyCases = [
      ["item", (instance) => [instance.items, 0], "trap"],
      ["orientation", (instance) => [instance.items[0], "orientation"], "error"],
      ["shape", (instance) => [instance.items[0], "shape"], "error"],
      ["shape data", (instance) => [instance.items[0].shape, "data"], "error"],
      ["bin", (instance) => [instance.bins, 0], "trap"],
    ];
    for (const [name, select, expectedKind] of mapProxyCases) {
      const instance = structuredClone(bppFixture);
      const [parent, key] = select(instance);
      let prototypeReads = 0;
      parent[key] = new Proxy(new Map(Object.entries(parent[key])), {
        getPrototypeOf() {
          prototypeReads += 1;
          throw new Error(`Rust Map probe: ${name}`);
        },
      });
      const result = captureThrownValue(() =>
        module.run_lbf_bpp_wasm(instance, { n_samples: 1, ls_frac: 0 }),
      );
      if (expectedKind === "trap") {
        assert.ok(
          result.thrown instanceof WebAssembly.RuntimeError,
          `${name} Map proxy must retain the struct decode trap`,
        );
        assert.equal(result.thrown.message, "unreachable");
        assert.equal(
          prototypeReads,
          0,
          `${name} struct decoding must not inspect Map proxy prototypes`,
        );
      } else {
        assert.equal(
          result.thrown?.message,
          `Rust Map probe: ${name}`,
          `${name} dynamic Map decoding must retain Rust's Map type probe`,
        );
        assert.equal(prototypeReads, 1);
      }
    }

    const nonMapIterableData = structuredClone(bppFixture);
    let rejectedIterableCalls = 0;
    const nonMapIterableShapeData = nonMapIterableData.items[0].shape.data;
    Object.defineProperty(nonMapIterableShapeData, Symbol.iterator, {
      enumerable: false,
      value: function* () {
        rejectedIterableCalls += 1;
        yield* Object.entries(nonMapIterableShapeData);
      },
    });
    const nonMapIterableError = captureThrownValue(() =>
      module.run_lbf_bpp_wasm(nonMapIterableData, {
        n_samples: 1,
        ls_frac: 0,
      }),
    );
    assert.ok(
      nonMapIterableError.thrown instanceof WebAssembly.RuntimeError,
      "a present non-Map iterator on a dynamic serde value must retain the decode trap",
    );
    assert.equal(nonMapIterableError.thrown.message, "unreachable");
    assert.equal(
      rejectedIterableCalls,
      0,
      "Rust rejects a non-Map iterable after the presence check without calling it",
    );

    const iteratorGetterData = structuredClone(bppFixture);
    let iteratorGetterReads = 0;
    Object.defineProperty(
      iteratorGetterData.items[0].shape.data,
      Symbol.iterator,
      {
        configurable: true,
        enumerable: false,
        get() {
          iteratorGetterReads += 1;
          return function* () {
            yield* Object.entries(iteratorGetterData.items[0].shape.data);
          };
        },
      },
    );
    const iteratorGetterError = captureThrownValue(() =>
      module.run_lbf_bpp_wasm(iteratorGetterData, {
        n_samples: 1,
        ls_frac: 0,
      }),
    );
    assert.ok(
      iteratorGetterError.thrown instanceof WebAssembly.RuntimeError,
      "a getter-backed non-Map iterator must retain the serde decode trap",
    );
    assert.equal(iteratorGetterError.thrown.message, "unreachable");
    assert.equal(
      iteratorGetterReads,
      0,
      "Rust checks iterator presence without evaluating its getter before rejecting the value",
    );

    const proxyIteratorPresent = structuredClone(bppFixture);
    let iteratorPresenceChecks = 0;
    proxyIteratorPresent.items[0].shape.data = new Proxy(
      proxyIteratorPresent.items[0].shape.data,
      {
        has(target, key) {
          if (key === Symbol.iterator) {
            iteratorPresenceChecks += 1;
            return true;
          }
          return Reflect.has(target, key);
        },
      },
    );
    const proxyIteratorPresentError = captureThrownValue(() =>
      module.run_lbf_bpp_wasm(proxyIteratorPresent, {
        n_samples: 1,
        ls_frac: 0,
      }),
    );
    assert.ok(
      proxyIteratorPresentError.thrown instanceof WebAssembly.RuntimeError,
      "a proxy reporting a non-Map iterator must retain the serde decode trap",
    );
    assert.equal(proxyIteratorPresentError.thrown.message, "unreachable");
    assert.equal(iteratorPresenceChecks, 1);

    const proxyIteratorFallback = structuredClone(bppFixture);
    const fallbackCounters = { has: 0, get: 0, iterate: 0 };
    const fallbackData = proxyIteratorFallback.items[0].shape.data;
    proxyIteratorFallback.items[0].shape.data = new Proxy(fallbackData, {
      has(target, key) {
        if (key === Symbol.iterator) {
          fallbackCounters.has += 1;
          return false;
        }
        return Reflect.has(target, key);
      },
      get(target, key, receiver) {
        if (key === Symbol.iterator) {
          fallbackCounters.get += 1;
          return function* () {
            fallbackCounters.iterate += 1;
            yield* Object.entries(target);
          };
        }
        return Reflect.get(target, key, receiver);
      },
    });
    const proxyIteratorFallbackError = captureThrownValue(() =>
      module.run_lbf_bpp_wasm(proxyIteratorFallback, {
        n_samples: 1,
        ls_frac: 0,
      }),
    );
    assert.equal(
      proxyIteratorFallbackError.thrown,
      undefined,
      "a proxy-hidden but retrievable map iterator must be consumed like Rust try_iter",
    );
    assert.deepEqual(fallbackCounters, { has: 1, get: 1, iterate: 1 });

    const runIteratorFallbackCase = (name, iteratorMethod, expectedCounts) => {
      const instance = structuredClone(bppFixture);
      const counts = { has: 0, get: 0, methodCalls: 0, nextGets: 0, nextCalls: 0 };
      const target = instance.items[0].shape.data;
      instance.items[0].shape.data = new Proxy(target, {
        has(object, key) {
          if (key === Symbol.iterator) {
            counts.has += 1;
            return false;
          }
          return Reflect.has(object, key);
        },
        get(object, key, receiver) {
          if (key === Symbol.iterator) {
            counts.get += 1;
            return iteratorMethod(target, counts);
          }
          return Reflect.get(object, key, receiver);
        },
      });
      const result = captureThrownValue(() =>
        module.run_lbf_bpp_wasm(instance, { n_samples: 1, ls_frac: 0 }),
      );
      assert.equal(
        result.thrown,
        undefined,
        `${name} must fall back to Object.entries when try_iter returns None`,
      );
      assert.deepEqual(counts, expectedCounts, `${name} iterator access order`);
    };
    runIteratorFallbackCase(
      "non-callable iterator method",
      () => 7,
      { has: 1, get: 1, methodCalls: 0, nextGets: 0, nextCalls: 0 },
    );
    runIteratorFallbackCase(
      "null iterator result",
      (_target, counts) => () => {
        counts.methodCalls += 1;
        return null;
      },
      { has: 1, get: 1, methodCalls: 1, nextGets: 0, nextCalls: 0 },
    );
    runIteratorFallbackCase(
      "iterator result without next",
      (_target, counts) => () => {
        counts.methodCalls += 1;
        return {};
      },
      { has: 1, get: 1, methodCalls: 1, nextGets: 0, nextCalls: 0 },
    );
    runIteratorFallbackCase(
      "iterator result with non-callable next",
      (_target, counts) => () => {
        counts.methodCalls += 1;
        const iterator = {};
        Object.defineProperty(iterator, "next", {
          get() {
            counts.nextGets += 1;
            return 7;
          },
        });
        return iterator;
      },
      { has: 1, get: 1, methodCalls: 1, nextGets: 1, nextCalls: 0 },
    );
    runIteratorFallbackCase(
      "iterator next getter order",
      (target, counts) => () => {
        counts.methodCalls += 1;
        const pairs = Object.entries(target);
        let index = 0;
        const iterator = {};
        Object.defineProperty(iterator, "next", {
          get() {
            counts.nextGets += 1;
            return () => {
              counts.nextCalls += 1;
              return index < pairs.length
                ? { done: false, value: pairs[index++] }
                : { done: true };
            };
          },
        });
        return iterator;
      },
      { has: 1, get: 1, methodCalls: 1, nextGets: 6, nextCalls: 5 },
    );

    const iteratorNextThrows = structuredClone(bppFixture);
    const nextThrowCounts = { has: 0, get: 0, methodCalls: 0, nextGets: 0, nextCalls: 0 };
    const throwingIterator = {};
    Object.defineProperty(throwingIterator, "next", {
      get() {
        nextThrowCounts.nextGets += 1;
        return () => {
          nextThrowCounts.nextCalls += 1;
          throw new Error("iterator next must become a serde trap");
        };
      },
    });
    const throwingIteratorData = iteratorNextThrows.items[0].shape.data;
    iteratorNextThrows.items[0].shape.data = new Proxy(throwingIteratorData, {
      has(target, key) {
        if (key === Symbol.iterator) {
          nextThrowCounts.has += 1;
          return false;
        }
        return Reflect.has(target, key);
      },
      get(target, key, receiver) {
        if (key === Symbol.iterator) {
          nextThrowCounts.get += 1;
          return () => {
            nextThrowCounts.methodCalls += 1;
            return throwingIterator;
          };
        }
        return Reflect.get(target, key, receiver);
      },
    });
    const iteratorNextError = captureThrownValue(() =>
      module.run_lbf_bpp_wasm(iteratorNextThrows, {
        n_samples: 1,
        ls_frac: 0,
      }),
    );
    assert.ok(
      iteratorNextError.thrown instanceof WebAssembly.RuntimeError,
      "an exception from iterator.next must retain Rust's serde trap channel",
    );
    assert.equal(iteratorNextError.thrown.message, "unreachable");
    assert.deepEqual(nextThrowCounts, {
      has: 1,
      get: 1,
      methodCalls: 1,
      nextGets: 2,
      nextCalls: 1,
    });

    const runIteratorDoneCoercionCase = (
      name,
      firstDoneValue,
      expectedError,
      expectedCounts,
    ) => {
      const instance = structuredClone(bppFixture);
      const counts = {
        has: 0,
        get: 0,
        methodCalls: 0,
        nextCalls: 0,
        doneReads: 0,
        valueReads: 0,
        valueOfCalls: 0,
      };
      const target = instance.items[0].shape.data;
      const pairs = Object.entries(target);
      let index = 0;
      const iterator = {
        next() {
          counts.nextCalls += 1;
          if (index >= pairs.length)
            return { done: true };
          const pair = pairs[index++];
          return {
            get done() {
              counts.doneReads += 1;
              return counts.doneReads === 1 ? firstDoneValue(counts) : false;
            },
            get value() {
              counts.valueReads += 1;
              return pair;
            },
          };
        },
      };
      instance.items[0].shape.data = new Proxy(target, {
        has(object, key) {
          if (key === Symbol.iterator) {
            counts.has += 1;
            return false;
          }
          return Reflect.has(object, key);
        },
        get(object, key, receiver) {
          if (key === Symbol.iterator) {
            counts.get += 1;
            return () => {
              counts.methodCalls += 1;
              return iterator;
            };
          }
          return Reflect.get(object, key, receiver);
        },
      });
      const result = captureThrownValue(() =>
        module.run_lbf_bpp_wasm(instance, { n_samples: 1, ls_frac: 0 }),
      );
      if (expectedError === undefined) {
        assert.equal(result.thrown, undefined, `${name} done conversion`);
      } else {
        assert.equal(result.thrown?.constructor?.name, expectedError.name);
        assert.equal(result.thrown?.message, expectedError.message);
      }
      assert.deepEqual(counts, expectedCounts, `${name} done conversion order`);
    };
    runIteratorDoneCoercionCase(
      "string zero",
      () => "0",
      undefined,
      {
        has: 1, get: 1, methodCalls: 1, nextCalls: 5,
        doneReads: 4, valueReads: 4, valueOfCalls: 0,
      },
    );
    runIteratorDoneCoercionCase(
      "fractional zero",
      () => 0.5,
      undefined,
      {
        has: 1, get: 1, methodCalls: 1, nextCalls: 5,
        doneReads: 4, valueReads: 4, valueOfCalls: 0,
      },
    );
    runIteratorDoneCoercionCase(
      "object valueOf zero",
      (counts) => ({
        valueOf() {
          counts.valueOfCalls += 1;
          return 0;
        },
      }),
      undefined,
      {
        has: 1, get: 1, methodCalls: 1, nextCalls: 5,
        doneReads: 4, valueReads: 4, valueOfCalls: 1,
      },
    );
    runIteratorDoneCoercionCase(
      "BigInt zero",
      () => 0n,
      { name: "TypeError", message: "Cannot convert a BigInt value to a number" },
      {
        has: 1, get: 1, methodCalls: 1, nextCalls: 1,
        doneReads: 1, valueReads: 0, valueOfCalls: 0,
      },
    );
    runIteratorDoneCoercionCase(
      "Symbol done",
      () => Symbol("done"),
      { name: "TypeError", message: "Cannot convert a Symbol value to a number" },
      {
        has: 1, get: 1, methodCalls: 1, nextCalls: 1,
        doneReads: 1, valueReads: 0, valueOfCalls: 0,
      },
    );

    const runIteratorResultShapeCase = (
      name,
      firstResultFactory,
      expectedError,
      expectedNextCalls,
    ) => {
      const instance = structuredClone(bppFixture);
      const counts = { has: 0, get: 0, methodCalls: 0, nextCalls: 0 };
      const target = instance.items[0].shape.data;
      const pairs = Object.entries(target);
      let index = 0;
      const iterator = {
        next() {
          counts.nextCalls += 1;
          if (index === 0) {
            index += 1;
            return firstResultFactory();
          }
          const pairIndex = index - 1;
          if (pairIndex < pairs.length) {
            index += 1;
            return { done: false, value: pairs[pairIndex] };
          }
          return { done: true };
        },
      };
      instance.items[0].shape.data = new Proxy(target, {
        has(object, key) {
          if (key === Symbol.iterator) {
            counts.has += 1;
            return false;
          }
          return Reflect.has(object, key);
        },
        get(object, key, receiver) {
          if (key === Symbol.iterator) {
            counts.get += 1;
            return () => {
              counts.methodCalls += 1;
              return iterator;
            };
          }
          return Reflect.get(object, key, receiver);
        },
      });
      const result = captureThrownValue(() =>
        module.run_lbf_bpp_wasm(instance, { n_samples: 1, ls_frac: 0 }),
      );
      assert.equal(result.thrown?.constructor?.name, expectedError.name, name);
      assert.equal(result.thrown?.message, expectedError.message, name);
      assert.deepEqual(
        counts,
        { has: 1, get: 1, methodCalls: 1, nextCalls: expectedNextCalls },
        `${name} iterator result consumption`,
      );
    };
    runIteratorResultShapeCase(
      "null IteratorResult property access",
      () => null,
      { name: "TypeError", message: "Cannot read properties of null (reading 'done')" },
      1,
    );
    runIteratorResultShapeCase(
      "undefined IteratorResult property access",
      () => undefined,
      { name: "TypeError", message: "Cannot read properties of undefined (reading 'done')" },
      1,
    );
    runIteratorResultShapeCase(
      "primitive IteratorResult and missing pair property access",
      () => 1,
      { name: "TypeError", message: "Cannot read properties of undefined (reading '0')" },
      1,
    );
    runIteratorResultShapeCase(
      "null Map pair property access",
      () => ({ done: false, value: null }),
      { name: "TypeError", message: "Cannot read properties of null (reading '0')" },
      1,
    );
    runIteratorResultShapeCase(
      "primitive Map pair decode trap",
      () => ({ done: false, value: 7 }),
      { name: "RuntimeError", message: "unreachable" },
      6,
    );

    const runIteratorDuplicateKeyCase = (
      name,
      transformEntries,
      expectTrap,
      expectedEntryCount,
    ) => {
      const instance = structuredClone(bppFixture);
      const counts = {
        has: 0,
        get: 0,
        methodCalls: 0,
        nextCalls: 0,
        doneReads: 0,
        valueReads: 0,
      };
      const target = instance.items[0].shape.data;
      const entries = transformEntries(Object.entries(target));
      let index = 0;
      const iterator = {
        next() {
          counts.nextCalls += 1;
          if (index >= entries.length) {
            return {
              get done() {
                counts.doneReads += 1;
                return true;
              },
            };
          }
          const entry = entries[index++];
          return {
            get done() {
              counts.doneReads += 1;
              return false;
            },
            get value() {
              counts.valueReads += 1;
              return entry;
            },
          };
        },
      };
      instance.items[0].shape.data = new Proxy(target, {
        has(object, key) {
          if (key === Symbol.iterator) {
            counts.has += 1;
            return false;
          }
          return Reflect.has(object, key);
        },
        get(object, key, receiver) {
          if (key === Symbol.iterator) {
            counts.get += 1;
            return () => {
              counts.methodCalls += 1;
              return iterator;
            };
          }
          return Reflect.get(object, key, receiver);
        },
      });
      const result = captureThrownValue(() =>
        module.run_lbf_bpp_wasm(instance, { n_samples: 1, ls_frac: 0 }),
      );
      if (expectTrap) {
        assert.ok(result.thrown instanceof WebAssembly.RuntimeError, name);
        assert.equal(result.thrown.message, "unreachable", name);
      } else {
        assert.equal(result.thrown, undefined, name);
      }
      assert.deepEqual(
        counts,
        {
          has: 1,
          get: 1,
          methodCalls: 1,
          nextCalls: expectedEntryCount + 1,
          doneReads: expectedEntryCount + 1,
          valueReads: expectedEntryCount,
        },
        `${name} duplicate-key iterator consumption`,
      );
    };
    runIteratorDuplicateKeyCase(
      "duplicate unknown map keys are ignored by the rectangle decoder",
      (entries) => [["extra", 0], ["extra", 1], ...entries],
      false,
      6,
    );
    runIteratorDuplicateKeyCase(
      "duplicate known map key at the beginning traps after full consumption",
      (entries) => [["x_min", 10], ...entries],
      true,
      5,
    );
    runIteratorDuplicateKeyCase(
      "duplicate known map key at the end traps after full consumption",
      (entries) => [...entries, ["x_min", 10]],
      true,
      5,
    );

    const runIteratorSchemaDuplicateKeyCase = (
      name,
      selectTarget,
      replaceTarget,
      transformEntries,
      expectTrap,
      expectedEntryCount,
    ) => {
      const instance = structuredClone(bppFixture);
      const counts = {
        has: 0,
        get: 0,
        methodCalls: 0,
        nextCalls: 0,
        doneReads: 0,
        valueReads: 0,
      };
      const parent = instance.items[0];
      const target = selectTarget(parent);
      const entries = transformEntries(Object.entries(target));
      let index = 0;
      const iterator = {
        next() {
          counts.nextCalls += 1;
          if (index >= entries.length)
            return { done: true };
          const entry = entries[index++];
          return {
            get done() {
              counts.doneReads += 1;
              return false;
            },
            get value() {
              counts.valueReads += 1;
              return entry;
            },
          };
        },
      };
      replaceTarget(parent, new Proxy(target, {
        has(object, key) {
          if (key === Symbol.iterator) {
            counts.has += 1;
            return false;
          }
          return Reflect.has(object, key);
        },
        get(object, key, receiver) {
          if (key === Symbol.iterator) {
            counts.get += 1;
            return () => {
              counts.methodCalls += 1;
              return iterator;
            };
          }
          return Reflect.get(object, key, receiver);
        },
      }));
      const result = captureThrownValue(() =>
        module.run_lbf_bpp_wasm(instance, { n_samples: 1, ls_frac: 0 }),
      );
      if (expectTrap) {
        assert.ok(result.thrown instanceof WebAssembly.RuntimeError, name);
        assert.equal(result.thrown.message, "unreachable", name);
      } else {
        assert.equal(result.thrown, undefined, name);
      }
      assert.deepEqual(
        counts,
        {
          has: 1,
          get: 1,
          methodCalls: 1,
          nextCalls: expectedEntryCount + 1,
          doneReads: expectedEntryCount,
          valueReads: expectedEntryCount,
        },
        `${name} schema-specific iterator consumption`,
      );
    };
    const selectShape = (item) => item.shape;
    const replaceShape = (item, shape) => { item.shape = shape; };
    runIteratorSchemaDuplicateKeyCase(
      "duplicate unknown tagged-shape keys are ignored",
      selectShape,
      replaceShape,
      (entries) => [["extra", 0], ["extra", 1], ...entries],
      false,
      4,
    );
    runIteratorSchemaDuplicateKeyCase(
      "duplicate tagged-shape type traps after full consumption",
      selectShape,
      replaceShape,
      (entries) => [["type", "rectangle"], ...entries],
      true,
      3,
    );
    runIteratorSchemaDuplicateKeyCase(
      "duplicate tagged-shape data traps after full consumption",
      selectShape,
      replaceShape,
      (entries) => [...entries, ["data", entries.find(([key]) => key === "data")[1]]],
      true,
      3,
    );
    const selectOrientation = (item) => item.orientation;
    const replaceOrientation = (item, orientation) => {
      item.orientation = orientation;
    };
    runIteratorSchemaDuplicateKeyCase(
      "duplicate orientation rotation field traps after full consumption",
      selectOrientation,
      replaceOrientation,
      (entries) => [["rotation", entries[0][1]], ...entries],
      true,
      2,
    );
    runIteratorSchemaDuplicateKeyCase(
      "duplicate orientation unknown field remains rejected",
      selectOrientation,
      replaceOrientation,
      (entries) => [["extra", 1], ["extra", 2], ...entries],
      true,
      3,
    );
    const selectRotation = (item) => item.orientation.rotation;
    const replaceRotation = (item, rotation) => {
      item.orientation.rotation = rotation;
    };
    runIteratorSchemaDuplicateKeyCase(
      "duplicate rotation mode traps after full consumption",
      selectRotation,
      replaceRotation,
      (entries) => [["mode", "discrete"], ...entries],
      true,
      3,
    );
    runIteratorSchemaDuplicateKeyCase(
      "duplicate rotation angles traps after full consumption",
      selectRotation,
      replaceRotation,
      (entries) => [...entries, ["angles", [0]]],
      true,
      3,
    );
    runIteratorSchemaDuplicateKeyCase(
      "duplicate rotation unknown field remains rejected",
      selectRotation,
      replaceRotation,
      (entries) => [["extra", 1], ["extra", 2], ...entries],
      true,
      4,
    );
    const selectPolygonData = (item) => {
      item.shape = {
        type: "polygon",
        data: {
          outer: [[0, 0], [2, 0], [2, 2], [0, 2]],
          inner: [],
        },
      };
      return item.shape.data;
    };
    const replacePolygonData = (item, data) => {
      item.shape.data = data;
    };
    runIteratorSchemaDuplicateKeyCase(
      "duplicate unknown polygon-data keys are ignored",
      selectPolygonData,
      replacePolygonData,
      (entries) => [["extra", 1], ["extra", 2], ...entries],
      false,
      4,
    );
    runIteratorSchemaDuplicateKeyCase(
      "duplicate polygon outer field traps after full consumption",
      selectPolygonData,
      replacePolygonData,
      (entries) => [["outer", entries.find(([key]) => key === "outer")[1]], ...entries],
      true,
      3,
    );
    runIteratorSchemaDuplicateKeyCase(
      "duplicate polygon inner field traps after full consumption",
      selectPolygonData,
      replacePolygonData,
      (entries) => [...entries, ["inner", []]],
      true,
      3,
    );

    const qualityZonePropertyOrderCases = [
      ["shape property inserted first", ["shape", "quality"]],
      ["quality property inserted first", ["quality", "shape"]],
    ];
    for (const [name, propertyOrder] of qualityZonePropertyOrderCases) {
      const instance = structuredClone(bppFixture);
      const trace = [];
      const zoneDataTarget = { x_min: 0, y_min: 0, width: 1, height: 1 };
      const zoneData = new Proxy(zoneDataTarget, {
        get(target, key, receiver) {
          if (typeof key === "string")
            trace.push(`data.get ${key}`);
          return Reflect.get(target, key, receiver);
        },
      });
      const zoneShapeTarget = { type: "rectangle", data: zoneData };
      const zoneShape = new Proxy(zoneShapeTarget, {
        get(target, key, receiver) {
          if (key === "type" || key === "data")
            trace.push(`shape.get ${key}`);
          return Reflect.get(target, key, receiver);
        },
      });
      const zoneTarget = {};
      let qualityReads = 0;
      for (const key of propertyOrder) {
        if (key === "shape") {
          zoneTarget.shape = zoneShape;
        } else {
          Object.defineProperty(zoneTarget, "quality", {
            configurable: true,
            enumerable: true,
            get() {
              qualityReads += 1;
              trace.push("zone.get quality");
              zoneDataTarget.width = 0;
              return 0;
            },
          });
        }
      }
      const zone = new Proxy(zoneTarget, {
        ownKeys(target) {
          trace.push("zone.ownKeys");
          return Reflect.ownKeys(target);
        },
        getOwnPropertyDescriptor(target, key) {
          if (key === "shape" || key === "quality")
            trace.push(`zone.descriptor ${String(key)}`);
          return Reflect.getOwnPropertyDescriptor(target, key);
        },
        get(target, key, receiver) {
          if (key === Symbol.iterator)
            trace.push("zone.get Symbol.iterator");
          if (key === "shape")
            trace.push("zone.get shape");
          return Reflect.get(target, key, receiver);
        },
        has(target, key) {
          if (key === Symbol.iterator)
            trace.push("zone.has Symbol.iterator");
          return Reflect.has(target, key);
        },
      });
      instance.bins[0].zones = [zone];
      const zoneFieldOrderError = captureThrownValue(() =>
        module.run_lbf_bpp_wasm(instance, { n_samples: 1, ls_frac: 0 }),
      );
      assert.equal(
        zoneFieldOrderError.thrown,
        "Importer error: invalid rectangle, x_min: 0, x_max: 0, y_min: 0, y_max: 1",
        `${name}: Zone fields must be collected before nested shape decoding`,
      );
      assert.equal(qualityReads, 1, `${name}: quality getter read count`);
      const expectedTrace = [
        "zone.has Symbol.iterator",
        "zone.get Symbol.iterator",
        "zone.ownKeys",
      ];
      for (const key of propertyOrder) {
        expectedTrace.push(`zone.descriptor ${key}`, `zone.get ${key}`);
      }
      expectedTrace.push(
        "shape.get type",
        "shape.get data",
        "data.get x_min",
        "data.get y_min",
        "data.get width",
        "data.get height",
      );
      assert.deepEqual(trace, expectedTrace, `${name}: serde field collection order`);
    }

    const runQualityZoneIteratorCase = (
      name,
      entries,
      expectTrap,
    ) => {
      const instance = structuredClone(bppFixture);
      const counts = {
        has: 0,
        get: 0,
        methodCalls: 0,
        nextCalls: 0,
        doneReads: 0,
        valueReads: 0,
      };
      let index = 0;
      const iterator = {
        next() {
          counts.nextCalls += 1;
          if (index >= entries.length) {
            return {
              get done() {
                counts.doneReads += 1;
                return true;
              },
            };
          }
          const entry = entries[index++];
          return {
            get done() {
              counts.doneReads += 1;
              return false;
            },
            get value() {
              counts.valueReads += 1;
              return entry;
            },
          };
        },
      };
      const zone = new Proxy({
        quality: 0,
        shape: { type: "rectangle", data: {
          x_min: 0, y_min: 0, width: 1, height: 1,
        } },
      }, {
        has(target, key) {
          if (key === Symbol.iterator)
            counts.has += 1;
          return Reflect.has(target, key);
        },
        get(target, key, receiver) {
          if (key === Symbol.iterator) {
            counts.get += 1;
            return () => {
              counts.methodCalls += 1;
              return iterator;
            };
          }
          return Reflect.get(target, key, receiver);
        },
      });
      instance.bins[0].zones = [zone];
      const result = captureThrownValue(() =>
        module.run_lbf_bpp_wasm(instance, { n_samples: 1, ls_frac: 0 }),
      );
      if (expectTrap) {
        assert.ok(result.thrown instanceof WebAssembly.RuntimeError, name);
        assert.equal(result.thrown.message, "unreachable", name);
      } else {
        assert.equal(result.thrown, undefined, name);
      }
      assert.deepEqual(
        counts,
        {
          has: 1,
          get: 1,
          methodCalls: 1,
          nextCalls: entries.length + 1,
          doneReads: entries.length + 1,
          valueReads: entries.length,
        },
        `${name} quality-zone iterator consumption`,
      );
    };
    const qualityZoneRectangle = {
      type: "rectangle",
      data: { x_min: 0, y_min: 0, width: 1, height: 1 },
    };
    runQualityZoneIteratorCase(
      "duplicate quality-zone quality field traps after full consumption",
      [["quality", 0], ["quality", 0], ["shape", qualityZoneRectangle]],
      true,
    );
    runQualityZoneIteratorCase(
      "duplicate unknown quality-zone fields are ignored",
      [["extra", 0], ["extra", 1], ["quality", 0], ["shape", qualityZoneRectangle]],
      false,
    );
    const qualityZoneIteratorValueOrderInstance = structuredClone(bppFixture);
    const qualityZoneIteratorValueOrderTrace = [];
    const qualityZoneIteratorValueOrderDataTarget = {
      x_min: 0,
      y_min: 0,
      width: 1,
      height: 1,
    };
    const qualityZoneIteratorValueOrderData = new Proxy(
      qualityZoneIteratorValueOrderDataTarget,
      {
        get(target, key, receiver) {
          if (typeof key === "string")
            qualityZoneIteratorValueOrderTrace.push(`data.get ${key}`);
          return Reflect.get(target, key, receiver);
        },
      },
    );
    const qualityZoneIteratorValueOrderShape = new Proxy({
      type: "rectangle",
      data: qualityZoneIteratorValueOrderData,
    }, {
      get(target, key, receiver) {
        if (key === "type" || key === "data")
          qualityZoneIteratorValueOrderTrace.push(`shape.get ${key}`);
        return Reflect.get(target, key, receiver);
      },
    });
    let qualityZoneIteratorValueOrderIndex = 0;
    const qualityZoneIteratorValueOrderEntries = [
      ["shape", qualityZoneIteratorValueOrderShape],
      ["quality", 0],
    ];
    const qualityZoneIteratorValueOrderIterator = {
      next() {
        qualityZoneIteratorValueOrderTrace.push(
          `iterator.next ${qualityZoneIteratorValueOrderIndex}`,
        );
        if (qualityZoneIteratorValueOrderIndex === 0) {
          qualityZoneIteratorValueOrderIndex += 1;
          return {
            done: false,
            value: qualityZoneIteratorValueOrderEntries[0],
          };
        }
        if (qualityZoneIteratorValueOrderIndex === 1) {
          qualityZoneIteratorValueOrderIndex += 1;
          qualityZoneIteratorValueOrderTrace.push(
            "iterator mutates shape width to zero",
          );
          qualityZoneIteratorValueOrderDataTarget.width = 0;
          return {
            done: false,
            value: qualityZoneIteratorValueOrderEntries[1],
          };
        }
        return { done: true };
      },
    };
    qualityZoneIteratorValueOrderInstance.bins[0].zones = [new Proxy({}, {
      has(target, key) {
        if (key === Symbol.iterator)
          qualityZoneIteratorValueOrderTrace.push("zone.has iterator");
        return Reflect.has(target, key);
      },
      get(target, key, receiver) {
        if (key === Symbol.iterator) {
          qualityZoneIteratorValueOrderTrace.push("zone.get iterator");
          return () => {
            qualityZoneIteratorValueOrderTrace.push("iterator.call");
            return qualityZoneIteratorValueOrderIterator;
          };
        }
        return Reflect.get(target, key, receiver);
      },
    })];
    const qualityZoneIteratorValueOrderResult = captureThrownValue(() =>
      module.run_lbf_bpp_wasm(
        qualityZoneIteratorValueOrderInstance,
        { n_samples: 1, ls_frac: 0 },
      ),
    );
    assert.equal(
      qualityZoneIteratorValueOrderResult.thrown,
      undefined,
      "quality-zone shape must be decoded before advancing to the next pair",
    );
    assert.deepEqual(qualityZoneIteratorValueOrderTrace, [
      "zone.has iterator",
      "zone.get iterator",
      "iterator.call",
      "iterator.next 0",
      "shape.get type",
      "shape.get data",
      "data.get x_min",
      "data.get y_min",
      "data.get width",
      "data.get height",
      "iterator.next 1",
      "iterator mutates shape width to zero",
      "iterator.next 2",
    ]);
    const qualityZoneDuplicateShapeTrace = [];
    const makeQualityZoneDuplicateShape = (name) => new Proxy({
      type: "rectangle",
      data: { x_min: 0, y_min: 0, width: 1, height: 1 },
    }, {
      get(target, key, receiver) {
        if (key === "type" || key === "data")
          qualityZoneDuplicateShapeTrace.push(`${name}.get ${key}`);
        return Reflect.get(target, key, receiver);
      },
    });
    const qualityZoneDuplicateShapeEntries = [
      ["shape", makeQualityZoneDuplicateShape("shape1")],
      ["shape", makeQualityZoneDuplicateShape("shape2")],
      ["quality", 0],
    ];
    let qualityZoneDuplicateShapeIndex = 0;
    const qualityZoneDuplicateShapeIterator = {
      next() {
        qualityZoneDuplicateShapeTrace.push(
          `iterator.next ${qualityZoneDuplicateShapeIndex}`,
        );
        if (qualityZoneDuplicateShapeIndex < qualityZoneDuplicateShapeEntries.length) {
          return {
            done: false,
            value: qualityZoneDuplicateShapeEntries[
              qualityZoneDuplicateShapeIndex++
            ],
          };
        }
        return { done: true };
      },
    };
    const qualityZoneDuplicateShapeInstance = structuredClone(bppFixture);
    qualityZoneDuplicateShapeInstance.bins[0].zones = [new Proxy({}, {
      has(target, key) {
        if (key === Symbol.iterator)
          qualityZoneDuplicateShapeTrace.push("zone.has iterator");
        return Reflect.has(target, key);
      },
      get(target, key, receiver) {
        if (key === Symbol.iterator) {
          qualityZoneDuplicateShapeTrace.push("zone.get iterator");
          return () => {
            qualityZoneDuplicateShapeTrace.push("iterator.call");
            return qualityZoneDuplicateShapeIterator;
          };
        }
        return Reflect.get(target, key, receiver);
      },
    })];
    const qualityZoneDuplicateShapeResult = captureThrownValue(() =>
      module.run_lbf_bpp_wasm(
        qualityZoneDuplicateShapeInstance,
        { n_samples: 1, ls_frac: 0 },
      ),
    );
    assert.ok(
      qualityZoneDuplicateShapeResult.thrown instanceof WebAssembly.RuntimeError,
    );
    assert.equal(qualityZoneDuplicateShapeResult.thrown.message, "unreachable");
    assert.deepEqual(qualityZoneDuplicateShapeTrace, [
      "zone.has iterator",
      "zone.get iterator",
      "iterator.call",
      "iterator.next 0",
      "shape1.get type",
      "shape1.get data",
      "iterator.next 1",
      "shape2.get type",
      "shape2.get data",
      "iterator.next 2",
      "iterator.next 3",
    ]);
    const qualityZoneShapeNoIteratorTrace = [];
    const qualityZoneShapeNoIteratorDataTarget = {
      x_min: 0,
      y_min: 0,
      width: 1,
      height: 1,
    };
    const qualityZoneShapeNoIteratorData = new Proxy(
      qualityZoneShapeNoIteratorDataTarget,
      {
        ownKeys(target) {
          qualityZoneShapeNoIteratorTrace.push("data.ownKeys");
          return Reflect.ownKeys(target);
        },
        getOwnPropertyDescriptor(target, key) {
          if (typeof key === "string")
            qualityZoneShapeNoIteratorTrace.push(`data.descriptor ${key}`);
          return Reflect.getOwnPropertyDescriptor(target, key);
        },
        get(target, key, receiver) {
          if (typeof key === "string")
            qualityZoneShapeNoIteratorTrace.push(`data.get ${key}`);
          return Reflect.get(target, key, receiver);
        },
      },
    );
    const qualityZoneShapeNoIterator = new Proxy({
      type: "rectangle",
      data: qualityZoneShapeNoIteratorData,
    }, {
      has(target, key) {
        if (key === Symbol.iterator)
          qualityZoneShapeNoIteratorTrace.push("shape.has iterator");
        return Reflect.has(target, key);
      },
      ownKeys(target) {
        qualityZoneShapeNoIteratorTrace.push("shape.ownKeys");
        return Reflect.ownKeys(target);
      },
      getOwnPropertyDescriptor(target, key) {
        if (key === "type" || key === "data")
          qualityZoneShapeNoIteratorTrace.push(`shape.descriptor ${key}`);
        return Reflect.getOwnPropertyDescriptor(target, key);
      },
      get(target, key, receiver) {
        if (key === "type" || key === "data")
          qualityZoneShapeNoIteratorTrace.push(`shape.get ${key}`);
        else if (key === Symbol.iterator)
          qualityZoneShapeNoIteratorTrace.push("shape.get iterator");
        return Reflect.get(target, key, receiver);
      },
    });
    const qualityZoneShapeNoIteratorInstance = structuredClone(bppFixture);
    qualityZoneShapeNoIteratorInstance.bins[0].zones = [{
      quality: 0,
      shape: qualityZoneShapeNoIterator,
    }];
    const qualityZoneShapeNoIteratorResult = captureThrownValue(() =>
      module.run_lbf_bpp_wasm(
        qualityZoneShapeNoIteratorInstance,
        { n_samples: 1, ls_frac: 0 },
      ),
    );
    assert.equal(qualityZoneShapeNoIteratorResult.thrown, undefined);
    assert.deepEqual(qualityZoneShapeNoIteratorTrace, [
      "shape.has iterator",
      "shape.get iterator",
      "shape.ownKeys",
      "shape.descriptor type",
      "shape.get type",
      "shape.descriptor data",
      "shape.get data",
      "data.ownKeys",
      "data.descriptor x_min",
      "data.get x_min",
      "data.descriptor y_min",
      "data.get y_min",
      "data.descriptor width",
      "data.get width",
      "data.descriptor height",
      "data.get height",
    ]);
    const bppPolygonNoIteratorTrace = [];
    const bppPolygonNoIteratorData = new Proxy({
      outer: [[0, 0], [2, 0], [2, 2], [0, 2]],
      inner: [],
    }, {
      ownKeys(target) {
        bppPolygonNoIteratorTrace.push("data.ownKeys");
        return Reflect.ownKeys(target);
      },
      getOwnPropertyDescriptor(target, key) {
        if (typeof key === "string")
          bppPolygonNoIteratorTrace.push(`data.descriptor ${key}`);
        return Reflect.getOwnPropertyDescriptor(target, key);
      },
      get(target, key, receiver) {
        if (typeof key === "string")
          bppPolygonNoIteratorTrace.push(`data.get ${key}`);
        return Reflect.get(target, key, receiver);
      },
    });
    const bppPolygonNoIterator = new Proxy({
      type: "polygon",
      data: bppPolygonNoIteratorData,
    }, {
      has(target, key) {
        if (key === Symbol.iterator)
          bppPolygonNoIteratorTrace.push("shape.has iterator");
        return Reflect.has(target, key);
      },
      ownKeys(target) {
        bppPolygonNoIteratorTrace.push("shape.ownKeys");
        return Reflect.ownKeys(target);
      },
      getOwnPropertyDescriptor(target, key) {
        if (key === "type" || key === "data")
          bppPolygonNoIteratorTrace.push(`shape.descriptor ${key}`);
        return Reflect.getOwnPropertyDescriptor(target, key);
      },
      get(target, key, receiver) {
        if (key === "type" || key === "data")
          bppPolygonNoIteratorTrace.push(`shape.get ${key}`);
        else if (key === Symbol.iterator)
          bppPolygonNoIteratorTrace.push("shape.get iterator");
        return Reflect.get(target, key, receiver);
      },
    });
    const bppPolygonNoIteratorInstance = structuredClone(bppFixture);
    bppPolygonNoIteratorInstance.items[0].shape = bppPolygonNoIterator;
    const bppPolygonNoIteratorResult = captureThrownValue(() =>
      module.run_lbf_bpp_wasm(
        bppPolygonNoIteratorInstance,
        { n_samples: 1, ls_frac: 0 },
      ),
    );
    assert.equal(bppPolygonNoIteratorResult.thrown, undefined);
    assert.deepEqual(bppPolygonNoIteratorTrace, [
      "shape.has iterator",
      "shape.get iterator",
      "shape.ownKeys",
      "shape.descriptor type",
      "shape.get type",
      "shape.descriptor data",
      "shape.get data",
      "data.ownKeys",
      "data.descriptor outer",
      "data.get outer",
      "data.descriptor inner",
      "data.get inner",
    ]);
    const qualityZoneTaggedShapeIteratorTrace = [];
    const qualityZoneTaggedShapeIteratorDataTarget = {
      x_min: 0,
      y_min: 0,
      width: 1,
      height: 1,
    };
    const qualityZoneTaggedShapeIteratorData = new Proxy(
      qualityZoneTaggedShapeIteratorDataTarget,
      {
        ownKeys(target) {
          qualityZoneTaggedShapeIteratorTrace.push("data.ownKeys");
          return Reflect.ownKeys(target);
        },
        getOwnPropertyDescriptor(target, key) {
          if (typeof key === "string")
            qualityZoneTaggedShapeIteratorTrace.push(`data.descriptor ${key}`);
          return Reflect.getOwnPropertyDescriptor(target, key);
        },
        get(target, key, receiver) {
          if (typeof key === "string")
            qualityZoneTaggedShapeIteratorTrace.push(`data.get ${key}`);
          return Reflect.get(target, key, receiver);
        },
      },
    );
    const qualityZoneTaggedShapeIteratorTarget = {
      type: "rectangle",
      data: qualityZoneTaggedShapeIteratorData,
    };
    Object.defineProperty(qualityZoneTaggedShapeIteratorTarget, Symbol.iterator, {
      configurable: true,
      get() {
        qualityZoneTaggedShapeIteratorTrace.push("shape.iterator getter");
        return () => {
          qualityZoneTaggedShapeIteratorTrace.push("shape.iterator called");
          return [][Symbol.iterator]();
        };
      },
    });
    const qualityZoneTaggedShapeIterator = new Proxy(
      qualityZoneTaggedShapeIteratorTarget,
      {
        has(target, key) {
          if (key === Symbol.iterator)
            qualityZoneTaggedShapeIteratorTrace.push("shape.has iterator");
          return Reflect.has(target, key);
        },
        ownKeys(target) {
          qualityZoneTaggedShapeIteratorTrace.push("shape.ownKeys");
          return Reflect.ownKeys(target);
        },
        getOwnPropertyDescriptor(target, key) {
          if (key === "type" || key === "data")
            qualityZoneTaggedShapeIteratorTrace.push(`shape.descriptor ${key}`);
          return Reflect.getOwnPropertyDescriptor(target, key);
        },
        get(target, key, receiver) {
          if (key === "type" || key === "data")
            qualityZoneTaggedShapeIteratorTrace.push(`shape.get ${key}`);
          else if (key === Symbol.iterator)
            qualityZoneTaggedShapeIteratorTrace.push("shape.get iterator");
          return Reflect.get(target, key, receiver);
        },
      },
    );
    const qualityZoneTaggedShapeIteratorInstance = structuredClone(bppFixture);
    qualityZoneTaggedShapeIteratorInstance.bins[0].zones = [{
      quality: 0,
      shape: qualityZoneTaggedShapeIterator,
    }];
    warnings.length = 0;
    const qualityZoneTaggedShapeIteratorResult = captureThrownValue(() =>
      module.run_lbf_bpp_wasm(
        qualityZoneTaggedShapeIteratorInstance,
        { n_samples: 1, ls_frac: 0 },
      ),
    );
    assert.ok(
      qualityZoneTaggedShapeIteratorResult.thrown instanceof WebAssembly.RuntimeError,
    );
    assert.equal(
      qualityZoneTaggedShapeIteratorResult.thrown.message,
      "unreachable",
    );
    assert.deepEqual(qualityZoneTaggedShapeIteratorTrace, [
      "shape.has iterator",
      "shape.ownKeys",
      "shape.descriptor type",
      "shape.descriptor data",
      "shape.get type",
      "shape.get data",
      "data.get toJSON",
      "data.ownKeys",
      "data.descriptor x_min",
      "data.descriptor y_min",
      "data.descriptor width",
      "data.descriptor height",
      "data.get x_min",
      "data.get y_min",
      "data.get width",
      "data.get height",
    ]);
    assert.deepEqual(warnings, [
      'ExtBPInstance decode error: Error: invalid type: JsValue(Object({"type":"rectangle","data":{"x_min":0,"y_min":0,"width":1,"height":1}})), expected any value, not using fallback!!',
    ]);
    const sppTaggedShapeIteratorTrace = [];
    const sppTaggedShapeIteratorInstance = structuredClone(fixture);
    const sppTaggedShapeIteratorTarget = {
      type: "rectangle",
      data: { x_min: 0, y_min: 0, width: 1, height: 1 },
    };
    Object.defineProperty(sppTaggedShapeIteratorTarget, Symbol.iterator, {
      configurable: true,
      get() {
        sppTaggedShapeIteratorTrace.push("shape.iterator getter");
        return () => {
          sppTaggedShapeIteratorTrace.push("shape.iterator called");
          return [][Symbol.iterator]();
        };
      },
    });
    const sppTaggedShapeIterator = new Proxy(
      sppTaggedShapeIteratorTarget,
      {
        has(target, key) {
          if (key === Symbol.iterator)
            sppTaggedShapeIteratorTrace.push("shape.has iterator");
          return Reflect.has(target, key);
        },
        ownKeys(target) {
          sppTaggedShapeIteratorTrace.push("shape.ownKeys");
          return Reflect.ownKeys(target);
        },
        getOwnPropertyDescriptor(target, key) {
          if (key === "type" || key === "data")
            sppTaggedShapeIteratorTrace.push(`shape.descriptor ${key}`);
          return Reflect.getOwnPropertyDescriptor(target, key);
        },
        get(target, key, receiver) {
          if (key === "type" || key === "data")
            sppTaggedShapeIteratorTrace.push(`shape.get ${key}`);
          else if (key === Symbol.iterator)
            sppTaggedShapeIteratorTrace.push("shape.get iterator");
          return Reflect.get(target, key, receiver);
        },
      },
    );
    sppTaggedShapeIteratorInstance.items[0].shape = sppTaggedShapeIterator;
    warnings.length = 0;
    const sppTaggedShapeIteratorResult = captureThrownValue(() =>
      module.run_lbf_spp_wasm(
        sppTaggedShapeIteratorInstance,
        { n_samples: 1, ls_frac: 0 },
      ),
    );
    assert.ok(
      sppTaggedShapeIteratorResult.thrown instanceof WebAssembly.RuntimeError,
    );
    assert.equal(sppTaggedShapeIteratorResult.thrown.message, "unreachable");
    assert.deepEqual(sppTaggedShapeIteratorTrace, [
      "shape.has iterator",
      "shape.ownKeys",
      "shape.descriptor type",
      "shape.descriptor data",
      "shape.get type",
      "shape.get data",
    ]);
    assert.deepEqual(warnings, [
      'ExtSPInstance decode error: Error: invalid type: JsValue(Object({"type":"rectangle","data":{"x_min":0,"y_min":0,"width":1,"height":1}})), expected any value, not using fallback!!',
    ]);
    const runShapeIteratorFailure = (mode) => {
      const trace = [];
      const instance = structuredClone(fixture);
      const iteratorMethod = () => {
        trace.push("shape.iterator called");
        if (mode === "iterator-method")
          throw new Error("iterator method failed");
        if (mode === "next-getter-second") {
          let reads = 0;
          return Object.defineProperty({}, "next", {
            get() {
              reads += 1;
              trace.push(`shape.next getter ${reads}`);
              if (reads === 1)
                return () => ({ done: false, value: ["type", "rectangle"] });
              throw new Error("second next getter failed");
            },
          });
        }
        if (mode === "next-getter")
          return Object.defineProperty({}, "next", {
            get() {
              trace.push("shape.next getter");
              throw new Error("next getter failed");
            },
          });
        if (mode === "next-call")
          return {
            next() {
              trace.push("shape.next call");
              throw new Error("next call failed");
            },
          };
        return [][Symbol.iterator]();
      };
      instance.items[0].shape = new Proxy({
        type: "rectangle",
        data: { x_min: 0, y_min: 0, width: 1, height: 1 },
      }, {
        has(target, key) {
          if (key === Symbol.iterator)
            trace.push("shape.has iterator");
          return Reflect.has(target, key);
        },
        get(target, key, receiver) {
          if (key === Symbol.iterator) {
            trace.push("shape.get iterator");
            if (mode === "iterator-get")
              throw new Error("iterator getter failed");
            return iteratorMethod;
          }
          return Reflect.get(target, key, receiver);
        },
      });
      warnings.length = 0;
      const result = captureThrownValue(() =>
        module.run_lbf_spp_wasm(instance, { n_samples: 1, ls_frac: 0 }),
      );
      return { result, trace, warnings: [...warnings] };
    };
    for (const testCase of [
      {
        mode: "iterator-get",
        message: "iterator getter failed",
        trace: ["shape.has iterator", "shape.get iterator"],
      },
      {
        mode: "iterator-method",
        message: "iterator method failed",
        trace: [
          "shape.has iterator",
          "shape.get iterator",
          "shape.iterator called",
        ],
      },
      {
        mode: "next-call",
        message: "next call failed",
        trace: [
          "shape.has iterator",
          "shape.get iterator",
          "shape.iterator called",
          "shape.next call",
        ],
      },
      {
        mode: "next-getter-second",
        message: "second next getter failed",
        trace: [
          "shape.has iterator",
          "shape.get iterator",
          "shape.iterator called",
          "shape.next getter 1",
          "shape.next getter 2",
        ],
      },
    ]) {
      const { result, trace, warnings: actualWarnings } =
        runShapeIteratorFailure(testCase.mode);
      assert.ok(result.thrown instanceof WebAssembly.RuntimeError);
      assert.equal(result.thrown.message, "unreachable");
      assert.deepEqual(actualWarnings, [
        `ExtSPInstance decode error: Error: ${testCase.message}, not using fallback!!`,
      ]);
      assert.deepEqual(trace, testCase.trace);
    }
    const shapeNextGetterFailure = runShapeIteratorFailure("next-getter");
    assert.ok(shapeNextGetterFailure.result.thrown instanceof Error);
    assert.equal(
      shapeNextGetterFailure.result.thrown.message,
      "next getter failed",
    );
    assert.deepEqual(shapeNextGetterFailure.warnings, []);
    assert.deepEqual(shapeNextGetterFailure.trace, [
      "shape.has iterator",
      "shape.get iterator",
      "shape.iterator called",
      "shape.next getter",
    ]);
    const runShapePairMapCase = (pairs, kind) => {
      const instance = structuredClone(kind === "SPP" ? fixture : bppFixture);
      const counts = {
        hasIterator: 0,
        getIterator: 0,
        iteratorMethodCalls: 0,
        nextGetterReads: 0,
        nextCalls: 0,
      };
      let pairIndex = 0;
      const iterator = {};
      Object.defineProperty(iterator, "next", {
        get() {
          counts.nextGetterReads += 1;
          return () => {
            counts.nextCalls += 1;
            if (pairIndex >= pairs.length)
              return { done: true };
            return { done: false, value: pairs[pairIndex++] };
          };
        },
      });
      const shape = new Proxy({
        type: "rectangle",
        data: { x_min: 0, y_min: 0, width: 1, height: 1 },
      }, {
        has(target, key) {
          if (key === Symbol.iterator) {
            counts.hasIterator += 1;
            return false;
          }
          return Reflect.has(target, key);
        },
        get(target, key, receiver) {
          if (key === Symbol.iterator) {
            counts.getIterator += 1;
            return () => {
              counts.iteratorMethodCalls += 1;
              return iterator;
            };
          }
          return Reflect.get(target, key, receiver);
        },
      });
      if (kind === "SPP")
        instance.items[0].shape = shape;
      else
        instance.bins[0].zones = [{ quality: 0, shape }];
      warnings.length = 0;
      const result = captureThrownValue(() =>
        kind === "SPP"
          ? module.run_lbf_spp_wasm(instance, { n_samples: 1, ls_frac: 0 })
          : module.run_lbf_bpp_wasm(instance, { n_samples: 1, ls_frac: 0 }),
      );
      return { result, warnings: [...warnings], counts };
    };
    const rectShapePairValue = {
      x_min: 0,
      y_min: 0,
      width: 1,
      height: 1,
    };
    let iterableTagReads = 0;
    const iterableTagGetterKey = {
      [Symbol.iterator]() {
        return [][Symbol.iterator]();
      },
    };
    Object.defineProperty(iterableTagGetterKey, Symbol.toStringTag, {
      get() {
        iterableTagReads += 1;
        return "Exotic";
      },
    });
    const iterableTagThrowingKey = {
      [Symbol.iterator]() {
        return [][Symbol.iterator]();
      },
    };
    Object.defineProperty(iterableTagThrowingKey, Symbol.toStringTag, {
      get() {
        throw new Error("custom tag failed");
      },
    });
    let iterableProxyPrototypeReads = 0;
    const iterableProxyPrototypeKey = new Proxy({
      [Symbol.iterator]() {
        return [][Symbol.iterator]();
      },
    }, {
      getPrototypeOf() {
        iterableProxyPrototypeReads += 1;
        throw new Error("custom key prototype failed");
      },
    });
    const shapePairMapCases = [
      {
        name: "duplicate type",
        pairs: [
          ["type", "rectangle"],
          ["type", "polygon"],
          ["data", rectShapePairValue],
        ],
        warning:
          "ExtSPInstance decode error: Error: duplicate field `type`, not using fallback!!",
        nextCalls: 4,
      },
      {
        name: "duplicate data",
        pairs: [
          ["type", "rectangle"],
          ["data", rectShapePairValue],
          ["data", rectShapePairValue],
        ],
        warning:
          "ExtSPInstance decode error: Error: duplicate field `data`, not using fallback!!",
        nextCalls: 4,
      },
      {
        name: "duplicate type before Symbol key",
        pairs: [
          ["type", "rectangle"],
          ["type", "polygon"],
          [Symbol("key"), "bad"],
          ["data", rectShapePairValue],
        ],
        warning:
          "ExtSPInstance decode error: Error: invalid type: JsValue(Symbol(key)), expected any value, not using fallback!!",
        nextCalls: 3,
      },
      {
        name: "Symbol key before duplicate type",
        pairs: [
          [Symbol("key"), "bad"],
          ["type", "rectangle"],
          ["type", "polygon"],
          ["data", rectShapePairValue],
        ],
        warning:
          "ExtSPInstance decode error: Error: invalid type: JsValue(Symbol(key)), expected any value, not using fallback!!",
        nextCalls: 1,
      },
      {
        name: "duplicate type before integer key",
        pairs: [
          ["type", "rectangle"],
          ["type", "polygon"],
          [1, "bad"],
          ["data", rectShapePairValue],
        ],
        warning:
          'ExtSPInstance decode error: Error: duplicate field `type`, not using fallback!!',
        nextCalls: 5,
      },
      {
        name: "integer key before duplicate type",
        pairs: [
          [1, "bad"],
          ["type", "rectangle"],
          ["type", "polygon"],
          ["data", rectShapePairValue],
        ],
        warning:
          'ExtSPInstance decode error: Error: invalid type: integer `1`, expected "type", "data", or other ignored fields, not using fallback!!',
        nextCalls: 5,
      },
      {
        name: "duplicate unknown key",
        pairs: [
          ["type", "rectangle"],
          ["extra", 1],
          ["extra", 2],
          ["data", rectShapePairValue],
        ],
        warning: null,
        nextCalls: 5,
      },
      {
        name: "integer key",
        pairs: [[1, "bad"], ["type", "rectangle"], ["data", rectShapePairValue]],
        warning:
          'ExtSPInstance decode error: Error: invalid type: integer `1`, expected "type", "data", or other ignored fields, not using fallback!!',
        nextCalls: 4,
      },
      {
        name: "fractional key",
        pairs: [[1.5, "bad"], ["type", "rectangle"], ["data", rectShapePairValue]],
        warning:
          'ExtSPInstance decode error: Error: invalid type: floating point `1.5`, expected "type", "data", or other ignored fields, not using fallback!!',
        nextCalls: 4,
      },
      {
        name: "NaN key",
        pairs: [[Number.NaN, "bad"], ["type", "rectangle"], ["data", rectShapePairValue]],
        warning:
          'ExtSPInstance decode error: Error: invalid type: floating point `NaN`, expected "type", "data", or other ignored fields, not using fallback!!',
        nextCalls: 4,
      },
      {
        name: "positive infinity key",
        pairs: [[Number.POSITIVE_INFINITY, "bad"], ["type", "rectangle"], ["data", rectShapePairValue]],
        warning:
          'ExtSPInstance decode error: Error: invalid type: floating point `inf`, expected "type", "data", or other ignored fields, not using fallback!!',
        nextCalls: 4,
      },
      {
        name: "negative infinity key",
        pairs: [[Number.NEGATIVE_INFINITY, "bad"], ["type", "rectangle"], ["data", rectShapePairValue]],
        warning:
          'ExtSPInstance decode error: Error: invalid type: floating point `-inf`, expected "type", "data", or other ignored fields, not using fallback!!',
        nextCalls: 4,
      },
      {
        name: "unsafe integer key",
        pairs: [[Number.MAX_SAFE_INTEGER + 1, "bad"], ["type", "rectangle"], ["data", rectShapePairValue]],
        warning:
          'ExtSPInstance decode error: Error: invalid type: floating point `9007199254740992.0`, expected "type", "data", or other ignored fields, not using fallback!!',
        nextCalls: 4,
      },
      {
        name: "large integer-valued float key",
        pairs: [[1e21, "bad"], ["type", "rectangle"], ["data", rectShapePairValue]],
        warning:
          'ExtSPInstance decode error: Error: invalid type: floating point `1000000000000000000000.0`, expected "type", "data", or other ignored fields, not using fallback!!',
        nextCalls: 4,
      },
      {
        name: "large exponential integer-valued float key",
        pairs: [[1e30, "bad"], ["type", "rectangle"], ["data", rectShapePairValue]],
        warning:
          'ExtSPInstance decode error: Error: invalid type: floating point `1000000000000000000000000000000.0`, expected "type", "data", or other ignored fields, not using fallback!!',
        nextCalls: 4,
      },
      {
        name: "small exponent float key",
        pairs: [[1.25e-7, "bad"], ["type", "rectangle"], ["data", rectShapePairValue]],
        warning:
          'ExtSPInstance decode error: Error: invalid type: floating point `0.000000125`, expected "type", "data", or other ignored fields, not using fallback!!',
        nextCalls: 4,
      },
      {
        name: "boolean key",
        pairs: [[true, "bad"], ["type", "rectangle"], ["data", rectShapePairValue]],
        warning:
          'ExtSPInstance decode error: Error: invalid type: boolean `true`, expected "type", "data", or other ignored fields, not using fallback!!',
        nextCalls: 4,
      },
      {
        name: "null key",
        pairs: [[null, "bad"], ["type", "rectangle"], ["data", rectShapePairValue]],
        warning:
          'ExtSPInstance decode error: Error: invalid type: unit value, expected "type", "data", or other ignored fields, not using fallback!!',
        nextCalls: 4,
      },
      {
        name: "undefined key",
        pairs: [[undefined, "bad"], ["type", "rectangle"], ["data", rectShapePairValue]],
        warning:
          'ExtSPInstance decode error: Error: invalid type: unit value, expected "type", "data", or other ignored fields, not using fallback!!',
        nextCalls: 4,
      },
      {
        name: "BigInt key",
        pairs: [[1n, "bad"], ["type", "rectangle"], ["data", rectShapePairValue]],
        warning:
          'ExtSPInstance decode error: Error: invalid type: integer `1`, expected "type", "data", or other ignored fields, not using fallback!!',
        nextCalls: 4,
      },
      {
        name: "object key",
        pairs: [[{}, "bad"], ["type", "rectangle"], ["data", rectShapePairValue]],
        warning:
          'ExtSPInstance decode error: Error: invalid type: map, expected "type", "data", or other ignored fields, not using fallback!!',
        nextCalls: 4,
      },
      {
        name: "array key",
        pairs: [[[], "bad"], ["type", "rectangle"], ["data", rectShapePairValue]],
        warning:
          'ExtSPInstance decode error: Error: invalid type: sequence, expected "type", "data", or other ignored fields, not using fallback!!',
        nextCalls: 4,
      },
      {
        name: "Uint8Array unknown byte key",
        pairs: [[new Uint8Array([101, 120, 116, 114, 97]), "ignored"], ["type", "rectangle"], ["data", rectShapePairValue]],
        warning: null,
        nextCalls: 4,
      },
      {
        name: "Uint8Array type key",
        pairs: [[new Uint8Array([116, 121, 112, 101]), "rectangle"], ["data", rectShapePairValue]],
        warning: null,
        nextCalls: 3,
      },
      {
        name: "byte type unknown variant before duplicate",
        pairs: [[new Uint8Array([116, 121, 112, 101]), "bad"], ["type", "rectangle"], ["data", rectShapePairValue]],
        warning:
          "ExtSPInstance decode error: Error: unknown variant `bad`, expected one of `rectangle`, `simple_polygon`, `polygon`, `multi_polygon`, not using fallback!!",
        nextCalls: 4,
      },
      {
        name: "ArrayBuffer data key",
        pairs: [["type", "rectangle"], [new Uint8Array([100, 97, 116, 97]).buffer, rectShapePairValue]],
        warning: null,
        nextCalls: 3,
      },
      {
        name: "invalid UTF-8 byte key",
        pairs: [[new Uint8Array([255]), "ignored"], ["type", "rectangle"], ["data", rectShapePairValue]],
        warning: null,
        nextCalls: 4,
      },
      {
        name: "Uint8ClampedArray key",
        pairs: [[new Uint8ClampedArray([1]), "bad"], ["type", "rectangle"], ["data", rectShapePairValue]],
        warning:
          "ExtSPInstance decode error: Error: invalid type: JsValue(Uint8ClampedArray), expected any value, not using fallback!!",
        nextCalls: 1,
      },
      {
        name: "Int8Array key",
        pairs: [[new Int8Array([1]), "bad"], ["type", "rectangle"], ["data", rectShapePairValue]],
        warning:
          "ExtSPInstance decode error: Error: invalid type: JsValue(Int8Array), expected any value, not using fallback!!",
        nextCalls: 1,
      },
      {
        name: "Int8Array proxy key",
        pairs: [[new Proxy(new Int8Array([1]), {}), "bad"], ["type", "rectangle"], ["data", rectShapePairValue]],
        warning:
          'ExtSPInstance decode error: Error: invalid type: JsValue(Object({"0":1})), expected any value, not using fallback!!',
        nextCalls: 1,
      },
      {
        name: "Uint16Array key",
        pairs: [[new Uint16Array([1]), "bad"], ["type", "rectangle"], ["data", rectShapePairValue]],
        warning:
          "ExtSPInstance decode error: Error: invalid type: JsValue(Uint16Array), expected any value, not using fallback!!",
        nextCalls: 1,
      },
      {
        name: "Int16Array key",
        pairs: [[new Int16Array([1]), "bad"], ["type", "rectangle"], ["data", rectShapePairValue]],
        warning:
          "ExtSPInstance decode error: Error: invalid type: JsValue(Int16Array), expected any value, not using fallback!!",
        nextCalls: 1,
      },
      {
        name: "Uint32Array key",
        pairs: [[new Uint32Array([1]), "bad"], ["type", "rectangle"], ["data", rectShapePairValue]],
        warning:
          "ExtSPInstance decode error: Error: invalid type: JsValue(Uint32Array), expected any value, not using fallback!!",
        nextCalls: 1,
      },
      {
        name: "Int32Array key",
        pairs: [[new Int32Array([1]), "bad"], ["type", "rectangle"], ["data", rectShapePairValue]],
        warning:
          "ExtSPInstance decode error: Error: invalid type: JsValue(Int32Array), expected any value, not using fallback!!",
        nextCalls: 1,
      },
      {
        name: "BigInt64Array key",
        pairs: [[new BigInt64Array([1n]), "bad"], ["type", "rectangle"], ["data", rectShapePairValue]],
        warning:
          "ExtSPInstance decode error: Error: invalid type: JsValue(BigInt64Array), expected any value, not using fallback!!",
        nextCalls: 1,
      },
      {
        name: "BigUint64Array key",
        pairs: [[new BigUint64Array([1n]), "bad"], ["type", "rectangle"], ["data", rectShapePairValue]],
        warning:
          "ExtSPInstance decode error: Error: invalid type: JsValue(BigUint64Array), expected any value, not using fallback!!",
        nextCalls: 1,
      },
      {
        name: "Float32Array key",
        pairs: [[new Float32Array([1]), "bad"], ["type", "rectangle"], ["data", rectShapePairValue]],
        warning:
          "ExtSPInstance decode error: Error: invalid type: JsValue(Float32Array), expected any value, not using fallback!!",
        nextCalls: 1,
      },
      {
        name: "Float64Array key",
        pairs: [[new Float64Array([1]), "bad"], ["type", "rectangle"], ["data", rectShapePairValue]],
        warning:
          "ExtSPInstance decode error: Error: invalid type: JsValue(Float64Array), expected any value, not using fallback!!",
        nextCalls: 1,
      },
      {
        name: "DataView key",
        pairs: [[new DataView(new ArrayBuffer(4)), "bad"], ["type", "rectangle"], ["data", rectShapePairValue]],
        warning:
          'ExtSPInstance decode error: Error: invalid type: map, expected "type", "data", or other ignored fields, not using fallback!!',
        nextCalls: 4,
      },
      {
        name: "cross-realm Uint8Array key",
        pairs: [[foreignRealmValue("new Uint8Array([116, 121, 112, 101])"), "bad"], ["type", "rectangle"], ["data", rectShapePairValue]],
        warning:
          "ExtSPInstance decode error: Error: invalid type: JsValue(Uint8Array), expected any value, not using fallback!!",
        nextCalls: 1,
      },
      {
        name: "cross-realm ArrayBuffer key",
        pairs: [[foreignRealmValue("new ArrayBuffer(4)"), "bad"], ["type", "rectangle"], ["data", rectShapePairValue]],
        warning:
          'ExtSPInstance decode error: Error: invalid type: map, expected "type", "data", or other ignored fields, not using fallback!!',
        nextCalls: 4,
      },
      {
        name: "cross-realm Map key",
        pairs: [[foreignRealmValue("new Map([[1, 2]])"), "bad"], ["type", "rectangle"], ["data", rectShapePairValue]],
        warning:
          "ExtSPInstance decode error: Error: invalid type: JsValue(Map), expected any value, not using fallback!!",
        nextCalls: 1,
      },
      {
        name: "cross-realm array key",
        pairs: [[foreignRealmValue("[1, 2]"), "bad"], ["type", "rectangle"], ["data", rectShapePairValue]],
        warning:
          'ExtSPInstance decode error: Error: invalid type: sequence, expected "type", "data", or other ignored fields, not using fallback!!',
        nextCalls: 4,
      },
      {
        name: "plain custom toStringTag key",
        pairs: [[Object.defineProperty({ x: 1 }, Symbol.toStringTag, { value: "Int8Array" }), "bad"], ["type", "rectangle"], ["data", rectShapePairValue]],
        warning:
          'ExtSPInstance decode error: Error: invalid type: map, expected "type", "data", or other ignored fields, not using fallback!!',
        nextCalls: 4,
      },
      {
        name: "iterable custom toStringTag key",
        pairs: [[{ [Symbol.toStringTag]: "Exotic", [Symbol.iterator]() { return [][Symbol.iterator](); } }, "bad"], ["type", "rectangle"], ["data", rectShapePairValue]],
        warning:
          "ExtSPInstance decode error: Error: invalid type: JsValue(Exotic), expected any value, not using fallback!!",
        nextCalls: 1,
      },
      {
        name: "iterable toStringTag getter key",
        pairs: [[iterableTagGetterKey, "bad"], ["type", "rectangle"], ["data", rectShapePairValue]],
        warning:
          "ExtSPInstance decode error: Error: invalid type: JsValue(Exotic), expected any value, not using fallback!!",
        nextCalls: 1,
      },
      {
        name: "iterable throwing toStringTag getter key",
        pairs: [[iterableTagThrowingKey, "bad"], ["type", "rectangle"], ["data", rectShapePairValue]],
        warning: null,
        thrown: { name: "Error", message: "custom tag failed" },
        nextCalls: 1,
      },
      {
        name: "iterable proxy throwing prototype key",
        pairs: [[iterableProxyPrototypeKey, "bad"], ["type", "rectangle"], ["data", rectShapePairValue]],
        warning:
          "ExtSPInstance decode error: Error: invalid type: JsValue(Object({})), expected any value, not using fallback!!",
        nextCalls: 1,
      },
      {
        name: "proxy has iterator trap key",
        pairs: [[new Proxy({ [Symbol.iterator]() { return [][Symbol.iterator](); } }, {
          has(target, key) {
            if (key === Symbol.iterator)
              throw new Error("has iterator failed");
            return Reflect.has(target, key);
          },
        }), "bad"], ["type", "rectangle"], ["data", rectShapePairValue]],
        warning: null,
        thrown: { name: "Error", message: "has iterator failed" },
        nextCalls: 1,
      },
      {
        name: "ArrayBuffer proxy key",
        pairs: [[new Proxy(new ArrayBuffer(4), {}), "ignored"], ["type", "rectangle"], ["data", rectShapePairValue]],
        warning: null,
        nextCalls: 4,
      },
      {
        name: "Uint8Array proxy key",
        pairs: [[new Proxy(new Uint8Array([1]), {}), "bad"], ["type", "rectangle"], ["data", rectShapePairValue]],
        warning: null,
        thrown: {
          name: "TypeError",
          message:
            "Method get TypedArray.prototype.length called on incompatible receiver [object Uint8Array]",
        },
        nextCalls: 1,
      },
      {
        name: "throwing prototype proxy key",
        pairs: [[new Proxy({ x: 1 }, { getPrototypeOf() { throw new Error("key proto failed"); } }), "bad"], ["type", "rectangle"], ["data", rectShapePairValue]],
        warning:
          'ExtSPInstance decode error: Error: invalid type: map, expected "type", "data", or other ignored fields, not using fallback!!',
        nextCalls: 4,
      },
      {
        name: "Symbol key",
        pairs: [[Symbol("key"), "bad"], ["type", "rectangle"], ["data", rectShapePairValue]],
        warning:
          "ExtSPInstance decode error: Error: invalid type: JsValue(Symbol(key)), expected any value, not using fallback!!",
        nextCalls: 1,
      },
      {
        name: "function key",
        pairs: [[() => {}, "bad"], ["type", "rectangle"], ["data", rectShapePairValue]],
        warning:
          "ExtSPInstance decode error: Error: invalid type: JsValue(Function), expected any value, not using fallback!!",
        nextCalls: 1,
      },
    ];
    if (typeof Float16Array === "function") {
      shapePairMapCases.push(
        {
          name: "Float16Array key",
          pairs: [[new Float16Array([1]), "bad"], ["type", "rectangle"], ["data", rectShapePairValue]],
          warning:
            "ExtSPInstance decode error: Error: invalid type: JsValue(Float16Array), expected any value, not using fallback!!",
          nextCalls: 1,
        },
        {
          name: "cross-realm Float16Array key",
          pairs: [[foreignRealmValue("new Float16Array([1])"), "bad"], ["type", "rectangle"], ["data", rectShapePairValue]],
          warning:
            "ExtSPInstance decode error: Error: invalid type: JsValue(Float16Array), expected any value, not using fallback!!",
          nextCalls: 1,
        },
      );
    }
    for (const kind of ["SPP", "BPP"]) {
      for (const testCase of shapePairMapCases) {
        const { result, warnings: actualWarnings, counts } =
          runShapePairMapCase(testCase.pairs, kind);
        const expectedWarning = testCase.warning?.replace(
          "ExtSPInstance",
          kind === "SPP" ? "ExtSPInstance" : "ExtBPInstance",
        );
        assert.equal(counts.hasIterator, 1, `${kind} ${testCase.name}`);
        assert.equal(counts.getIterator, 1, `${kind} ${testCase.name}`);
        assert.equal(counts.iteratorMethodCalls, 1, `${kind} ${testCase.name}`);
        assert.equal(counts.nextCalls, testCase.nextCalls, `${kind} ${testCase.name}`);
        assert.equal(
          counts.nextGetterReads,
          testCase.nextCalls + 1,
          `${kind} ${testCase.name}`,
        );
        if (testCase.warning === null) {
          assert.deepEqual(actualWarnings, [], `${kind} ${testCase.name}`);
          if (testCase.thrown !== undefined) {
            assert.ok(result.thrown instanceof Error, `${kind} ${testCase.name}`);
            assert.equal(result.thrown.name, testCase.thrown.name, `${kind} ${testCase.name}`);
            assert.equal(
              result.thrown.message,
              testCase.thrown.message,
              `${kind} ${testCase.name}`,
            );
          } else {
            assert.equal(result.thrown, undefined, `${kind} ${testCase.name}`);
          }
        } else {
          assert.ok(
            result.thrown instanceof WebAssembly.RuntimeError,
            `${kind} ${testCase.name}`,
          );
          assert.equal(result.thrown.message, "unreachable", `${kind} ${testCase.name}`);
          assert.deepEqual(actualWarnings, [expectedWarning], `${kind} ${testCase.name}`);
        }
      }
    }
    assert.equal(iterableTagReads, 2);
    assert.equal(iterableProxyPrototypeReads, 10);
    const runShapeTypeValueCase = (kind, valueFactory) => {
      const instance = structuredClone(kind === "SPP" ? fixture : bppFixture);
      const shape = {
        type: valueFactory(),
        data: { x_min: 0, y_min: 0, width: 1, height: 1 },
      };
      if (kind === "SPP")
        instance.items[0].shape = shape;
      else
        instance.bins[0].zones = [{ quality: 0, shape }];
      warnings.length = 0;
      const result = captureThrownValue(() =>
        kind === "SPP"
          ? module.run_lbf_spp_wasm(instance, { n_samples: 1, ls_frac: 0 })
          : module.run_lbf_bpp_wasm(instance, { n_samples: 1, ls_frac: 0 }),
      );
      return { result, warnings: [...warnings] };
    };
    let shapeTypeIteratorHasReads = 0;
    let shapeTypeUint8ArrayPrototypeReads = 0;
    let shapeTypeArrayBufferPrototypeReads = 0;
    let shapeTypeEnumProxyEvents = [];
    let shapeTypeDebugEvents = [];
    let shapeTypeDebugPrototypeReads = 0;
    let shapeTypePayloadProxyEvents = [];
    const shapeTypeIterableProxy = new Proxy({
      [Symbol.iterator]() { return [][Symbol.iterator](); },
    }, {
      has(target, key) {
        if (key === Symbol.iterator)
          shapeTypeIteratorHasReads += 1;
        return Reflect.has(target, key);
      },
    });
    const shapeTypeUint8ArrayProxy = new Proxy(new Uint8Array([1]), {
      getPrototypeOf(target) {
        shapeTypeUint8ArrayPrototypeReads += 1;
        return Reflect.getPrototypeOf(target);
      },
    });
    const shapeTypeArrayBufferProxy = new Proxy(new ArrayBuffer(1), {
      getPrototypeOf(target) {
        shapeTypeArrayBufferPrototypeReads += 1;
        return Reflect.getPrototypeOf(target);
      },
    });
    const makeShapeTypeEnumProxy = behavior => {
      shapeTypeEnumProxyEvents = [];
      return new Proxy({ rectangle: null }, {
        has(target, key) {
          shapeTypeEnumProxyEvents.push(["has", key === Symbol.iterator ? "@@iterator" : String(key)]);
          if (key === Symbol.iterator && behavior.hasFalse)
            return false;
          return Reflect.has(target, key);
        },
        get(target, key, receiver) {
          shapeTypeEnumProxyEvents.push(["get", key === Symbol.iterator ? "@@iterator" : String(key)]);
          if (key === Symbol.iterator) {
            if (behavior.throwGetter)
              throw new Error("enum iterator getter failed");
            if (behavior.throwMethod)
              return () => {
                shapeTypeEnumProxyEvents.push(["iterator-call"]);
                throw new Error("enum iterator call failed");
              };
            if (behavior.iteratorFactory)
              return () => {
                shapeTypeEnumProxyEvents.push(["iterator-call"]);
                return behavior.iteratorFactory();
              };
            if (behavior.nonFunction)
              return 1;
            if (behavior.nullIterator)
              return () => {
                shapeTypeEnumProxyEvents.push(["iterator-call"]);
                return null;
              };
            if (behavior.emptyIterator)
              return () => {
                shapeTypeEnumProxyEvents.push(["iterator-call"]);
                return [][Symbol.iterator]();
              };
            if (behavior.multipleIterator)
              return () => {
                shapeTypeEnumProxyEvents.push(["iterator-call"]);
                return [["rectangle", null], ["rectangle", null]][Symbol.iterator]();
              };
            if (behavior.invalidIterator)
              return () => {
                shapeTypeEnumProxyEvents.push(["iterator-call"]);
                return {};
              };
            if (behavior.throwNext)
              return () => {
                shapeTypeEnumProxyEvents.push(["iterator-call"]);
                return {
                  next() {
                    shapeTypeEnumProxyEvents.push(["next-call"]);
                    throw new Error("enum next failed");
                  },
                };
              };
            if (behavior.throwFirstNextGetter)
              return () => {
                shapeTypeEnumProxyEvents.push(["iterator-call"]);
                return Object.defineProperty({}, "next", {
                  get() {
                    shapeTypeEnumProxyEvents.push(["next-get"]);
                    throw new Error("enum next getter failed");
                  },
                });
              };
            if (behavior.throwSecondNextGetter)
              return () => {
                shapeTypeEnumProxyEvents.push(["iterator-call"]);
                let reads = 0;
                return Object.defineProperty({}, "next", {
                  get() {
                    shapeTypeEnumProxyEvents.push(["next-get"]);
                    if (++reads === 2)
                      throw new Error("enum next getter failed");
                    return () => ({ done: true });
                  },
                });
              };
          }
          return Reflect.get(target, key, receiver);
        },
        ownKeys(target) {
          shapeTypeEnumProxyEvents.push(["ownKeys"]);
          return Reflect.ownKeys(target);
        },
        getOwnPropertyDescriptor(target, key) {
          shapeTypeEnumProxyEvents.push(["descriptor", String(key)]);
          return Reflect.getOwnPropertyDescriptor(target, key);
        },
      });
    };
    const makeShapeTypeDebugProxy = customTag => {
      shapeTypeDebugEvents = [];
      const target = {
        [Symbol.iterator]() { return [][Symbol.iterator](); },
      };
      if (customTag === undefined) {
        Object.defineProperty(target, "toJSON", {
          get() {
            return function() {
              shapeTypeDebugEvents.push(["call", "toJSON"]);
              return { visible: 1 };
            };
          },
        });
      }
      Object.defineProperty(target, Symbol.toStringTag, {
        get() { return customTag; },
      });
      return new Proxy(target, {
        has(target, key) {
          shapeTypeDebugEvents.push(["has", key === Symbol.iterator ? "@@iterator" : String(key)]);
          return Reflect.has(target, key);
        },
        get(target, key, receiver) {
          if (key === Symbol.toStringTag)
            shapeTypeDebugEvents.push(["get", "@@toStringTag"]);
          else if (key === "toJSON")
            shapeTypeDebugEvents.push(["get", "toJSON"]);
          return Reflect.get(target, key, receiver);
        },
        getPrototypeOf(target) {
          shapeTypeDebugEvents.push(["getPrototypeOf"]);
          shapeTypeDebugPrototypeReads += 1;
          return Reflect.getPrototypeOf(target);
        },
      });
    };
    const makeShapeTypeEmptyPayloadProxy = () => {
      shapeTypePayloadProxyEvents = [];
      return new Proxy({}, {
        has(target, key) {
          shapeTypePayloadProxyEvents.push(["has", String(key)]);
          return Reflect.has(target, key);
        },
        get(target, key, receiver) {
          shapeTypePayloadProxyEvents.push(["get", String(key)]);
          return Reflect.get(target, key, receiver);
        },
        getPrototypeOf(target) {
          shapeTypePayloadProxyEvents.push(["getPrototypeOf"]);
          return Reflect.getPrototypeOf(target);
        },
        ownKeys(target) {
          shapeTypePayloadProxyEvents.push(["ownKeys"]);
          return Reflect.ownKeys(target);
        },
        getOwnPropertyDescriptor(target, key) {
          shapeTypePayloadProxyEvents.push(["descriptor", String(key)]);
          return Reflect.getOwnPropertyDescriptor(target, key);
        },
      });
    };
    const makeShapeTypePayloadDebugProxy = () => {
      shapeTypePayloadProxyEvents = [];
      const target = {
        value: 1,
        [Symbol.iterator]() { return [][Symbol.iterator](); },
      };
      const keyName = key => key === Symbol.iterator ? "@@iterator"
        : key === Symbol.toStringTag ? "@@toStringTag" : String(key);
      return new Proxy(target, {
        has(target, key) {
          shapeTypePayloadProxyEvents.push(["has", keyName(key)]);
          return Reflect.has(target, key);
        },
        get(target, key, receiver) {
          shapeTypePayloadProxyEvents.push(["get", keyName(key)]);
          return Reflect.get(target, key, receiver);
        },
        getPrototypeOf(target) {
          shapeTypePayloadProxyEvents.push(["getPrototypeOf"]);
          return Reflect.getPrototypeOf(target);
        },
        ownKeys(target) {
          shapeTypePayloadProxyEvents.push(["ownKeys"]);
          return Reflect.ownKeys(target);
        },
        getOwnPropertyDescriptor(target, key) {
          shapeTypePayloadProxyEvents.push(["descriptor", String(key)]);
          return Reflect.getOwnPropertyDescriptor(target, key);
        },
      });
    };
    const makeShapeTypeHiddenPayloadIteratorProxy = behavior => {
      shapeTypePayloadProxyEvents = [];
      return new Proxy({}, {
        has(target, key) {
          shapeTypePayloadProxyEvents.push(["has", key === Symbol.iterator ? "@@iterator" : String(key)]);
          if (key === Symbol.iterator && behavior.hasFalse)
            return false;
          return Reflect.has(target, key);
        },
        get(target, key, receiver) {
          shapeTypePayloadProxyEvents.push(["get", key === Symbol.iterator ? "@@iterator" : String(key)]);
          if (key !== Symbol.iterator)
            return Reflect.get(target, key, receiver);
          if (behavior.throwGetter)
            throw new Error("payload iterator getter failed");
          if (behavior.throwMethod)
            return () => {
              shapeTypePayloadProxyEvents.push(["iterator-call"]);
              throw new Error("payload iterator call failed");
            };
          if (behavior.nonFunction)
            return 1;
          if (behavior.iteratorFactory)
            return () => {
              shapeTypePayloadProxyEvents.push(["iterator-call"]);
              return behavior.iteratorFactory();
            };
          if (behavior.throwNext)
            return () => {
              shapeTypePayloadProxyEvents.push(["iterator-call"]);
              return {
                next() {
                  shapeTypePayloadProxyEvents.push(["next-call"]);
                  throw new Error("payload iterator next failed");
                },
              };
            };
          if (behavior.throwFirstNextGetter)
            return () => {
              shapeTypePayloadProxyEvents.push(["iterator-call"]);
              return Object.defineProperty({}, "next", {
                get() {
                  shapeTypePayloadProxyEvents.push(["next-get"]);
                  throw new Error("payload next getter failed");
                },
              });
            };
          if (behavior.throwSecondNextGetter)
            return () => {
              shapeTypePayloadProxyEvents.push(["iterator-call"]);
              let reads = 0;
              return Object.defineProperty({}, "next", {
                get() {
                  shapeTypePayloadProxyEvents.push(["next-get"]);
                  if (++reads === 2)
                    throw new Error("payload next getter failed");
                  return () => ({ done: true });
                },
              });
            };
          if (behavior.nonFunctionSecondNextGetter)
            return () => {
              shapeTypePayloadProxyEvents.push(["iterator-call"]);
              let reads = 0;
              return Object.defineProperty({}, "next", {
                get() {
                  shapeTypePayloadProxyEvents.push(["next-get"]);
                  return ++reads === 2 ? 1 : () => ({ done: true });
                },
              });
            };
        },
        ownKeys(target) {
          shapeTypePayloadProxyEvents.push(["ownKeys"]);
          return Reflect.ownKeys(target);
        },
        getOwnPropertyDescriptor(target, key) {
          shapeTypePayloadProxyEvents.push(["descriptor", String(key)]);
          return Reflect.getOwnPropertyDescriptor(target, key);
        },
      });
    };
    const shapeTypePlainEnumProxyEvents = [
      ["has", "@@iterator"], ["get", "@@iterator"], ["ownKeys"],
      ["descriptor", "rectangle"], ["get", "rectangle"],
    ];
    const shapeTypeValueCases = [
      { name: "undefined type value", value: () => undefined, error: "invalid type: unit value, expected string or map" },
      { name: "null type value", value: () => null, error: "invalid type: unit value, expected string or map" },
      { name: "boolean type value", value: () => false, error: "invalid type: boolean `false`, expected string or map" },
      { name: "integer type value", value: () => 1, error: "invalid type: integer `1`, expected string or map" },
      { name: "fractional type value", value: () => 1.5, error: "invalid type: floating point `1.5`, expected string or map" },
      { name: "NaN type value", value: () => Number.NaN, error: "invalid type: floating point `NaN`, expected string or map" },
      { name: "infinite type value", value: () => Number.POSITIVE_INFINITY, error: "invalid type: floating point `inf`, expected string or map" },
      { name: "BigInt type value", value: () => 1n, error: "invalid type: integer `1`, expected string or map" },
      { name: "Symbol type value", value: () => Symbol("shape"), error: "invalid type: JsValue(Symbol(shape)), expected any value" },
      { name: "function type value", value: () => () => {}, error: "invalid type: JsValue(Function), expected any value" },
      { name: "array type value", value: () => [], error: "invalid type: sequence, expected string or map" },
      { name: "Uint8Array type value", value: () => new Uint8Array([1]), error: "invalid type: byte array, expected string or map" },
      { name: "ArrayBuffer type value", value: () => new ArrayBuffer(1), error: "invalid type: byte array, expected string or map" },
      { name: "Uint8Array Proxy type value", value: () => shapeTypeUint8ArrayProxy, jsError: { name: "TypeError", message: "Method get TypedArray.prototype.length called on incompatible receiver [object Uint8Array]" } },
      { name: "ArrayBuffer Proxy type value", value: () => shapeTypeArrayBufferProxy, error: "invalid type: byte array, expected string or map" },
      { name: "Float32Array type value", value: () => new Float32Array([1]), error: "invalid type: JsValue(Float32Array), expected any value" },
      { name: "DataView type value", value: () => new DataView(new ArrayBuffer(1)), error: "invalid value: map, expected map with a single key" },
      { name: "Set type value", value: () => new Set(), error: "invalid type: JsValue(Set), expected any value" },
      { name: "boxed string type value", value: () => new String("rectangle"), error: "invalid type: JsValue(String), expected any value" },
      { name: "empty object type value", value: () => ({}), error: "invalid value: map, expected map with a single key" },
      { name: "unknown string type value", value: () => "bad", error: "unknown variant `bad`, expected one of `rectangle`, `simple_polygon`, `polygon`, `multi_polygon`" },
      { name: "known single-key object type value", value: () => ({ rectangle: null }), succeeds: true },
      { name: "known single-key Map type value", value: () => new Map([["rectangle", null]]), succeeds: true },
      { name: "iterable Object JsValue debug proxy", value: () => makeShapeTypeDebugProxy(undefined), error: 'invalid type: JsValue(Object({"visible":1})), expected any value', debugEvents: [["getPrototypeOf"], ["getPrototypeOf"], ["has", "@@iterator"], ["getPrototypeOf"], ["getPrototypeOf"], ["getPrototypeOf"], ["get", "@@toStringTag"], ["get", "toJSON"], ["call", "toJSON"]] },
      { name: "iterable custom-tag JsValue debug proxy", value: () => makeShapeTypeDebugProxy("CustomShape"), error: "invalid type: JsValue(CustomShape), expected any value", debugEvents: [["getPrototypeOf"], ["getPrototypeOf"], ["has", "@@iterator"], ["getPrototypeOf"], ["getPrototypeOf"], ["getPrototypeOf"], ["get", "@@toStringTag"], ["getPrototypeOf"]] },
      { name: "plain object enum Proxy access order", value: () => makeShapeTypeEnumProxy({}), succeeds: true, events: shapeTypePlainEnumProxyEvents },
      { name: "has-false hidden enum iterator", value: () => makeShapeTypeEnumProxy({ hasFalse: true, iteratorFactory: () => [["rectangle", null]][Symbol.iterator]() }), succeeds: true, events: [["has", "@@iterator"], ["get", "@@iterator"], ["iterator-call"]] },
      { name: "has-false enum iterator getter error", value: () => makeShapeTypeEnumProxy({ hasFalse: true, throwGetter: true }), error: "enum iterator getter failed", events: [["has", "@@iterator"], ["get", "@@iterator"]] },
      { name: "has-false enum iterator method error", value: () => makeShapeTypeEnumProxy({ hasFalse: true, throwMethod: true }), error: "enum iterator call failed", events: [["has", "@@iterator"], ["get", "@@iterator"], ["iterator-call"]] },
      { name: "has-false nonfunction enum iterator", value: () => makeShapeTypeEnumProxy({ hasFalse: true, nonFunction: true }), succeeds: true, events: shapeTypePlainEnumProxyEvents },
      { name: "has-false null enum iterator result", value: () => makeShapeTypeEnumProxy({ hasFalse: true, nullIterator: true }), succeeds: true, events: [["has", "@@iterator"], ["get", "@@iterator"], ["iterator-call"], ["ownKeys"], ["descriptor", "rectangle"], ["get", "rectangle"]] },
      { name: "has-false empty enum iterator", value: () => makeShapeTypeEnumProxy({ hasFalse: true, emptyIterator: true }), error: "invalid value: map, expected map with a single key", events: [["has", "@@iterator"], ["get", "@@iterator"], ["iterator-call"]] },
      { name: "has-false multi-pair enum iterator", value: () => makeShapeTypeEnumProxy({ hasFalse: true, multipleIterator: true }), error: "invalid value: map, expected map with a single key", events: [["has", "@@iterator"], ["get", "@@iterator"], ["iterator-call"]] },
      { name: "has-false invalid enum iterator result", value: () => makeShapeTypeEnumProxy({ hasFalse: true, invalidIterator: true }), succeeds: true, events: [["has", "@@iterator"], ["get", "@@iterator"], ["iterator-call"], ["ownKeys"], ["descriptor", "rectangle"], ["get", "rectangle"]] },
      { name: "has-false enum next call error", value: () => makeShapeTypeEnumProxy({ hasFalse: true, throwNext: true }), error: "enum next failed", events: [["has", "@@iterator"], ["get", "@@iterator"], ["iterator-call"], ["next-call"]] },
      { name: "has-false first enum next getter error", value: () => makeShapeTypeEnumProxy({ hasFalse: true, throwFirstNextGetter: true }), jsError: { name: "Error", message: "enum next getter failed" }, events: [["has", "@@iterator"], ["get", "@@iterator"], ["iterator-call"], ["next-get"]] },
      { name: "has-false second enum next getter error", value: () => makeShapeTypeEnumProxy({ hasFalse: true, throwSecondNextGetter: true }), error: "enum next getter failed", events: [["has", "@@iterator"], ["get", "@@iterator"], ["iterator-call"], ["next-get"], ["next-get"]] },
      { name: "known variant with non-unit payload", value: () => ({ rectangle: 1 }), error: "invalid type: integer `1`, expected unit" },
      { name: "known variant with empty object payload", value: () => ({ rectangle: {} }), succeeds: true },
      { name: "known variant with empty Map payload", value: () => ({ rectangle: new Map() }), succeeds: true },
      { name: "known variant with empty DataView payload", value: () => ({ rectangle: new DataView(new ArrayBuffer(1)) }), succeeds: true },
      { name: "known variant with DataView property payload", value: () => ({ rectangle: Object.assign(new DataView(new ArrayBuffer(1)), { value: 1 }) }), error: "invalid type: map, expected unit" },
      { name: "known variant with empty payload Proxy access order", value: () => ({ rectangle: makeShapeTypeEmptyPayloadProxy() }), succeeds: true, payloadEvents: [["getPrototypeOf"], ["getPrototypeOf"], ["has", "Symbol(Symbol.iterator)"], ["get", "Symbol(Symbol.iterator)"], ["ownKeys"]] },
      { name: "known variant iterable payload JsValue debug access order", value: () => ({ rectangle: makeShapeTypePayloadDebugProxy() }), error: 'invalid type: JsValue(Object({"value":1})), expected any value', payloadEvents: [["getPrototypeOf"], ["getPrototypeOf"], ["has", "@@iterator"], ["getPrototypeOf"], ["getPrototypeOf"], ["getPrototypeOf"], ["get", "@@toStringTag"], ["get", "toJSON"], ["ownKeys"], ["descriptor", "value"], ["get", "value"]] },
      { name: "known variant with hidden empty payload iterator", value: () => ({ rectangle: makeShapeTypeHiddenPayloadIteratorProxy({ hasFalse: true, iteratorFactory: () => [][Symbol.iterator]() }) }), succeeds: true, payloadEvents: [["has", "@@iterator"], ["get", "@@iterator"], ["iterator-call"]] },
      { name: "known variant with hidden nonfunction payload iterator", value: () => ({ rectangle: makeShapeTypeHiddenPayloadIteratorProxy({ hasFalse: true, nonFunction: true }) }), succeeds: true, payloadEvents: [["has", "@@iterator"], ["get", "@@iterator"], ["ownKeys"]] },
      { name: "known variant with hidden invalid payload iterator", value: () => ({ rectangle: makeShapeTypeHiddenPayloadIteratorProxy({ hasFalse: true, iteratorFactory: () => ({}) }) }), succeeds: true, payloadEvents: [["has", "@@iterator"], ["get", "@@iterator"], ["iterator-call"], ["ownKeys"]] },
      { name: "known variant with malformed hidden payload map pair", value: () => ({ rectangle: makeShapeTypeHiddenPayloadIteratorProxy({ hasFalse: true, iteratorFactory: () => { let calls = 0; return { next() { shapeTypePayloadProxyEvents.push(["next-call"]); return calls++ === 0 ? { done: false, value: 1 } : { done: true }; } }; } }) }), error: "invalid type: map, expected unit", payloadEvents: [["has", "@@iterator"], ["get", "@@iterator"], ["iterator-call"], ["next-call"], ["next-call"]] },
      { name: "known variant with hidden payload TypedArray pair key", value: () => ({ rectangle: makeShapeTypeHiddenPayloadIteratorProxy({ hasFalse: true, iteratorFactory: () => ({ next() { shapeTypePayloadProxyEvents.push(["next-call"]); return { done: false, value: [new Float32Array([1]), 1] }; } }) }) }), error: "invalid type: JsValue(Float32Array), expected any value", payloadEvents: [["has", "@@iterator"], ["get", "@@iterator"], ["iterator-call"], ["next-call"]] },
      { name: "known variant with named function payload", value: () => ({ rectangle: function payload() {} }), error: "invalid type: JsValue(Function(payload)), expected any value" },
      { name: "known variant function Proxy name diagnostic", value: () => { shapeTypePayloadProxyEvents = []; const fn = function proxyPayload() {}; return { rectangle: new Proxy(fn, { get(target, key, receiver) { if (key === "name") shapeTypePayloadProxyEvents.push(["get", "name"]); return Reflect.get(target, key, receiver); } }) }; }, error: "invalid type: JsValue(Function(proxyPayload)), expected any value", payloadEvents: [["get", "name"]] },
      { name: "known variant function name getter error", value: () => { shapeTypePayloadProxyEvents = []; const fn = function nameTarget() {}; Object.defineProperty(fn, "name", { configurable: true, get() { shapeTypePayloadProxyEvents.push(["get", "name"]); throw new Error("function name getter failed"); } }); return { rectangle: fn }; }, jsError: { name: "Error", message: "function name getter failed" }, payloadEvents: [["get", "name"]] },
      { name: "known variant with enumerable payload getter", value: () => { shapeTypePayloadProxyEvents = []; const payload = {}; Object.defineProperty(payload, "visible", { enumerable: true, get() { shapeTypePayloadProxyEvents.push(["get", "visible"]); return 1; } }); return { rectangle: payload }; }, error: "invalid type: map, expected unit", payloadEvents: [["get", "visible"]] },
      { name: "known variant with payload getter error", value: () => { shapeTypePayloadProxyEvents = []; const payload = {}; Object.defineProperty(payload, "visible", { enumerable: true, get() { shapeTypePayloadProxyEvents.push(["get", "visible"]); throw new Error("payload value getter failed"); } }); return { rectangle: payload }; }, jsError: { name: "Error", message: "payload value getter failed" }, payloadEvents: [["get", "visible"]] },
      { name: "known variant ignores enumerable symbol payload getter", value: () => { shapeTypePayloadProxyEvents = []; const payload = {}; Object.defineProperty(payload, Symbol("hidden"), { enumerable: true, get() { shapeTypePayloadProxyEvents.push(["get", "symbol"]); throw new Error("symbol payload getter must be ignored"); } }); return { rectangle: payload }; }, succeeds: true, payloadEvents: [] },
      { name: "known variant with hidden payload iterator getter error", value: () => ({ rectangle: makeShapeTypeHiddenPayloadIteratorProxy({ hasFalse: true, throwGetter: true }) }), error: "payload iterator getter failed", payloadEvents: [["has", "@@iterator"], ["get", "@@iterator"]] },
      { name: "known variant with hidden payload iterator method error", value: () => ({ rectangle: makeShapeTypeHiddenPayloadIteratorProxy({ hasFalse: true, throwMethod: true }) }), error: "payload iterator call failed", payloadEvents: [["has", "@@iterator"], ["get", "@@iterator"], ["iterator-call"]] },
      { name: "known variant with hidden payload iterator next error", value: () => ({ rectangle: makeShapeTypeHiddenPayloadIteratorProxy({ hasFalse: true, throwNext: true }) }), error: "payload iterator next failed", payloadEvents: [["has", "@@iterator"], ["get", "@@iterator"], ["iterator-call"], ["next-call"]] },
      { name: "known variant with hidden first payload next getter error", value: () => ({ rectangle: makeShapeTypeHiddenPayloadIteratorProxy({ hasFalse: true, throwFirstNextGetter: true }) }), jsError: { name: "Error", message: "payload next getter failed" }, payloadEvents: [["has", "@@iterator"], ["get", "@@iterator"], ["iterator-call"], ["next-get"]] },
      { name: "known variant with hidden second payload next getter error", value: () => ({ rectangle: makeShapeTypeHiddenPayloadIteratorProxy({ hasFalse: true, throwSecondNextGetter: true }) }), error: "payload next getter failed", payloadEvents: [["has", "@@iterator"], ["get", "@@iterator"], ["iterator-call"], ["next-get"], ["next-get"]] },
      { name: "known variant with nonfunction second payload next getter", value: () => ({ rectangle: makeShapeTypeHiddenPayloadIteratorProxy({ hasFalse: true, nonFunctionSecondNextGetter: true }) }), error: "arg0.next is not a function", errorType: "TypeError", payloadEvents: [["has", "@@iterator"], ["get", "@@iterator"], ["iterator-call"], ["next-get"], ["next-get"]] },
      { name: "known variant payload IteratorResult done/value getter order", value: () => ({ rectangle: makeShapeTypeHiddenPayloadIteratorProxy({ hasFalse: true, iteratorFactory: () => { let calls = 0; return { next() { shapeTypePayloadProxyEvents.push(["next-call"]); if (++calls === 1) return Object.defineProperties({}, { done: { get() { shapeTypePayloadProxyEvents.push(["done-get"]); return false; } }, value: { get() { shapeTypePayloadProxyEvents.push(["value-get"]); return ["x", 1]; } } }); return { done: true }; } }; } }) }), error: "invalid type: map, expected unit", payloadEvents: [["has", "@@iterator"], ["get", "@@iterator"], ["iterator-call"], ["next-call"], ["done-get"], ["value-get"], ["next-call"]] },
      { name: "known variant payload pair key before mutated value getter", value: () => ({ rectangle: makeShapeTypeHiddenPayloadIteratorProxy({ hasFalse: true, iteratorFactory: () => { let calls = 0; return { next() { shapeTypePayloadProxyEvents.push(["next-call"]); if (++calls > 1) return { done: true }; const pair = {}; Object.defineProperty(pair, "0", { get() { shapeTypePayloadProxyEvents.push(["pair-key-get"]); Object.defineProperty(pair, "1", { configurable: true, get() { shapeTypePayloadProxyEvents.push(["pair-value-get"]); throw new Error("pair value after key failed"); } }); return "x"; } }); return { done: false, value: pair }; } }; } }) }), jsError: { name: "Error", message: "pair value after key failed" }, payloadEvents: [["has", "@@iterator"], ["get", "@@iterator"], ["iterator-call"], ["next-call"], ["pair-key-get"], ["pair-value-get"]] },
      { name: "unknown single-key Map type value", value: () => new Map([["bad", null]]), error: "unknown variant `bad`, expected one of `rectangle`, `simple_polygon`, `polygon`, `multi_polygon`" },
      { name: "multi-key object type value", value: () => ({ rectangle: null, extra: 1 }), error: "invalid value: map, expected map with a single key" },
      { name: "cross-realm Map type value", value: () => foreignRealmValue("new Map([[\"rectangle\", null]])"), error: "invalid type: JsValue(Map), expected any value" },
      { name: "iterable proxy type value", value: () => shapeTypeIterableProxy, error: "invalid type: JsValue(Object({})), expected any value" },
    ];
    const shapeTypeRectangleBaseline = new Map();
    for (const kind of ["SPP", "BPP"]) {
      const baseline = runShapeTypeValueCase(kind, () => "rectangle");
      assert.equal(baseline.result.thrown, undefined, `${kind} rectangle baseline`);
      assert.deepEqual(baseline.warnings, [], `${kind} rectangle baseline`);
      shapeTypeRectangleBaseline.set(kind, baseline);
    }
    for (const kind of ["SPP", "BPP"]) {
      for (const testCase of shapeTypeValueCases) {
        const { result, warnings: actualWarnings } =
          runShapeTypeValueCase(kind, testCase.value);
        if (testCase.events)
          assert.deepEqual(shapeTypeEnumProxyEvents, testCase.events, `${kind} ${testCase.name} events`);
        if (testCase.debugEvents)
          assert.deepEqual(shapeTypeDebugEvents, testCase.debugEvents, `${kind} ${testCase.name} events`);
        if (testCase.payloadEvents)
          assert.deepEqual(shapeTypePayloadProxyEvents, testCase.payloadEvents, `${kind} ${testCase.name} payload events`);
        if (testCase.error) {
          assert.equal(result.thrown?.message, "unreachable", `${kind} ${testCase.name}`);
          assert.deepEqual(actualWarnings, [
            `Ext${kind === "SPP" ? "SP" : "BP"}Instance decode error: ${testCase.errorType ?? "Error"}: ${testCase.error}, not using fallback!!`,
          ], `${kind} ${testCase.name}`);
        } else if (testCase.jsError) {
          assert.equal(result.thrown?.name, testCase.jsError.name, `${kind} ${testCase.name}`);
          assert.equal(result.thrown?.message, testCase.jsError.message, `${kind} ${testCase.name}`);
          assert.deepEqual(actualWarnings, [], `${kind} ${testCase.name}`);
        } else if (testCase.succeeds) {
          assert.equal(result.thrown, undefined, `${kind} ${testCase.name}`);
          assert.deepEqual(actualWarnings, [], `${kind} ${testCase.name}`);
          assert.deepEqual(
            result.value.output.solution,
            shapeTypeRectangleBaseline.get(kind).result.value.output.solution,
            `${kind} ${testCase.name} solution`,
          );
        } else {
          assert.ok(result.thrown instanceof WebAssembly.RuntimeError, `${kind} ${testCase.name}`);
          assert.equal(result.thrown.message, "unreachable", `${kind} ${testCase.name}`);
          assert.deepEqual(actualWarnings, [
            `Ext${kind === "SPP" ? "SP" : "BP"}Instance decode error: ${testCase.errorType ?? "Error"}: ${testCase.error}, not using fallback!!`,
          ], `${kind} ${testCase.name}`);
        }
      }
    }
    assert.equal(shapeTypeIteratorHasReads, 2);
    assert.equal(shapeTypeUint8ArrayPrototypeReads, 2);
    assert.equal(shapeTypeArrayBufferPrototypeReads, 4);
    assert.equal(shapeTypeDebugPrototypeReads, 22);
    const qualityZoneNextGetterFailureInstance = structuredClone(bppFixture);
    const qualityZoneNextGetterFailureCounts = {
      has: 0,
      get: 0,
      methodCalls: 0,
      nextGetterReads: 0,
    };
    const qualityZoneNextGetterFailureIterator = {};
    Object.defineProperty(qualityZoneNextGetterFailureIterator, "next", {
      get() {
        qualityZoneNextGetterFailureCounts.nextGetterReads += 1;
        throw new Error("quality-zone iterator next getter failed");
      },
    });
    qualityZoneNextGetterFailureInstance.bins[0].zones = [new Proxy({
      quality: 0,
      shape: qualityZoneRectangle,
    }, {
      has(target, key) {
        if (key === Symbol.iterator)
          qualityZoneNextGetterFailureCounts.has += 1;
        return Reflect.has(target, key);
      },
      get(target, key, receiver) {
        if (key === Symbol.iterator) {
          qualityZoneNextGetterFailureCounts.get += 1;
          return () => {
            qualityZoneNextGetterFailureCounts.methodCalls += 1;
            return qualityZoneNextGetterFailureIterator;
          };
        }
        return Reflect.get(target, key, receiver);
      },
    })];
    const qualityZoneNextGetterFailure = captureThrownValue(() =>
      module.run_lbf_bpp_wasm(
        qualityZoneNextGetterFailureInstance,
        { n_samples: 1, ls_frac: 0 },
      ),
    );
    assert.ok(qualityZoneNextGetterFailure.thrown instanceof Error);
    assert.equal(
      qualityZoneNextGetterFailure.thrown.message,
      "quality-zone iterator next getter failed",
    );
    assert.deepEqual(qualityZoneNextGetterFailureCounts, {
      has: 1,
      get: 1,
      methodCalls: 1,
      nextGetterReads: 1,
    });

    const sppNameMutatesMinSeparation = structuredClone(fixture);
    const originalSppName = sppNameMutatesMinSeparation.name;
    Object.defineProperty(sppNameMutatesMinSeparation, "name", {
      configurable: true,
      enumerable: true,
      get() {
        sppNameMutatesMinSeparation.min_item_separation = -1;
        return originalSppName;
      },
    });
    const sppNameMutationError = captureThrownValue(() =>
      module.run_lbf_spp_wasm(sppNameMutatesMinSeparation, {
        n_samples: 1,
        ls_frac: 0,
      }),
    );
    assert.equal(
      sppNameMutationError.thrown,
      "Importer error: min_item_separation must be finite and nonnegative",
      "SPP must read min_item_separation after the name getter runs",
    );

    const minSeparationGetterCases = [
      ["SPP", structuredClone(fixture), (instance) =>
        module.run_lbf_spp_wasm(instance, { n_samples: 1, ls_frac: 0 })],
      ["BPP", structuredClone(bppFixture), (instance) =>
        module.run_lbf_bpp_wasm(instance, { n_samples: 1, ls_frac: 0 })],
    ];
    for (const [kind, instance, run] of minSeparationGetterCases) {
      Object.defineProperty(instance, "min_item_separation", {
        configurable: true,
        enumerable: true,
        get() {
          instance.items = [];
          return 0;
        },
      });
      const result = captureThrownValue(() => run(instance));
      assert.equal(
        result.thrown,
        "Importer error: instance must have positive item demand",
        `${kind} must read items after the min_item_separation getter runs`,
      );
    }

    let undefinedMinSeparationGets = 0;
    let undefinedMinSeparationHasCalls = 0;
    const minSeparationHasMutatesItems = new Proxy(structuredClone(fixture), {
      get(target, key, receiver) {
        if (key === "min_item_separation") {
          undefinedMinSeparationGets += 1;
          return undefined;
        }
        return Reflect.get(target, key, receiver);
      },
      has(target, key) {
        if (key === "min_item_separation") {
          undefinedMinSeparationHasCalls += 1;
          target.items = [];
          return false;
        }
        return Reflect.has(target, key);
      },
    });
    const minSeparationHasMutationError = captureThrownValue(() =>
      module.run_lbf_spp_wasm(minSeparationHasMutatesItems, {
        n_samples: 1,
        ls_frac: 0,
      }),
    );
    assert.equal(
      minSeparationHasMutationError.thrown,
      "Importer error: instance must have positive item demand",
      "the default-field has trap must run before SPP items are read",
    );
    assert.equal(undefinedMinSeparationGets, 1);
    assert.equal(undefinedMinSeparationHasCalls, 1);

    const nestedReadMutatesRoot = structuredClone(bppFixture);
    const invalidBinAfterItemRead = structuredClone(nestedReadMutatesRoot.bins[0]);
    invalidBinAfterItemRead.shape.type = "unsupported-shape";
    let itemReadCount = 0;
    nestedReadMutatesRoot.items = new Proxy(nestedReadMutatesRoot.items, {
      get(target, key, receiver) {
        if (key === "0" && itemReadCount === 0) {
          itemReadCount += 1;
          nestedReadMutatesRoot.bins = [invalidBinAfterItemRead];
        }
        return Reflect.get(target, key, receiver);
      },
    });
    const nestedReadMutationError = captureThrownValue(() =>
      module.run_lbf_bpp_wasm(nestedReadMutatesRoot, {
        n_samples: 1,
        ls_frac: 0,
      }),
    );
    assert.ok(
      nestedReadMutationError.thrown instanceof WebAssembly.RuntimeError,
      "root bins must be read after nested items are decoded, as in Rust",
    );
    assert.equal(
      nestedReadMutationError.thrown.message,
      "unreachable",
      "a nested item read that invalidates a later bin must preserve Rust's trap",
    );
    assert.equal(itemReadCount, 1, "the nested item proxy must trigger the bin mutation");

    const sppNestedReadMutatesRoot = structuredClone(fixture);
    let sppItemReadCount = 0;
    sppNestedReadMutatesRoot.items = new Proxy(sppNestedReadMutatesRoot.items, {
      get(target, key, receiver) {
        if (key === "0" && sppItemReadCount === 0) {
          sppItemReadCount += 1;
          sppNestedReadMutatesRoot.strip_height = 0;
        }
        return Reflect.get(target, key, receiver);
      },
    });
    const sppNestedReadMutationError = captureThrownValue(() =>
      module.run_lbf_spp_wasm(sppNestedReadMutatesRoot, {
        n_samples: 64,
        ls_frac: 0,
      }),
    );
    assert.equal(
      sppNestedReadMutationError.thrown,
      "Importer error: strip height must be positive",
      "SPP strip_height must be read after nested items are decoded, as in Rust",
    );
    assert.equal(
      sppItemReadCount,
      1,
      "the SPP nested item proxy must trigger the strip-height mutation",
    );

    const inheritedRootItems = structuredClone(bppFixture);
    inheritedRootItems.min_item_separation = -1;
    const inheritedItemsValue = inheritedRootItems.items;
    delete inheritedRootItems.items;
    Object.setPrototypeOf(inheritedRootItems, { items: inheritedItemsValue });
    const inheritedRootItemsError = captureThrownValue(() =>
      module.run_lbf_bpp_wasm(inheritedRootItems, { n_samples: 1, ls_frac: 0 }),
    );
    assert.equal(
      inheritedRootItemsError.thrown,
      "Importer error: min_item_separation must be finite and nonnegative",
      "serde-wasm-bindgen must read inherited root instance fields",
    );

    const inheritedRootMinSeparation = structuredClone(bppFixture);
    inheritedRootMinSeparation.min_item_separation = -1;
    delete inheritedRootMinSeparation.min_item_separation;
    Object.setPrototypeOf(inheritedRootMinSeparation, {
      min_item_separation: -1,
    });
    const inheritedRootMinSeparationError = captureThrownValue(() =>
      module.run_lbf_bpp_wasm(inheritedRootMinSeparation, {
        n_samples: 1,
        ls_frac: 0,
      }),
    );
    assert.equal(
      inheritedRootMinSeparationError.thrown,
      "Importer error: min_item_separation must be finite and nonnegative",
      "inherited root min_item_separation must retain Rust's decoded value",
    );

    const undefinedBppMinSeparation = structuredClone(bppFixture);
    undefinedBppMinSeparation.min_item_separation = undefined;
    const undefinedBppMinSeparationError = captureThrownValue(() =>
      module.run_lbf_bpp_wasm(undefinedBppMinSeparation, {
        n_samples: 1,
        ls_frac: 0,
      }),
    );
    assert.ok(
      undefinedBppMinSeparationError.thrown instanceof WebAssembly.RuntimeError,
      "explicit undefined in the defaulted BPP f32 field must preserve Rust's decode trap",
    );
    assert.equal(undefinedBppMinSeparationError.thrown.message, "unreachable");

    const undefinedPolygonInner = structuredClone(bppFixture);
    undefinedPolygonInner.min_item_separation = -1;
    undefinedPolygonInner.items[0].shape = {
      type: "polygon",
      data: {
        outer: [[0, 0], [2, 0], [2, 2], [0, 2]],
        inner: undefined,
      },
    };
    const undefinedPolygonInnerError = captureThrownValue(() =>
      module.run_lbf_bpp_wasm(undefinedPolygonInner, {
        n_samples: 1,
        ls_frac: 0,
      }),
    );
    assert.ok(
      undefinedPolygonInnerError.thrown instanceof WebAssembly.RuntimeError,
      "explicit undefined in the defaulted polygon inner list must preserve Rust's decode trap",
    );
    assert.equal(undefinedPolygonInnerError.thrown.message, "unreachable");

    const absentPolygonInner = structuredClone(bppFixture);
    absentPolygonInner.min_item_separation = -1;
    absentPolygonInner.items[0].shape = {
      type: "polygon",
      data: { outer: [[0, 0], [2, 0], [2, 2], [0, 2]] },
    };
    const absentPolygonInnerError = captureThrownValue(() =>
      module.run_lbf_bpp_wasm(absentPolygonInner, {
        n_samples: 1,
        ls_frac: 0,
      }),
    );
    assert.equal(
      absentPolygonInnerError.thrown,
      importerError,
      "an absent defaulted polygon inner list must remain equivalent to Rust's empty list",
    );

    const undefinedBinZones = structuredClone(bppFixture);
    undefinedBinZones.min_item_separation = -1;
    undefinedBinZones.bins[0].zones = undefined;
    const undefinedBinZonesError = captureThrownValue(() =>
      module.run_lbf_bpp_wasm(undefinedBinZones, {
        n_samples: 1,
        ls_frac: 0,
      }),
    );
    assert.ok(
      undefinedBinZonesError.thrown instanceof WebAssembly.RuntimeError,
      "explicit undefined in the defaulted bin zones list must preserve Rust's decode trap",
    );
    assert.equal(undefinedBinZonesError.thrown.message, "unreachable");

    const absentBinZones = structuredClone(bppFixture);
    absentBinZones.min_item_separation = -1;
    delete absentBinZones.bins[0].zones;
    const absentBinZonesError = captureThrownValue(() =>
      module.run_lbf_bpp_wasm(absentBinZones, {
        n_samples: 1,
        ls_frac: 0,
      }),
    );
    assert.equal(
      absentBinZonesError.thrown,
      importerError,
      "an absent defaulted bin zones list must remain equivalent to Rust's empty list",
    );

    for (const legacyField of [
      "allowed_orientations",
      "allowed_rotations",
      "allowed_reflection_axes",
    ]) {
      const undefinedLegacyOrientation = structuredClone(bppFixture);
      undefinedLegacyOrientation.min_item_separation = -1;
      undefinedLegacyOrientation.items[0][legacyField] = undefined;
      const undefinedLegacyOrientationError = captureThrownValue(() =>
        module.run_lbf_bpp_wasm(undefinedLegacyOrientation, {
          n_samples: 1,
          ls_frac: 0,
        }),
      );
      assert.ok(
        undefinedLegacyOrientationError.thrown instanceof WebAssembly.RuntimeError,
        `explicit undefined for legacy ${legacyField} must remain a present rejected field`,
      );
      assert.equal(undefinedLegacyOrientationError.thrown.message, "unreachable");
    }

    const unsupportedQualityZoneShapes = [
      ["polygon", {
        type: "polygon",
        data: {
          outer: [[0, 0], [2, 0], [2, 2], [0, 2]],
          inner: [],
        },
      }],
      ["multipolygon", {
        type: "multi_polygon",
        data: [
          { outer: [[0, 0], [2, 0], [2, 2], [0, 2]], inner: [] },
          { outer: [[4, 4], [5, 4], [5, 5], [4, 5]], inner: [] },
        ],
      }],
    ];
    for (const [name, shape] of unsupportedQualityZoneShapes) {
      const instance = structuredClone(bppFixture);
      instance.bins[0].zones = [{ quality: 0, shape }];
      const result = captureThrownValue(() =>
        module.run_lbf_bpp_wasm(instance, { n_samples: 1, ls_frac: 0 }),
      );
      assert.ok(
        result.thrown instanceof WebAssembly.RuntimeError,
        `BPP quality-zone ${name} must preserve Rust's unimplemented trap channel`,
      );
      assert.equal(result.thrown.message, "unreachable");
    }

    const qualityOutOfRange = structuredClone(bppFixture);
    qualityOutOfRange.bins[0].zones = [
      {
        quality: 10,
        shape: { type: "rectangle", data: { x_min: 0, y_min: 0, width: 1, height: 1 } },
      },
    ];
    const qualityOutOfRangeError = captureThrownValue(() =>
      module.run_lbf_bpp_wasm(qualityOutOfRange, { n_samples: 1, ls_frac: 0 }),
    );
    assert.ok(
      qualityOutOfRangeError.thrown instanceof WebAssembly.RuntimeError,
      "out-of-range quality levels must preserve Rust's assertion trap channel",
    );
    assert.equal(qualityOutOfRangeError.thrown.message, "unreachable");

    const oneSampleBpp = captureThrownValue(() =>
      module.run_lbf_bpp_wasm(bppFixture, {
        prng_seed: 0x12345678,
        n_samples: 1,
        ls_frac: 0,
      }),
    );
    assert.equal(
      oneSampleBpp.thrown,
      undefined,
      "the wasm32 SmallRng stream must place the BPP item within one sample",
    );
    assert.equal(oneSampleBpp.value.svgs.length, 1);
    assert.equal(
      Number.isFinite(oneSampleBpp.value.output.solution.density),
      true,
      "a successful one-sample BPP result must retain finite solution density",
    );

    const bppShortColorError = captureThrownValue(() =>
      module.run_lbf_bpp_wasm(bppFixture, {
        svg_draw_options: {
          theme: { ...svgTheme, container_fill: "#123" },
        },
        n_samples: 1,
        ls_frac: 0,
      }),
    );
    assert.ok(
      bppShortColorError.thrown instanceof WebAssembly.RuntimeError,
      "BPP short colors must preserve Rust's color-deserialization trap",
    );
    assert.equal(bppShortColorError.thrown.message, "unreachable");

    for (const field of ["name", "items", "bins"]) {
      for (const state of ["missing", "undefined"]) {
        const bppRequiredFieldInput = structuredClone(bppFixture);
        if (state === "missing") delete bppRequiredFieldInput[field];
        else bppRequiredFieldInput[field] = undefined;
        const result = captureThrownValue(() =>
          module.run_lbf_bpp_wasm(bppRequiredFieldInput, {
            n_samples: 1,
            ls_frac: 0,
          }),
        );
        assert.ok(
          result.thrown instanceof WebAssembly.RuntimeError,
          `BPP required field ${field} (${state}) must preserve Rust's decode trap`,
        );
        assert.equal(
          result.thrown.message,
          "unreachable",
          `BPP required field ${field} (${state}) must use Rust's trap message`,
        );
      }
    }

    bppFixture.min_item_separation = -1;
    warnings.length = 0;
    const bppError = captureThrownValue(
      () => module.run_lbf_bpp_wasm(bppFixture, {}),
    );
    assert.equal(
      bppError.thrown,
      "Importer error: min_item_separation must be finite and nonnegative",
    );
    assert.deepEqual(warnings, []);

    const invalidBinRectangle = structuredClone(bppFixture);
    invalidBinRectangle.min_item_separation = 0;
    invalidBinRectangle.bins[0].shape.data.width = 0;
    const bppRectangleError = captureThrownValue(() =>
      module.run_lbf_bpp_wasm(invalidBinRectangle, { n_samples: 1, ls_frac: 0 }),
    );
    assert.equal(
      bppRectangleError.thrown,
      "Importer error: invalid rectangle, x_min: 0, x_max: 0, y_min: 0, y_max: 6",
      "BPP bin rectangle errors must preserve Rust bounds details",
    );

    const bppNonFiniteRectangleCases = [
      ["positive infinite item width", (instance) => {
        instance.items[0].shape.data.width = Number.POSITIVE_INFINITY;
      }],
      ["positive infinite bin width", (instance) => {
        instance.bins[0].shape.data.width = Number.POSITIVE_INFINITY;
      }],
      ["positive infinite bin height", (instance) => {
        instance.bins[0].shape.data.height = Number.POSITIVE_INFINITY;
      }],
      ["positive infinite quality-zone width", (instance) => {
        instance.bins[0].zones = [{
          quality: 0,
          shape: {
            type: "rectangle",
            data: { x_min: 0, y_min: 0, width: Number.POSITIVE_INFINITY, height: 1 },
          },
        }];
      }],
      ["positive infinite quality-zone height", (instance) => {
        instance.bins[0].zones = [{
          quality: 0,
          shape: {
            type: "rectangle",
            data: { x_min: 0, y_min: 0, width: 1, height: Number.POSITIVE_INFINITY },
          },
        }];
      }],
      ["finite f32-overflowing bin width", (instance) => {
        instance.bins[0].shape.data.width = 1e100;
      }],
      ["finite f32-overflowing quality-zone width", (instance) => {
        instance.bins[0].zones = [{
          quality: 0,
          shape: {
            type: "rectangle",
            data: { x_min: 0, y_min: 0, width: 1e100, height: 1 },
          },
        }];
      }],
    ];
    for (const [name, mutate] of bppNonFiniteRectangleCases) {
      const instance = structuredClone(bppFixture);
      instance.min_item_separation = 0;
      mutate(instance);
      const result = captureThrownValue(() =>
        module.run_lbf_bpp_wasm(instance, { n_samples: 1, ls_frac: 0 }),
      );
      assert.ok(
        result.thrown instanceof WebAssembly.RuntimeError,
        `${name} must preserve Rust's accepted-rectangle polygon-construction trap`,
      );
      assert.equal(result.thrown.message, "unreachable", `${name} trap text`);
    }

    for (const [name, mutate, expected] of [
      ["NaN bin width", (instance) => {
        instance.bins[0].shape.data.width = Number.NaN;
      }, "Importer error: invalid rectangle, x_min: 0, x_max: NaN, y_min: 0, y_max: 6"],
      ["negative infinite bin width", (instance) => {
        instance.bins[0].shape.data.width = Number.NEGATIVE_INFINITY;
      }, "Importer error: invalid rectangle, x_min: 0, x_max: -inf, y_min: 0, y_max: 6"],
    ]) {
      const instance = structuredClone(bppFixture);
      instance.min_item_separation = 0;
      mutate(instance);
      const result = captureThrownValue(() =>
        module.run_lbf_bpp_wasm(instance, { n_samples: 1, ls_frac: 0 }),
      );
      assert.equal(result.thrown, expected, `${name} should remain a Rust rectangle error`);
    }

    for (const [name, zones, expected] of [
      [
        "Rust lower-quality NaN zone precedes positive infinity",
        [
          { quality: 1, shape: { type: "rectangle", data: { x_min: 0, y_min: 0, width: Number.POSITIVE_INFINITY, height: 1 } } },
          { quality: 0, shape: { type: "rectangle", data: { x_min: 0, y_min: 0, width: Number.NaN, height: 1 } } },
        ],
        "Importer error: invalid rectangle, x_min: 0, x_max: NaN, y_min: 0, y_max: 1",
      ],
      [
        "Rust lower-quality negative infinity zone precedes positive infinity",
        [
          { quality: 1, shape: { type: "rectangle", data: { x_min: 0, y_min: 0, width: Number.POSITIVE_INFINITY, height: 1 } } },
          { quality: 0, shape: { type: "rectangle", data: { x_min: 0, y_min: 0, width: Number.NEGATIVE_INFINITY, height: 1 } } },
        ],
        "Importer error: invalid rectangle, x_min: 0, x_max: -inf, y_min: 0, y_max: 1",
      ],
    ]) {
      const instance = structuredClone(bppFixture);
      instance.min_item_separation = 0;
      instance.bins[0].zones = zones;
      const result = captureThrownValue(() =>
        module.run_lbf_bpp_wasm(instance, { n_samples: 1, ls_frac: 0 }),
      );
      assert.equal(result.thrown, expected, `${name} must preserve Rust's quality-group order`);
    }

    const bppMixedQualityZoneShapes = structuredClone(bppFixture);
    bppMixedQualityZoneShapes.min_item_separation = 0;
    bppMixedQualityZoneShapes.bins[0].zones = [
      {
        quality: 1,
        shape: {
          type: "rectangle",
          data: { x_min: 0, y_min: 0, width: Number.POSITIVE_INFINITY, height: 1 },
        },
      },
      {
        quality: 0,
        shape: { type: "simple_polygon", data: [[0, 0], [1, 0]] },
      },
    ];
    const bppMixedQualityZoneShapesError = captureThrownValue(() =>
      module.run_lbf_bpp_wasm(bppMixedQualityZoneShapes, {
        n_samples: 1,
        ls_frac: 0,
      }),
    );
    assert.equal(
      bppMixedQualityZoneShapesError.thrown,
      "Importer error: Simple polygon must have at least 3 points: [Point(0.0, 0.0), Point(1.0, 0.0)]",
      "Rust must import lower-quality zones before higher-quality zones regardless of input order",
    );

    const bppInvalidZoneQualityBeforeInvalidOuter = structuredClone(bppFixture);
    bppInvalidZoneQualityBeforeInvalidOuter.min_item_separation = 0;
    bppInvalidZoneQualityBeforeInvalidOuter.bins[0].shape.data.width = Number.NaN;
    bppInvalidZoneQualityBeforeInvalidOuter.bins[0].zones = [
      {
        quality: 10,
        shape: {
          type: "rectangle",
          data: { x_min: 0, y_min: 0, width: 1, height: 1 },
        },
      },
    ];
    const bppInvalidZoneQualityBeforeInvalidOuterError = captureThrownValue(() =>
      module.run_lbf_bpp_wasm(bppInvalidZoneQualityBeforeInvalidOuter, {
        n_samples: 1,
        ls_frac: 0,
      }),
    );
    assert.ok(
      bppInvalidZoneQualityBeforeInvalidOuterError.thrown instanceof WebAssembly.RuntimeError,
      "Rust validates all quality IDs before importing the bin outer shape",
    );
    assert.equal(
      bppInvalidZoneQualityBeforeInvalidOuterError.thrown.message,
      "unreachable",
    );
  } finally {
    console.warn = originalWarn;
  }

  console.log("LbfWasm configuration and Importer error regression checks passed");
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
