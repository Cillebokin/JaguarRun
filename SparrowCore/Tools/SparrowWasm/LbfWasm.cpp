#include "../Lbf/LbfProductSupport.h"
#include "JaguarCore/IO/JsonCodec.h"

#include <emscripten/bind.h>
#include <emscripten/emscripten.h>
#include <emscripten/val.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

EM_JS(emscripten::EM_VAL, StringifyInputForRustSerde,
      (emscripten::EM_VAL valueHandle, int isBpp), {
    const value = Emval.toValue(valueHandle);
    const isRootArray = value !== null
        && typeof value === "object" && Array.isArray(value);
    const instanceFields = isBpp
        ? ["name", "min_item_separation", "items", "bins"]
        : ["name", "min_item_separation", "items", "strip_height"];
    let serializedRoot = value;
    let rootMinItemSeparationPresent = true;
    const rootMissingFields = new Set();
    if (value !== null && typeof value === "object") {
        serializedRoot = {};
        for (const key of instanceFields) {
            Object.defineProperty(serializedRoot, key, {
                configurable: true,
                enumerable: true,
                get() {
                    let member = Reflect.get(value, key);
                    let present;
                    if (member === undefined) {
                        present = Reflect.has(value, key);
                        if (key === "min_item_separation")
                            rootMinItemSeparationPresent = present;
                        if (isRootArray && !present)
                            rootMissingFields.add(key);
                    }
                    if (isRootArray && key === "name"
                        && (member !== undefined || present)) {
                        if (typeof member === "number") {
                            reportSerdeDecodeError(new Error(
                                "invalid type: floating point `"
                                    + serdeFloatNumberText(member)
                                    + "`, expected a string",
                            ));
                        }
                        if (typeof member !== "string")
                            reportSerdeDecodeError(
                                shapeTypeInvalidUnexpected(member, "a string"),
                            );
                    }
                    const isRootF32Field = key === "min_item_separation"
                        || (!isBpp && key === "strip_height");
                    if (isRootArray && isRootF32Field
                        && (member !== undefined || present)
                        && typeof member !== "number") {
                        reportSerdeDecodeError(
                            rootF32InvalidUnexpected(member),
                        );
                    }
                    const isRootSequenceField = key === "items"
                        || (isBpp && key === "bins");
                    if (isRootArray && isRootSequenceField
                        && (member !== undefined || present)) {
                        if (member === null
                            || (typeof member !== "object"
                                && typeof member !== "function")) {
                            reportSerdeDecodeError(new TypeError(
                                "Reflect.get called on non-object",
                            ));
                        }
                        if (!Array.isArray(member)) {
                            const sequence = readSerdeIterableSequence(member);
                            if (sequence === undefined)
                                reportSerdeDecodeError(
                                    new Error(
                                        "invalid type: JsValue("
                                            + shapeTypeJsValueName(member)
                                            + "), expected a sequence",
                                    ),
                                );
                            member = sequence;
                        }
                    }
                    return member;
                },
            });
        }
    }
    const bigInts = [];
    const nonFiniteNumbers = [];
    let markerPrefix = null;
    const ensureMarkerPrefix = () => {
        if (markerPrefix !== null)
            return;
        const randomWords = new Uint32Array(2);
        if (globalThis.crypto && typeof globalThis.crypto.getRandomValues === "function") {
            globalThis.crypto.getRandomValues(randomWords);
        } else {
            randomWords[0] = Date.now() >>> 0;
            randomWords[1] = Math.floor(Math.random() * 0x100000000) >>> 0;
        }
        markerPrefix = `__jaguar_raw_value_${randomWords[0].toString(16)}_${randomWords[1].toString(16)}_`;
    };
    let unsupportedSerdeBigInt = false;
    let unsupportedSerdeNestedToJSON = false;
    const unsupportedSerdeDynamicValue = {};
    const orientationFieldNames = new Set(["rotation"]);
    const rotationFieldNames = new Set(["mode", "angles", "step"]);
    const shapeObjectFieldNames = new Set(["type", "data"]);
    const shapeTypeVariantNames = new Set([
        "rectangle",
        "simple_polygon",
        "polygon",
        "multi_polygon",
    ]);
    const rectangleDataFieldNames = new Set(["x_min", "y_min", "width", "height"]);
    const polygonDataFieldNames = new Set(["outer", "inner"]);
    const qualityZoneFieldNames = new Set(["quality", "shape"]);
    const reportSerdeDecodeError = error => {
        console.warn(
            `Ext${isBpp ? "BP" : "SP"}Instance decode error: ${String(error)}, not using fallback!!`,
        );
        throw unsupportedSerdeDynamicValue;
    };
    const serdeFloatNumberText = number => {
        if (Number.isNaN(number))
            return "NaN";
        if (number === Number.POSITIVE_INFINITY)
            return "inf";
        if (number === Number.NEGATIVE_INFINITY)
            return "-inf";
        const source = String(number);
        const exponentIndex = source.search(/[eE]/);
        if (exponentIndex === -1)
            return Number.isInteger(number) ? source + ".0" : source;
        let [mantissa, exponent] = source.toLowerCase().split("e");
        let sign = "";
        if (mantissa.startsWith("-")) {
            sign = "-";
            mantissa = mantissa.slice(1);
        }
        const decimalIndex = mantissa.indexOf(".");
        const point = (decimalIndex === -1 ? mantissa.length : decimalIndex)
            + Number(exponent);
        const digits = mantissa.replace(".", "");
        let decimal;
        if (point <= 0)
            decimal = sign + "0." + "0".repeat(-point) + digits;
        else if (point >= digits.length)
            decimal = sign + digits + "0".repeat(point - digits.length);
        else
            decimal = sign + digits.slice(0, point) + "." + digits.slice(point);
        return Number.isInteger(number) ? decimal + ".0" : decimal;
    };
    const serdeByteKeyDecoder = new TextDecoder("utf-8", { ignoreBOM: true });
    const shapeMapByteEntryKey = entryKey => {
        let isUint8Array = false;
        try {
            isUint8Array = entryKey instanceof Uint8Array;
        } catch {
            // serde-wasm-bindgen treats failed dyn_ref checks as a non-byte value.
        }
        if (isUint8Array) {
            entryKey.length;
            return serdeByteKeyDecoder.decode(entryKey);
        }
        let isArrayBuffer = false;
        try {
            isArrayBuffer = entryKey instanceof ArrayBuffer;
        } catch {
            // Continue as a dynamic non-byte key, matching failed dyn_ref checks.
        }
        if (isArrayBuffer)
            return serdeByteKeyDecoder.decode(new Uint8Array(entryKey));
        return null;
    };
    const typedArrayTypeNames = new Set([
        "BigInt64Array",
        "BigUint64Array",
        "Float16Array",
        "Float32Array",
        "Float64Array",
        "Int8Array",
        "Int16Array",
        "Int32Array",
        "Uint8Array",
        "Uint8ClampedArray",
        "Uint16Array",
        "Uint32Array",
    ]);
    const shapeMapEntryDecodeError = entryKey => {
        const expected = '"type", "data", or other ignored fields';
        const type = typeof entryKey;
        if (type === "symbol") {
            return {
                error: new Error(
                    "invalid type: JsValue(" + String(entryKey)
                        + "), expected any value",
                ),
                stopAfterCurrentPair: true,
            };
        }
        if (type === "function") {
            return {
                error: new Error(
                    "invalid type: JsValue(Function), expected any value",
                ),
                stopAfterCurrentPair: true,
            };
        }
        let actualType;
        if (entryKey === null || type === "undefined")
            actualType = "unit value";
        else if (type === "boolean")
            actualType = "boolean `" + entryKey + "`";
        else if (type === "number")
            actualType = Number.isSafeInteger(entryKey)
                ? "integer `" + entryKey + "`"
                : "floating point `" + serdeFloatNumberText(entryKey) + "`";
        else if (type === "bigint")
            actualType = "integer `" + entryKey + "`";
        else if (Array.isArray(entryKey))
            actualType = "sequence";
        else if (ArrayBuffer.isView(entryKey)) {
            const objectTag = Object.prototype.toString.call(entryKey);
            const className = objectTag.startsWith("[object ")
                && objectTag.endsWith("]")
                ? objectTag.slice(8, -1) : null;
            if (className !== null && typedArrayTypeNames.has(className)) {
                return {
                    error: new Error(
                        "invalid type: JsValue(" + className
                            + "), expected any value",
                    ),
                    stopAfterCurrentPair: true,
                };
            }
            actualType = "map";
        } else if (Reflect.has(entryKey, Symbol.iterator)) {
            let isMap = false;
            try {
                isMap = entryKey instanceof Map;
            } catch {
                // Match serde-wasm-bindgen's failed Map type check.
            }
            if (!isMap) {
                // invalid_type_ probes byte representations again for Unexpected.
                shapeMapByteEntryKey(entryKey);
                const objectTag = Object.prototype.toString.call(entryKey);
                const className = objectTag.startsWith("[object ")
                    && objectTag.endsWith("]")
                    ? objectTag.slice(8, -1) : null;
                let debugValue = className ?? objectTag;
                if (className === "Object") {
                    try {
                        debugValue = "Object(" + JSON.stringify(entryKey) + ")";
                    } catch {
                        debugValue = "Object";
                    }
                }
                return {
                    error: new Error(
                        "invalid type: JsValue(" + debugValue
                            + "), expected any value",
                    ),
                    stopAfterCurrentPair: true,
                };
            }
            actualType = "map";
        } else
            actualType = "map";
        return {
            error: new Error(`invalid type: ${actualType}, expected ${expected}`),
            stopAfterCurrentPair: false,
        };
    };
    const shapeTypeJsValueName = value => {
        const objectTag = Object.prototype.toString.call(value);
        const className = objectTag.startsWith("[object ")
            && objectTag.endsWith("]")
            ? objectTag.slice(8, -1) : null;
        let debugValue = className ?? objectTag;
        if (className === "Object") {
            try {
                debugValue = "Object(" + JSON.stringify(value) + ")";
            } catch {
                debugValue = "Object";
            }
        } else if (value instanceof Error) {
            debugValue = `${value.name}: ${value.message}\n${value.stack}`;
        }
        return debugValue;
    };
    const shapeTypeInvalidUnexpected = (value, expected) => {
        const type = typeof value;
        if (value === null || type === "undefined")
            return new Error(`invalid type: unit value, expected ${expected}`);
        if (type === "boolean")
            return new Error("invalid type: boolean `" + value + "`, expected " + expected);
        if (type === "number") {
            const actualType = Number.isSafeInteger(value)
                ? "integer `" + value + "`"
                : "floating point `" + serdeFloatNumberText(value) + "`";
            return new Error(`invalid type: ${actualType}, expected ${expected}`);
        }
        if (type === "bigint") {
            if (value < -(1n << 63n) || value > (1n << 64n) - 1n) {
                return new Error(
                    "Couldn't deserialize i64 or u64 from a BigInt outside i64::MIN..u64::MAX bounds",
                );
            }
            return new Error("invalid type: integer `" + value + "`, expected " + expected);
        }
        if (type === "symbol")
            return new Error(`invalid type: JsValue(${String(value)}), expected any value`);
        if (type === "function") {
            if (expected === "unit") {
                const functionName = Reflect.get(value, "name", value);
                const debugName = typeof functionName === "string"
                    && functionName.length > 0
                    ? `Function(${functionName})` : "Function";
                return new Error(
                    `invalid type: JsValue(${debugName}), expected any value`,
                );
            }
            return new Error("invalid type: JsValue(Function), expected any value");
        }
        if (type === "string")
            return new Error(`invalid type: string ${JSON.stringify(value)}, expected ${expected}`);
        if (Array.isArray(value))
            return new Error(`invalid type: sequence, expected ${expected}`);
        let isUint8Array = false;
        try {
            isUint8Array = value instanceof Uint8Array;
        } catch {
            // Match serde-wasm-bindgen's failed Uint8Array type check.
        }
        if (isUint8Array) {
            void value.length;
            return new Error(`invalid type: byte array, expected ${expected}`);
        }
        let isArrayBuffer = false;
        try {
            isArrayBuffer = value instanceof ArrayBuffer;
        } catch {
            // Match serde-wasm-bindgen's failed ArrayBuffer type check.
        }
        if (isArrayBuffer)
            return new Error(`invalid type: byte array, expected ${expected}`);
        if (value !== null && type === "object") {
            const hasIterator = Reflect.has(value, Symbol.iterator);
            let isMap = false;
            if (hasIterator) {
                try {
                    isMap = value instanceof Map;
                } catch {
                    // Match serde-wasm-bindgen's failed Map type check.
                }
            }
            if (expected === "unit") {
                if (hasIterator && !isMap) {
                    let invalidAsBytes = false;
                    try {
                        invalidAsBytes = value instanceof Uint8Array;
                    } catch {
                        // Match serde-wasm-bindgen's invalid-type byte inspection.
                    }
                    if (invalidAsBytes) {
                        void value.length;
                        return new Error(`invalid type: byte array, expected ${expected}`);
                    }
                    let invalidAsArrayBuffer = false;
                    try {
                        invalidAsArrayBuffer = value instanceof ArrayBuffer;
                    } catch {
                        // Match serde-wasm-bindgen's invalid-type byte inspection.
                    }
                    if (invalidAsArrayBuffer) {
                        const byteView = new Uint8Array(value);
                        void byteView.length;
                        return new Error(`invalid type: byte array, expected ${expected}`);
                    }
                    return new Error(
                        `invalid type: JsValue(${shapeTypeJsValueName(value)}), expected any value`,
                    );
                }
                const iterableMap = readSerdeIterableMap(
                    value,
                    null,
                    reportSerdeDecodeError,
                    reportSerdeDecodeError,
                    error => reportSerdeDecodeError(
                        error.message.startsWith("invalid type: JsValue(")
                            ? error
                            : new Error("invalid type: map, expected unit"),
                    ),
                );
                const mapEntries = iterableMap === undefined
                    ? Object.entries(value)
                    : Object.entries(iterableMap);
                if (mapEntries.length === 0)
                    return null;
            }
            if (hasIterator && !isMap) {
                return new Error(
                    `invalid type: JsValue(${shapeTypeJsValueName(value)}), expected any value`,
                );
            }
            return new Error(`invalid type: map, expected ${expected}`);
        }
        return new Error(
            `invalid type: JsValue(${shapeTypeJsValueName(value)}), expected any value`,
        );
    };
    const rootF32InvalidUnexpected = value => {
        const type = typeof value;
        if (type === "bigint")
            return new Error("invalid type: JsValue(BigInt), expected f32");
        if (type === "symbol")
            return new Error(
                "invalid type: JsValue(" + String(value) + "), expected f32",
            );
        if (type === "function") {
            const functionName = Reflect.get(value, "name", value);
            const debugName = typeof functionName === "string"
                && functionName.length > 0
                ? `Function(${functionName})` : "Function";
            return new Error(
                `invalid type: JsValue(${debugName}), expected f32`,
            );
        }
        if (Array.isArray(value)) {
            let debugValue;
            try {
                debugValue = JSON.stringify(value);
            } catch {
                debugValue = "Array";
            }
            return new Error(
                "invalid type: JsValue(" + debugValue + "), expected f32",
            );
        }
        if (value !== null && type === "object") {
            let isUint8Array = false;
            try {
                isUint8Array = value instanceof Uint8Array;
            } catch {
                // Match the pinned deserializer's byte-view type check.
            }
            let isArrayBuffer = false;
            try {
                isArrayBuffer = value instanceof ArrayBuffer;
            } catch {
                // Match the pinned deserializer's byte-view type check.
            }
            if (isUint8Array || isArrayBuffer)
                return shapeTypeInvalidUnexpected(value, "f32");
            return new Error(
                "invalid type: JsValue("
                    + shapeTypeJsValueName(value)
                    + "), expected f32",
            );
        }
        return shapeTypeInvalidUnexpected(value, "f32");
    };
    const shapeTypeVariantError = variant => {
        if (typeof variant !== "string")
            return shapeTypeInvalidUnexpected(variant, "variant identifier");
        if (!shapeTypeVariantNames.has(variant)) {
            return new Error(
                "unknown variant `" + variant
                    + "`, expected one of `rectangle`, `simple_polygon`, `polygon`, `multi_polygon`",
            );
        }
        return null;
    };
    const decodeShapeTypeValue = value => {
        if (typeof value === "string") {
            const error = shapeTypeVariantError(value);
            return error === null ? { variant: value } : { error };
        }
        if (value === null || typeof value !== "object" || Array.isArray(value))
            return { error: shapeTypeInvalidUnexpected(value, "string or map") };
        let isUint8Array = false;
        try {
            isUint8Array = value instanceof Uint8Array;
        } catch {
            // Match serde-wasm-bindgen's failed Uint8Array type check.
        }
        if (isUint8Array) {
            void value.length;
            return { error: new Error("invalid type: byte array, expected string or map") };
        }
        let isArrayBuffer = false;
        try {
            isArrayBuffer = value instanceof ArrayBuffer;
        } catch {
            // Match serde-wasm-bindgen's failed ArrayBuffer type check.
        }
        if (isArrayBuffer)
            return { error: new Error("invalid type: byte array, expected string or map") };
        const hasIterator = Reflect.has(value, Symbol.iterator);
        let isMap = false;
        if (hasIterator) {
            try {
                isMap = value instanceof Map;
            } catch {
                // Match serde-wasm-bindgen's failed Map type check.
            }
        }
        if (hasIterator && !isMap) {
            let invalidAsBytes = false;
            try {
                invalidAsBytes = value instanceof Uint8Array;
            } catch {
                // Match serde-wasm-bindgen's invalid-type byte inspection.
            }
            if (invalidAsBytes) {
                void value.length;
                return {
                    error: new Error("invalid type: byte array, expected any value"),
                };
            }
            let invalidAsArrayBuffer = false;
            try {
                invalidAsArrayBuffer = value instanceof ArrayBuffer;
            } catch {
                // Match serde-wasm-bindgen's invalid-type byte inspection.
            }
            if (invalidAsArrayBuffer) {
                const byteView = new Uint8Array(value);
                void byteView.length;
                return {
                    error: new Error("invalid type: byte array, expected any value"),
                };
            }
            return {
                error: new Error(
                    `invalid type: JsValue(${shapeTypeJsValueName(value)}), expected any value`,
                ),
            };
        }
        let entries;
        if (isMap) {
            entries = Array.from(value);
        } else if (!hasIterator) {
            let iteratorMethod;
            try {
                iteratorMethod = Reflect.get(value, Symbol.iterator, value);
            } catch (error) {
                return { error };
            }
            if (typeof iteratorMethod === "function") {
                let iterator;
                try {
                    iterator = Reflect.apply(iteratorMethod, value, []);
                } catch (error) {
                    return { error };
                }
                if (iterator !== null && typeof iterator === "object"
                    && typeof Reflect.get(iterator, "next", iterator) === "function") {
                    entries = [];
                    while (true) {
                        let step;
                        try {
                            const next = Reflect.get(iterator, "next", iterator);
                            step = Reflect.apply(next, iterator, []);
                        } catch (error) {
                            return { error };
                        }
                        if (+step.done | 0)
                            break;
                        const pair = step.value;
                        entries.push([pair[0], pair[1]]);
                    }
                }
            }
        }
        if (entries === undefined)
            entries = Object.entries(value);
        if (entries.length !== 1)
            return { error: new Error("invalid value: map, expected map with a single key") };
        const [variant, payload] = entries[0];
        const variantError = shapeTypeVariantError(variant);
        if (variantError !== null)
            return { error: variantError };
        if (payload !== null && payload !== undefined) {
            const payloadError = shapeTypeInvalidUnexpected(payload, "unit");
            if (payloadError !== null)
                return { error: payloadError };
        }
        return { variant };
    };
    let rootHolder = null;
    const jsonProxyByValue = new WeakMap();
    const jsonValueByProxy = new WeakMap();
    const jsonDuplicateFieldSchemaByProxy = new WeakMap();
    const jsonMaterializedFieldNamesByProxy = new WeakMap();
    const jsonMapProxies = new WeakSet();
    const jsonPointTupleProxies = new WeakSet();
    let jsonReplacer = null;
    const hasMapInternalSlots = candidate => {
        try {
            Map.prototype.has.call(candidate, undefined);
            return true;
        } catch {
            return false;
        }
    };
    const readSerdeIteratorMap = (
        iterator,
        duplicateFieldNames = null,
        prepareEntryValue = null,
        onNextCallError = null,
        onSerdeMapError = null,
        allowNonStringEntryKeys = false,
        validateEntryKey = null,
    ) => {
        if (typeof Reflect.get(iterator, "next", iterator) !== "function")
            return undefined;
        const entries = [];
        const keys = new Set();
        let unsupportedEntryKey = false;
        let duplicateKnownField = null;
        let shapeMapDecodeError = null;
        let shapeMapDecodeErrorIsDuplicate = false;
        let hasFirstShapeTypeValue = false;
        let firstShapeTypeValue;
        while (true) {
            let step;
            try {
                const next = Reflect.get(iterator, "next", iterator);
                if (typeof next !== "function")
                    throw new TypeError("arg0.next is not a function");
                step = Reflect.apply(next, iterator, []);
            } catch (error) {
                if (onNextCallError !== null)
                    onNextCallError(error);
                throw unsupportedSerdeDynamicValue;
            }
            const done = +step.done | 0;
            if (done)
                break;
            const pair = step.value;
            let entryKey = pair[0];
            let entryValue = pair[1];
            if (validateEntryKey !== null)
                validateEntryKey(entryKey);
            if (typeof entryKey !== "string" && onSerdeMapError !== null) {
                const byteKey = shapeMapByteEntryKey(entryKey);
                if (byteKey !== null)
                    entryKey = byteKey;
            }
            if (typeof entryKey !== "string") {
                if (allowNonStringEntryKeys) {
                    if (prepareEntryValue !== null)
                        entryValue = prepareEntryValue(entryKey, entryValue);
                    entries.push([entryKey, entryValue]);
                    continue;
                }
                unsupportedEntryKey = true;
                if (onSerdeMapError !== null) {
                    const decodeError = shapeMapEntryDecodeError(entryKey);
                    if (shapeMapDecodeError === null)
                        shapeMapDecodeError = decodeError.error;
                    if (decodeError.stopAfterCurrentPair)
                        onSerdeMapError(decodeError.error);
                }
                continue;
            }
            const duplicateKey = keys.has(entryKey);
            if (!duplicateKey
                && duplicateFieldNames === shapeObjectFieldNames
                && entryKey === "type")
            {
                hasFirstShapeTypeValue = true;
                firstShapeTypeValue = entryValue;
            }
            if (duplicateKey) {
                if (duplicateFieldNames && duplicateFieldNames.has(entryKey))
                {
                    if (duplicateKnownField === null)
                        duplicateKnownField = entryKey;
                    if (onSerdeMapError !== null && shapeMapDecodeError === null) {
                        shapeMapDecodeError = new Error(
                            "duplicate field `" + entryKey + "`",
                        );
                        shapeMapDecodeErrorIsDuplicate = true;
                    }
                }
            } else {
                keys.add(entryKey);
            }
            if (prepareEntryValue !== null)
                entryValue = prepareEntryValue(entryKey, entryValue);
            entries.push([entryKey, entryValue]);
        }
        if (unsupportedEntryKey || duplicateKnownField !== null) {
            if (onSerdeMapError !== null
                && shapeMapDecodeErrorIsDuplicate
                && duplicateFieldNames === shapeObjectFieldNames
                && duplicateKnownField === "type"
                && hasFirstShapeTypeValue
                && typeof firstShapeTypeValue === "string"
                && !shapeTypeVariantNames.has(firstShapeTypeValue))
            {
                shapeMapDecodeError = new Error(
                    "unknown variant `" + firstShapeTypeValue
                        + "`, expected one of `rectangle`, `simple_polygon`, `polygon`, `multi_polygon`",
                );
            }
            if (onSerdeMapError !== null && shapeMapDecodeError !== null)
                onSerdeMapError(shapeMapDecodeError);
            throw unsupportedSerdeDynamicValue;
        }
        return allowNonStringEntryKeys ? entries : Object.fromEntries(entries);
    };
    const readSerdeIterableMap = (
        candidate,
        duplicateFieldNames = null,
        onIteratorError = null,
        onNextCallError = null,
        onSerdeMapError = null,
        allowNonStringEntryKeys = false,
        validateEntryKey = null,
    ) => {
        let iteratorMethod;
        try {
            iteratorMethod = Reflect.get(candidate, Symbol.iterator, candidate);
        } catch (error) {
            if (onIteratorError !== null)
                onIteratorError(error);
            throw error;
        }
        if (typeof iteratorMethod !== "function")
            return undefined;
        let iterator;
        try {
            iterator = Reflect.apply(iteratorMethod, candidate, []);
        } catch (error) {
            if (onIteratorError !== null)
                onIteratorError(error);
            throw error;
        }
        if (iterator === null || typeof iterator !== "object")
            return undefined;
        return readSerdeIteratorMap(
            iterator,
            duplicateFieldNames,
            null,
            onNextCallError,
            onSerdeMapError,
            allowNonStringEntryKeys,
            validateEntryKey,
        );
    };
    const readSerdeIterableSequence = candidate => {
        let iteratorMethod;
        try {
            iteratorMethod = Reflect.get(
                candidate, Symbol.iterator, candidate,
            );
        } catch (error) {
            reportSerdeDecodeError(error);
        }
        if (typeof iteratorMethod !== "function")
            return undefined;
        let iterator;
        try {
            iterator = Reflect.apply(iteratorMethod, candidate, []);
        } catch (error) {
            reportSerdeDecodeError(error);
        }
        if (iterator === null || typeof iterator !== "object")
            return undefined;
        let initialNext;
        try {
            initialNext = Reflect.get(iterator, "next", iterator);
        } catch (error) {
            reportSerdeDecodeError(error);
        }
        if (typeof initialNext !== "function")
            return undefined;

        const sequence = [];
        while (true) {
            let step;
            try {
                const next = Reflect.get(iterator, "next", iterator);
                if (typeof next !== "function")
                    throw new TypeError("arg0.next is not a function");
                step = Reflect.apply(next, iterator, []);
            } catch (error) {
                reportSerdeDecodeError(error);
            }
            if (step === null
                || (typeof step !== "object" && typeof step !== "function")) {
                reportSerdeDecodeError(new TypeError(
                    "iterator.next() returned a non-object value",
                ));
            }
            let done;
            try {
                done = +Reflect.get(step, "done", step) | 0;
            } catch (error) {
                reportSerdeDecodeError(error);
            }
            if (done)
                break;
            let value;
            try {
                value = Reflect.get(step, "value", step);
            } catch (error) {
                reportSerdeDecodeError(error);
            }
            sequence.push(value);
        }
        return sequence;
    };
    const wrapForJson = (
        candidate,
        mapProbeContext = false,
        duplicateFieldNames = null,
        materializedFieldNames = null,
        pointSequenceDepth = null,
        pointTupleExtraValue = false,
    ) => {
        if (pointSequenceDepth !== null && pointSequenceDepth > 0
            && !Array.isArray(candidate))
        {
            reportSerdeDecodeError(
                shapeTypeInvalidUnexpected(candidate, "a sequence"),
            );
        }
        if (pointSequenceDepth === 0 && !Array.isArray(candidate)) {
            reportSerdeDecodeError(
                shapeTypeInvalidUnexpected(candidate, "a tuple of size 2"),
            );
        }
        if (candidate === null || typeof candidate !== "object")
            return candidate;
        if (pointTupleExtraValue) {
            let isByteArray = false;
            try {
                isByteArray = candidate instanceof Uint8Array;
            } catch {
                // Match serde-wasm-bindgen's failed Uint8Array check.
            }
            if (isByteArray) {
                for (let index = 0; index < 4; ++index)
                    void candidate.length;
                reportSerdeDecodeError(
                    new Error(
                        "invalid length 3, expected 2 elements in sequence",
                    ),
                );
            }
            let isArrayBuffer = false;
            try {
                isArrayBuffer = candidate instanceof ArrayBuffer;
            } catch {
                // Match serde-wasm-bindgen's failed ArrayBuffer check.
            }
            if (isArrayBuffer) {
                const byteView = new Uint8Array(candidate);
                void byteView.length;
                reportSerdeDecodeError(
                    new Error(
                        "invalid length 3, expected 2 elements in sequence",
                    ),
                );
            }
        }
        const cachedByContext = jsonProxyByValue.get(candidate);
        const contextIndex = duplicateFieldNames === shapeObjectFieldNames
            ? 2
            : duplicateFieldNames === rectangleDataFieldNames ? 3
            : duplicateFieldNames === orientationFieldNames ? 4
            : duplicateFieldNames === rotationFieldNames ? 5
            : duplicateFieldNames === polygonDataFieldNames ? 6 : 1;
        const cacheIndex = pointTupleExtraValue
            ? 11 : pointSequenceDepth !== null
            ? 8 + pointSequenceDepth
            : materializedFieldNames === qualityZoneFieldNames
            ? 7 : mapProbeContext ? contextIndex : 0;
        if (cachedByContext !== undefined
            && cachedByContext[cacheIndex] !== undefined)
            return cachedByContext[cacheIndex];
        const isArray = Array.isArray(candidate);
        let isMap = hasMapInternalSlots(candidate);
        if (!isArray && materializedFieldNames === qualityZoneFieldNames) {
            let iteratorMethod;
            try {
                Reflect.has(candidate, Symbol.iterator);
                iteratorMethod = Reflect.get(
                    candidate,
                    Symbol.iterator,
                    candidate,
                );
            } catch {
                throw unsupportedSerdeDynamicValue;
            }
            if (typeof iteratorMethod === "function") {
                let iterator;
                try {
                    iterator = Reflect.apply(iteratorMethod, candidate, []);
                } catch {
                    throw unsupportedSerdeDynamicValue;
                }
                if (iterator !== null && typeof iterator === "object") {
                    const iterableMap = readSerdeIteratorMap(
                        iterator,
                        qualityZoneFieldNames,
                        (key, value) => {
                            if (key !== "shape")
                                return value;
                            const shapeSnapshot = JSON.stringify(
                                wrapForJson(value, true, shapeObjectFieldNames),
                                jsonReplacer,
                            );
                            return shapeSnapshot === undefined
                                ? undefined : JSON.parse(shapeSnapshot);
                        },
                    );
                    if (iterableMap !== undefined)
                        return wrapForJson(
                            iterableMap,
                            false,
                            qualityZoneFieldNames,
                            qualityZoneFieldNames,
                        );
                }
            }
            isMap = false;
        }
        if (!isArray && mapProbeContext && !isMap) {
            if (Reflect.has(candidate, Symbol.iterator)) {
                let candidateIsMap = false;
                if (pointTupleExtraValue) {
                    try {
                        candidateIsMap = candidate instanceof Map;
                    } catch {
                        // Match serde-wasm-bindgen's failed Map type check.
                    }
                } else {
                    candidateIsMap = candidate instanceof Map;
                }
                if (candidateIsMap && pointTupleExtraValue) {
                    // The nested ContentVisitor buffers arbitrary map keys before Point rejects tuple length.
                    readSerdeIterableMap(
                        candidate,
                        null,
                        reportSerdeDecodeError,
                        reportSerdeDecodeError,
                        null,
                        true,
                        entryKey => {
                            if (typeof entryKey === "symbol") {
                                reportSerdeDecodeError(
                                    shapeTypeInvalidUnexpected(entryKey, "any value"),
                                );
                            }
                            if (typeof entryKey === "function") {
                                const functionName = Reflect.get(
                                    entryKey,
                                    "name",
                                    entryKey,
                                );
                                const debugName = typeof functionName === "string"
                                    && functionName.length > 0
                                    ? `Function(${functionName})` : "Function";
                                reportSerdeDecodeError(
                                    new Error(
                                        `invalid type: JsValue(${debugName}), expected any value`,
                                    ),
                                );
                            }
                        },
                    );
                    reportSerdeDecodeError(
                        new Error(
                            "invalid length 3, expected 2 elements in sequence",
                        ),
                    );
                } else if (candidateIsMap) {
                    isMap = true;
                } else if (pointTupleExtraValue) {
                    let debugIsByteArray = false;
                    try {
                        debugIsByteArray = candidate instanceof Uint8Array;
                    } catch {
                        // Match serde-wasm-bindgen's JsValue debug formatting.
                    }
                    if (debugIsByteArray) {
                        void candidate.length;
                    } else {
                        let debugIsArrayBuffer = false;
                        try {
                            debugIsArrayBuffer = candidate instanceof ArrayBuffer;
                        } catch {
                            // Match serde-wasm-bindgen's JsValue debug formatting.
                        }
                        if (debugIsArrayBuffer) {
                            const byteView = new Uint8Array(candidate);
                            void byteView.length;
                        }
                    }
                    reportSerdeDecodeError(
                        new Error(
                            `invalid type: JsValue(${shapeTypeJsValueName(candidate)}), expected any value`,
                        ),
                    );
                } else if (duplicateFieldNames === shapeObjectFieldNames) {
                    const debugTag = Object.prototype.toString.call(candidate);
                    const debugClassName = debugTag.startsWith("[object ")
                        && debugTag.endsWith("]")
                        ? debugTag.slice(8, -1) : null;
                    let debugValue = debugClassName ?? debugTag;
                    if (debugClassName === "Object") {
                        try {
                            debugValue = `Object(${JSON.stringify(candidate)})`;
                        } catch {
                            debugValue = "Object";
                        }
                    }
                    console.warn(
                        `Ext${isBpp ? "BP" : "SP"}Instance decode error: Error: invalid type: JsValue(${debugValue}), expected any value, not using fallback!!`,
                    );
                    throw unsupportedSerdeDynamicValue;
                } else
                    throw unsupportedSerdeDynamicValue;
            } else {
                const onShapeDecodeError =
                    duplicateFieldNames === shapeObjectFieldNames
                        ? reportSerdeDecodeError : null;
                const iterableMap = readSerdeIterableMap(
                    candidate,
                    duplicateFieldNames,
                    onShapeDecodeError,
                    onShapeDecodeError,
                    onShapeDecodeError,
                );
                if (iterableMap !== undefined)
                    return wrapForJson(iterableMap, true, duplicateFieldNames);
                if (duplicateFieldNames === shapeObjectFieldNames
                    || duplicateFieldNames === rectangleDataFieldNames
                    || duplicateFieldNames === polygonDataFieldNames)
                {
                    return wrapForJson(
                        Object.fromEntries(Object.entries(candidate)),
                        false,
                        duplicateFieldNames,
                    );
                }
            }
        }
        let proxyTarget;
        if (isArray) {
            proxyTarget = [];
        } else if (isMap) {
            proxyTarget = new Map();
        } else {
            proxyTarget = Object.create(null);
            if (materializedFieldNames === qualityZoneFieldNames) {
                for (const [key, member] of Object.entries(candidate)) {
                    if (!qualityZoneFieldNames.has(key))
                        continue;
                    Object.defineProperty(proxyTarget, key, {
                        configurable: true,
                        enumerable: true,
                        value: member,
                    });
                }
            } else {
                for (const key of Object.keys(candidate)) {
                    Object.defineProperty(proxyTarget, key, {
                        configurable: true,
                        enumerable: true,
                        get() {
                            return Reflect.get(candidate, key, candidate);
                        },
                    });
                }
            }
        }
        let shapeTag;
        let pointTupleLength = null;
        const proxy = new Proxy(proxyTarget, {
            get(target, key) {
                if (key === "toJSON")
                    return undefined;
                let member = isArray || isMap
                    ? Reflect.get(candidate, key, candidate)
                    : Reflect.get(target, key, target);
                if (pointSequenceDepth === 0 && isArray) {
                    if (key === "length") {
                        pointTupleLength = member;
                        if (member === 0) {
                            reportSerdeDecodeError(
                                new Error(
                                    "invalid length 0, expected a tuple of size 2",
                                ),
                            );
                        }
                    } else if (key === "0" || key === "1") {
                        if (typeof member !== "number"
                            && typeof member !== "bigint") {
                            reportSerdeDecodeError(
                                shapeTypeInvalidUnexpected(member, "f32"),
                            );
                        }
                        if (key === "0" && pointTupleLength === 1) {
                            reportSerdeDecodeError(
                                new Error(
                                    "invalid length 1, expected a tuple of size 2",
                                ),
                            );
                        }
                    } else if (key === "2" && typeof member === "function") {
                        reportSerdeDecodeError(
                            shapeTypeInvalidUnexpected(member, "f32"),
                        );
                    }
                }
                if (duplicateFieldNames === shapeObjectFieldNames && key === "type") {
                    const decodedType = decodeShapeTypeValue(member);
                    if (decodedType.error !== undefined)
                        reportSerdeDecodeError(decodedType.error);
                    member = decodedType.variant;
                    shapeTag = member;
                }
                if (typeof member === "function")
                    return member.bind(candidate);
                const childMapProbeContext = mapProbeContext
                    || key === "orientation" || key === "rotation"
                    || key === "shape" || key === "data";
                let childDuplicateFieldNames = null;
                let childMaterializedFieldNames = null;
                let childPointSequenceDepth = pointSequenceDepth !== null
                    && pointSequenceDepth > 0
                    && !(isArray && key === "length")
                    ? pointSequenceDepth - 1 : null;
                if (key === "shape")
                    childDuplicateFieldNames = shapeObjectFieldNames;
                else if (key === "orientation")
                    childDuplicateFieldNames = orientationFieldNames;
                else if (key === "rotation"
                    && duplicateFieldNames === orientationFieldNames)
                    childDuplicateFieldNames = rotationFieldNames;
                else if (key === "data"
                    && duplicateFieldNames === shapeObjectFieldNames
                    && shapeTag === "rectangle")
                    childDuplicateFieldNames = rectangleDataFieldNames;
                else if (key === "data"
                    && duplicateFieldNames === shapeObjectFieldNames
                    && shapeTag === "polygon")
                    childDuplicateFieldNames = polygonDataFieldNames;
                if (key === "data"
                    && duplicateFieldNames === shapeObjectFieldNames
                    && shapeTag === "simple_polygon")
                    childPointSequenceDepth = 1;
                else if (duplicateFieldNames === polygonDataFieldNames
                    && key === "outer")
                    childPointSequenceDepth = 1;
                else if (duplicateFieldNames === polygonDataFieldNames
                    && key === "inner")
                    childPointSequenceDepth = 2;
                if (key === "zones")
                    childMaterializedFieldNames = qualityZoneFieldNames;
                else if (materializedFieldNames === qualityZoneFieldNames && isArray)
                    childMaterializedFieldNames = qualityZoneFieldNames;
                const childPointTupleExtraValue = pointSequenceDepth === 0
                    && isArray && key === "2";
                return wrapForJson(
                    member,
                    childMapProbeContext,
                    childDuplicateFieldNames,
                    childMaterializedFieldNames,
                    childPointSequenceDepth,
                    childPointTupleExtraValue,
                );
            },
        });
        const proxyCache = cachedByContext ?? [];
        proxyCache[cacheIndex] = proxy;
        jsonProxyByValue.set(candidate, proxyCache);
        jsonValueByProxy.set(proxy, candidate);
        jsonDuplicateFieldSchemaByProxy.set(proxy, duplicateFieldNames);
        jsonMaterializedFieldNamesByProxy.set(proxy, materializedFieldNames);
        if (pointSequenceDepth === 0 && isArray)
            jsonPointTupleProxies.add(proxy);
        if (isMap)
            jsonMapProxies.add(proxy);
        return proxy;
    };
    serializedRoot = wrapForJson(serializedRoot);
    let serialized;
    try {
        jsonReplacer = function (key, entry) {
        if (key === "" && rootHolder === null)
            rootHolder = this;
        if (key === "2" && jsonPointTupleProxies.has(this)) {
            if (typeof entry === "symbol")
                reportSerdeDecodeError(
                    shapeTypeInvalidUnexpected(entry, "f32"),
                );
            const deferredTupleLengthCheck = { value: entry };
            Object.defineProperty(
                deferredTupleLengthCheck,
                "checkTupleLength",
                {
                    enumerable: true,
                    get() {
                        reportSerdeDecodeError(
                            new Error(
                                "invalid length 3, expected 2 elements in sequence",
                            ),
                        );
                    },
                },
            );
            return deferredTupleLengthCheck;
        }
        if (key === "toJSON") {
            const original = jsonValueByProxy.get(this);
            if (original !== undefined) {
                const member = Reflect.get(original, key, original);
                if (typeof member === "function"
                    && !Array.isArray(original) && !jsonMapProxies.has(this)) {
                    unsupportedSerdeNestedToJSON = true;
                    return undefined;
                }
                return wrapForJson(member);
            }
        }
        if (jsonMapProxies.has(entry) && this !== rootHolder) {
            const entries = Array.from(entry.entries());
            if (entries.every(([entryKey]) => typeof entryKey === "string"))
                return wrapForJson(
                    Object.fromEntries(entries.map(
                        ([entryKey, entryValue]) => [
                            entryKey,
                            wrapForJson(entryValue, true),
                        ],
                    )),
                    true,
                    jsonDuplicateFieldSchemaByProxy.get(entry),
                    jsonMaterializedFieldNamesByProxy.get(entry),
                );
        }
        const defaultedOrRejectedField = key === "inner" || key === "zones"
            || key === "allowed_orientations" || key === "allowed_rotations"
            || key === "allowed_reflection_axes";
        if (entry === undefined && key === "min_item_separation"
            && this === serializedRoot && !rootMinItemSeparationPresent)
            return undefined;
        if (entry === undefined
            && ((key === "min_item_separation" && this === serializedRoot)
                || defaultedOrRejectedField)) {
            return null;
        }
        if (typeof entry === "number" && !Number.isFinite(entry)) {
            ensureMarkerPrefix();
            const marker = `${markerPrefix}nonfinite_${nonFiniteNumbers.length}__`;
            nonFiniteNumbers.push(Number.isNaN(entry)
                ? "NaN" : entry > 0 ? "Infinity" : "-Infinity");
            return marker;
        }
        if (typeof entry !== "bigint")
            return entry;
        if ((key === "min_item_separation" || key === "strip_height")
            && this === serializedRoot) {
            unsupportedSerdeBigInt = true;
            return null;
        }
        ensureMarkerPrefix();
        const marker = `${markerPrefix}bigint_${bigInts.length}__`;
        bigInts.push(entry.toString());
        return marker;
        };
        serialized = JSON.stringify(serializedRoot, jsonReplacer);
    } catch (error) {
        if (error === unsupportedSerdeDynamicValue)
            return Emval.toHandle(undefined);
        return Emval.toHandle({
            __jaguar_lbf_raw_serde_exception__: true,
            value: error,
        });
    }
    if (isRootArray) {
        const missingRequiredField = instanceFields.find(
            key => key !== "min_item_separation" && rootMissingFields.has(key),
        );
        if (missingRequiredField !== undefined) {
            console.warn(
                "Ext" + (isBpp ? "BP" : "SP")
                    + "Instance decode error: Error: missing field `"
                    + missingRequiredField
                    + "`, not using fallback!!",
            );
            return Emval.toHandle(undefined);
        }
    }
    if (unsupportedSerdeBigInt || unsupportedSerdeNestedToJSON)
        return Emval.toHandle(undefined);
    if (serialized === undefined)
        return Emval.toHandle(undefined);

    let json = serialized;
    for (let i = 0; i < bigInts.length; ++i) {
        const marker = `"${markerPrefix}bigint_${i}__"`;
        const index = json.indexOf(marker);
        if (index < 0 || json.indexOf(marker, index + marker.length) >= 0)
            throw new TypeError("BigInt JSON marker collision");
        json = json.slice(0, index) + bigInts[i] + json.slice(index + marker.length);
    }
    for (let i = 0; i < nonFiniteNumbers.length; ++i) {
        const marker = `"${markerPrefix}nonfinite_${i}__"`;
        const index = json.indexOf(marker);
        if (index < 0 || json.indexOf(marker, index + marker.length) >= 0)
            throw new TypeError("Non-finite JSON marker collision");
        json = json.slice(0, index) + nonFiniteNumbers[i] + json.slice(index + marker.length);
    }
    return Emval.toHandle(json);
});

namespace
{
using emscripten::val;
using sparrow::lbf_entry::Artifact;
using sparrow::lbf_entry::RuntimeConfig;

bool HasField(const val& object, const char* name)
{
    if (object.isNull() || object.isUndefined()
        || object.typeOf().as<std::string>() != "object")
    {
        return false;
    }
    return val::global("Reflect").call<bool>("has", object, val(name));
}

val Member(const val& object, const char* name)
{
    if (object.isNull() || object.isUndefined()
        || object.typeOf().as<std::string>() != "object")
    {
        return val::undefined();
    }
    return object[name];
}

bool IsNumber(const val& value, double& number)
{
    if (value.typeOf().as<std::string>() != "number")
        return false;
    number = value.as<double>();
    return std::isfinite(number);
}

bool IsSequence(const val& value)
{
    if (value.isArray())
        return true;
    if (value.isNull() || value.isUndefined()
        || value.typeOf().as<std::string>() != "object")
    {
        return false;
    }

    const val arrayBuffer = val::global("ArrayBuffer");
    return !arrayBuffer.isUndefined()
        && arrayBuffer.call<bool>("isView", value)
        && value["length"].typeOf().as<std::string>() == "number";
}

double RustF32(double value) noexcept
{
    return static_cast<double>(static_cast<float>(value));
}

struct RustRectangleFailure
{
    bool traps = false;
    std::string errorMessage;
};

std::string RustF32Display(float value)
{
    if (std::isnan(value))
        return "NaN";
    if (std::isinf(value))
        return std::signbit(value) ? "-inf" : "inf";

    char buffer[256]{};
    const auto result = std::to_chars(
        buffer, buffer + sizeof(buffer), value, std::chars_format::fixed);
    if (result.ec == std::errc{})
        return std::string(buffer, result.ptr);
    return "0";
}

std::string RustInvalidRectangleError(
    const jaguar::ExternalRectangle& rectangle)
{
    const float xMin = static_cast<float>(rectangle.xMin);
    const float yMin = static_cast<float>(rectangle.yMin);
    const float xMax = xMin + static_cast<float>(rectangle.width);
    const float yMax = yMin + static_cast<float>(rectangle.height);
    return "Importer error: invalid rectangle, x_min: "
        + RustF32Display(xMin) + ", x_max: " + RustF32Display(xMax)
        + ", y_min: " + RustF32Display(yMin) + ", y_max: "
        + RustF32Display(yMax);
}

std::optional<RustRectangleFailure> RustRectangleFailureForShape(
    const jaguar::ExternalShape& shape)
{
    const auto* rectangle = std::get_if<jaguar::ExternalRectangle>(&shape.value);
    if (!rectangle)
        return std::nullopt;

    const float xMin = static_cast<float>(rectangle->xMin);
    const float yMin = static_cast<float>(rectangle->yMin);
    const float width = static_cast<float>(rectangle->width);
    const float height = static_cast<float>(rectangle->height);
    const float xMax = xMin + width;
    const float yMax = yMin + height;
    const bool rustAcceptsBounds = xMin < xMax && yMin < yMax;
    if (!rustAcceptsBounds)
    {
        return RustRectangleFailure{
            false, RustInvalidRectangleError(*rectangle)};
    }

    const bool hasNonFiniteBound =
        !std::isfinite(xMin) || !std::isfinite(yMin) ||
        !std::isfinite(xMax) || !std::isfinite(yMax);
    if (hasNonFiniteBound)
        return RustRectangleFailure{true, {}};
    return std::nullopt;
}

bool HasRustAcceptedNonFiniteSppRectangle(const std::string& input)
{
    const auto parsed = jaguar::ParseExternalSPInstanceJson(
        input, jaguar::JsonParseOptions{true});
    if (!parsed.HasValue())
        return false;

    for (const jaguar::ExternalDemandItem& demandItem : parsed.Value().items)
    {
        if (demandItem.demand == 0)
            continue;
        const auto failure = RustRectangleFailureForShape(
            demandItem.item.shape);
        if (failure.has_value())
            return failure->traps;
    }
    return false;
}

std::optional<RustRectangleFailure> FirstRustBppRectangleFailure(
    const std::string& input)
{
    const auto parsed = jaguar::ParseExternalBPInstanceJson(
        input, jaguar::JsonParseOptions{true});
    if (!parsed.HasValue())
        return std::nullopt;

    for (const jaguar::ExternalDemandItem& demandItem : parsed.Value().items)
    {
        if (demandItem.demand == 0)
            continue;
        const auto failure = RustRectangleFailureForShape(
            demandItem.item.shape);
        if (failure.has_value())
            return failure;
    }

    std::vector<const jaguar::ExternalBin*> bins;
    bins.reserve(parsed.Value().bins.size());
    for (const jaguar::ExternalBin& bin : parsed.Value().bins)
        bins.push_back(&bin);
    std::sort(bins.begin(), bins.end(), [](const auto* lhs, const auto* rhs) {
        return lhs->container.id < rhs->container.id;
    });

    for (const jaguar::ExternalBin* bin : bins)
    {
        if (bin->stock == 0)
            continue;
        const auto outerFailure = RustRectangleFailureForShape(
            bin->container.shape);
        if (outerFailure.has_value())
            return outerFailure;

        std::vector<const jaguar::ExternalQualityZone*> zones;
        zones.reserve(bin->container.zones.size());
        for (const jaguar::ExternalQualityZone& zone : bin->container.zones)
            zones.push_back(&zone);
        std::stable_sort(zones.begin(), zones.end(), [](const auto* lhs, const auto* rhs) {
            return lhs->quality < rhs->quality;
        });
        for (const jaguar::ExternalQualityZone* zone : zones)
        {
            const auto zoneFailure = RustRectangleFailureForShape(zone->shape);
            if (zoneFailure.has_value())
                return zoneFailure;
        }
    }
    return std::nullopt;
}

bool IsUnsigned(const val& value,
                std::uint64_t& number,
                bool allowBigInt = false)
{
    if (value.typeOf().as<std::string>() == "bigint")
    {
        if (!allowBigInt)
            return false;

        const val bigInt = val::global("BigInt");
        const val bounded = bigInt.call<val>("asUintN", 64U, value);
        if (!val::global("Object").call<bool>("is", value, bounded))
            return false;

        const std::string decimal = bounded.call<std::string>("toString");
        std::uint64_t parsed = 0U;
        const auto result = std::from_chars(
            decimal.data(), decimal.data() + decimal.size(), parsed);
        if (result.ec != std::errc{} || result.ptr != decimal.data() + decimal.size())
            return false;
        number = parsed;
        return true;
    }

    double parsed = 0.0;
    constexpr double maxSafeInteger = 9007199254740991.0;
    if (!IsNumber(value, parsed) || parsed < 0.0
        || parsed > maxSafeInteger || std::floor(parsed) != parsed)
    {
        return false;
    }
    number = static_cast<std::uint64_t>(parsed);
    return true;
}

enum class ColorReadResult
{
    Parsed,
    SerdeError,
    RustPanic,
};

ColorReadResult ReadColor(const val& value, jaguar::SvgColor& color)
{
    if (value.typeOf().as<std::string>() != "string")
        return ColorReadResult::SerdeError;
    std::string text = value.as<std::string>();
    if (!text.empty() && text.front() == '#')
        text.erase(text.begin());
    if (text.size() < 6U)
        return ColorReadResult::RustPanic;
    std::uint8_t channels[3]{};
    for (std::size_t i = 0; i < 3U; ++i)
    {
        unsigned int channel = 0U;
        for (std::size_t j = 0; j < 2U; ++j)
        {
            const char digit = text[i * 2U + j];
            unsigned int nibble = 0U;
            if (digit >= '0' && digit <= '9')
                nibble = static_cast<unsigned int>(digit - '0');
            else if (digit >= 'a' && digit <= 'f')
                nibble = static_cast<unsigned int>(digit - 'a' + 10);
            else if (digit >= 'A' && digit <= 'F')
                nibble = static_cast<unsigned int>(digit - 'A' + 10);
            else
                return ColorReadResult::RustPanic;
            channel = (channel << 4U) | nibble;
        }
        channels[i] = static_cast<std::uint8_t>(channel);
    }
    color = {channels[0], channels[1], channels[2]};
    return ColorReadResult::Parsed;
}

bool ReadBoolIfPresent(const val& object,
                       const char* name,
                       bool& target)
{
    if (!HasField(object, name))
        return true;
    const val value = Member(object, name);
    if (value.typeOf().as<std::string>() != "boolean")
        return false;
    target = value.as<bool>();
    return true;
}

bool ParseTheme(const val& theme,
                RuntimeConfig& config,
                bool& rustColorPanic)
{
    double number = 0.0;
    bool missingField = false;
    const val strokeWidth = Member(theme, "stroke_width_multiplier");
    if (!HasField(theme, "stroke_width_multiplier"))
    {
        missingField = true;
    }
    else
    {
        if (!IsNumber(strokeWidth, number))
            return false;
        number = RustF32(number);
        if (!std::isfinite(number) || number <= 0.0)
            return false;
        config.svg.theme.strokeWidthMultiplier = number;
    }
    const auto readColor = [&rustColorPanic](const val& value,
                                             jaguar::SvgColor& color) {
        const ColorReadResult result = ReadColor(value, color);
        rustColorPanic = result == ColorReadResult::RustPanic;
        return result == ColorReadResult::Parsed;
    };
    const auto readColorField = [&theme, &missingField, &readColor](
                                   const char* name,
                                   jaguar::SvgColor& color) {
        const val value = Member(theme, name);
        if (!HasField(theme, name))
        {
            missingField = true;
            return true;
        }
        return readColor(value, color);
    };
    if (!readColorField("container_fill", config.svg.theme.containerFill)
        || !readColorField("item_fill", config.svg.theme.itemFill)
        || !readColorField("hole_fill", config.holeFill))
    {
        return false;
    }

    const val fills = Member(theme, "qz_fill");
    if (!HasField(theme, "qz_fill"))
    {
        missingField = true;
    }
    else
    {
        if (!fills.isArray())
            return false;
        const unsigned int fillCount = fills["length"].as<unsigned int>();
        const unsigned int parsedFillCount = fillCount < 10U ? fillCount : 10U;
        for (unsigned int i = 0; i < parsedFillCount; ++i)
        {
            if (!readColor(fills[i], config.svg.theme.qualityZoneFills[i]))
                return false;
        }
        if (fillCount != 10U)
            return false;
    }
    const val qualityOpacity = Member(theme, "qz_stroke_opac");
    if (!HasField(theme, "qz_stroke_opac"))
    {
        missingField = true;
    }
    else
    {
        if (!IsNumber(qualityOpacity, number))
            return false;
        config.svg.theme.qualityZoneStrokeOpacity = RustF32(number);
    }
    if (!readColorField("collision_highlight_color",
                        config.svg.theme.collisionHighlight))
    {
        return false;
    }
    config.svg.theme.collisionStroke = config.svg.theme.collisionHighlight;
    return !missingField;
}

bool ParseConfig(const val& input,
                 RuntimeConfig& config,
                 bool& rustColorPanic)
{
    rustColorPanic = false;
    if (input.isNull() || input.isUndefined())
        return false;
    if (input.typeOf().as<std::string>() != "object" || input.isArray())
        return false;

    std::uint64_t unsignedValue = 0U;
    double number = 0.0;
    const val cde = Member(input, "cde_config");
    if (HasField(input, "cde_config"))
    {
        if (cde.isNull() || cde.typeOf().as<std::string>() != "object")
            return false;
        if (!IsUnsigned(Member(cde, "quadtree_depth"), unsignedValue)
            || unsignedValue > std::numeric_limits<std::uint8_t>::max())
        {
            return false;
        }
        config.importer.cdeConfig.quadtreeDepth =
            static_cast<std::uint8_t>(unsignedValue);
        if (!IsUnsigned(Member(cde, "cd_threshold"), unsignedValue)
            || unsignedValue > std::numeric_limits<std::uint8_t>::max())
        {
            return false;
        }
        config.importer.cdeConfig.cdThreshold =
            static_cast<std::uint8_t>(unsignedValue);

        const val surrogate = Member(cde, "item_surrogate_config");
        if (surrogate.isNull()
            || surrogate.typeOf().as<std::string>() != "object")
        {
            return false;
        }
        const val limits = Member(surrogate, "n_pole_limits");
        if (!limits.isArray() || limits["length"].as<unsigned int>() != 3U)
            return false;
        for (unsigned int i = 0; i < 3U; ++i)
        {
            const val pair = limits[i];
            if (!IsSequence(pair) || pair["length"].as<unsigned int>() != 2U
                || !IsUnsigned(pair[0], unsignedValue)
                || unsignedValue > std::numeric_limits<std::size_t>::max())
            {
                return false;
            }
            const std::size_t count = static_cast<std::size_t>(unsignedValue);
            if (!IsNumber(pair[1], number))
                return false;
            config.importer.cdeConfig.itemSurrogateConfig
                .poleLimits[i] = {count, RustF32(number)};
        }
        if (!IsNumber(Member(surrogate, "ff_pole_area_ratio"), number))
            return false;
        config.importer.cdeConfig.itemSurrogateConfig
            .failFastPoleAreaRatio = RustF32(number);
        if (!IsUnsigned(Member(surrogate, "n_ff_piers"), unsignedValue)
            || unsignedValue > std::numeric_limits<std::size_t>::max())
        {
            return false;
        }
        config.importer.cdeConfig.itemSurrogateConfig.failFastPiers =
            static_cast<std::size_t>(unsignedValue);
    }

    const val simplify = Member(input, "poly_simpl_tolerance");
    if (HasField(input, "poly_simpl_tolerance"))
    {
        if (simplify.isNull() || simplify.isUndefined())
            config.importer.shapeModifyConfig.simplifyTolerance.reset();
        else if (IsNumber(simplify, number))
            config.importer.shapeModifyConfig.simplifyTolerance = RustF32(number);
        else
            return false;
    }
    const val narrow = Member(input, "narrow_concavity_cutoff");
    if (HasField(input, "narrow_concavity_cutoff"))
    {
        if (narrow.isNull() || narrow.isUndefined())
            config.importer.shapeModifyConfig.narrowConcavityCutoff.reset();
        else if (IsSequence(narrow) && narrow["length"].as<unsigned int>() == 2U
                 && IsNumber(narrow[0], number))
        {
            const double lower = RustF32(number);
            if (!IsNumber(narrow[1], number))
                return false;
            config.importer.shapeModifyConfig.narrowConcavityCutoff =
                std::pair<double, double>{lower, RustF32(number)};
        }
        else
            return false;
    }
    const val seed = Member(input, "prng_seed");
    if (HasField(input, "prng_seed"))
    {
        if (seed.isNull() || seed.isUndefined())
            config.configuredSeed.reset();
        else if (IsUnsigned(seed, unsignedValue, true))
            config.configuredSeed = unsignedValue;
        else
            return false;
    }
    const val samples = Member(input, "n_samples");
    if (HasField(input, "n_samples"))
    {
        if (!IsUnsigned(samples, unsignedValue)
            || unsignedValue > std::numeric_limits<std::size_t>::max())
        {
            return false;
        }
        config.optimizer.samplesPerItem =
            static_cast<std::size_t>(unsignedValue);
    }
    const val localFraction = Member(input, "ls_frac");
    if (HasField(input, "ls_frac"))
    {
        if (!IsNumber(localFraction, number))
            return false;
        config.optimizer.localSearchFraction = RustF32(number);
    }

    const val svg = Member(input, "svg_draw_options");
    if (HasField(input, "svg_draw_options"))
    {
        if (svg.isNull() || svg.typeOf().as<std::string>() != "object")
            return false;
        const val theme = Member(svg, "theme");
        if (HasField(svg, "theme")
            && (theme.isNull()
                || theme.typeOf().as<std::string>() != "object"
                || !ParseTheme(theme, config, rustColorPanic)))
        {
            return false;
        }
        if (!ReadBoolIfPresent(svg, "quadtree", config.svgQuadtree)
            || !ReadBoolIfPresent(svg, "surrogate", config.svg.drawSurrogates)
            || !ReadBoolIfPresent(svg, "highlight_collisions",
                                  config.svg.highlightCollisions)
            || !ReadBoolIfPresent(svg, "draw_cd_shapes",
                                  config.svgDrawCdShapes)
            || !ReadBoolIfPresent(svg, "highlight_cd_shapes",
                                  config.svgHighlightCdShapes))
        {
            return false;
        }
    }
    return config.optimizer.Validate().HasValue();
}

val JsonText(const val& value, bool isBpp)
{
    const val json = val::take_ownership(
        StringifyInputForRustSerde(value.as_handle(), isBpp ? 1 : 0));
    if (json.isUndefined())
    {
        __builtin_trap();
    }
    return json;
}

bool IsRawSerdeExceptionResult(const val& value)
{
    return !value.isNull()
        && value.typeOf().as<std::string>() == "object"
        && value["__jaguar_lbf_raw_serde_exception__"].as<bool>();
}

std::string ReadFallback(const char* path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open())
        return {};
    return std::string(std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>());
}

[[noreturn]] void ThrowJsString(const std::string& message)
{
    val(message).throw_();
}

void WarnInvalidConfig()
{
    const val console = val::global("console");
    if (!console.isNull() && !console.isUndefined())
        console.call<void>("warn", val("Invalid config, using default."));
}

val ToJsResult(const Artifact& artifact, const RuntimeConfig& config)
{
    constexpr std::uint64_t maxSafeInteger = 9007199254740991ULL;
    if (config.configuredSeed.has_value()
        && *config.configuredSeed > maxSafeInteger)
    {
        throw std::runtime_error(
            "Result encode error: Error: "
            + std::to_string(*config.configuredSeed)
            + " can't be represented as a JavaScript number");
    }

    const val result = val::global("JSON").call<val>(
        "parse", sparrow::lbf_entry::SerializeWasmResult(artifact));
    val outputConfig = result["output"]["config"];
    const auto& shapeConfig = config.importer.shapeModifyConfig;
    if (!shapeConfig.simplifyTolerance.has_value())
        outputConfig.set("poly_simpl_tolerance", val::undefined());
    if (!shapeConfig.narrowConcavityCutoff.has_value())
        outputConfig.set("narrow_concavity_cutoff", val::undefined());
    if (!config.configuredSeed.has_value())
        outputConfig.set("prng_seed", val::undefined());
    return result;
}

val RunSpp(val instance, val configValue)
{
    try
    {
        RuntimeConfig config = sparrow::lbf_entry::RustDefaultRuntimeConfig();
        bool rustColorPanic = false;
        if (!ParseConfig(configValue, config, rustColorPanic))
        {
            if (rustColorPanic)
                __builtin_trap();
            config = sparrow::lbf_entry::RustDefaultRuntimeConfig();
            WarnInvalidConfig();
        }
        const val json = instance.isNull() || instance.isUndefined()
            ? val(ReadFallback("/lbf_assets/swim.json"))
            : JsonText(instance, false);
        if (IsRawSerdeExceptionResult(json))
            return json;
        const std::string input = json.as<std::string>();
        if (input.empty())
            throw std::runtime_error("could not load SPP instance JSON");
        const std::uint64_t seed = config.configuredSeed.value_or(0x12345678U);
        const auto result = sparrow::lbf_entry::RunSppLbf(
            input, config, seed, "sol_input_spp.svg", {},
            sparrow::lbf_entry::ErrorTextFormat::RustWasm);
        if (!result.HasValue())
        {
            const jaguar::Error& error = result.GetError();
            if (error.code == jaguar::ErrorCode::InvalidPolygon
                && error.message == "point of inaccessibility could not be found")
            {
                // Rust unwraps Strip -> Container conversion when its
                // deflated polygon's pole search cannot produce a candidate.
                __builtin_trap();
            }
            if (error.code == jaguar::ErrorCode::OffsetFailed
                && error.message == "Simple polygon must have at least 3 points")
            {
                // Rust unwraps Strip -> Container conversion in SPProblem::new
                // and change_strip_width; importer polygon errors are formatted
                // earlier with an Importer prefix and point list.
                __builtin_trap();
            }
            if (error.code == jaguar::ErrorCode::InvalidInput
                && (error.message == "polygon contains a non-finite point"
                    || error.message == "polygon area is non-finite"))
            {
                // Extreme strip dimensions can make Rust's Strip -> Container
                // polygon construction trap while C++ reports its validation error.
                __builtin_trap();
            }
            if (error.code == jaguar::ErrorCode::InvalidInput
                && error.message
                    == "Importer error: separation leaves no usable strip height")
            {
                // Rust builds the deflated strip container and traps when the
                // separation consumes its fixed height; mirror that WASM exit.
                __builtin_trap();
            }
            if (error.code == jaguar::ErrorCode::Infeasible
                && error.message
                    == " strip width is running away, check if all items fit in the height of the strip")
            {
                __builtin_trap();
            }
            if (error.code == jaguar::ErrorCode::InvalidInput
                && error.message.rfind("Importer error: invalid rectangle,", 0) == 0
                && HasRustAcceptedNonFiniteSppRectangle(input))
            {
                __builtin_trap();
            }
            // Reparse failures because RunSppLbf shares serialization errors across stages.
            // This separates input decode traps from downstream serialization errors.
            if (error.code == jaguar::ErrorCode::SerializationFailure
                && !jaguar::ParseExternalSPInstanceJson(
                        input, jaguar::JsonParseOptions{true})
                        .HasValue())
            {
                __builtin_trap();
            }
            throw std::runtime_error(result.GetError().message);
        }
        return ToJsResult(result.Value(), config);
    }
    catch (const std::exception& error)
    {
        ThrowJsString(error.what());
    }
}

val RunBpp(val instance, val configValue)
{
    try
    {
        RuntimeConfig config = sparrow::lbf_entry::RustDefaultRuntimeConfig();
        bool rustColorPanic = false;
        if (!ParseConfig(configValue, config, rustColorPanic))
        {
            if (rustColorPanic)
                __builtin_trap();
            config = sparrow::lbf_entry::RustDefaultRuntimeConfig();
            WarnInvalidConfig();
        }
        const val json = instance.isNull() || instance.isUndefined()
            ? val(ReadFallback("/lbf_assets/baldacci1.json"))
            : JsonText(instance, true);
        if (IsRawSerdeExceptionResult(json))
            return json;
        const std::string input = json.as<std::string>();
        if (input.empty())
            throw std::runtime_error("could not load BPP instance JSON");
        const std::uint64_t seed = config.configuredSeed.value_or(0x12345678U);
        const auto result = sparrow::lbf_entry::RunBppLbf(
            input, config, seed, "sol_input", {},
            sparrow::lbf_entry::ErrorTextFormat::RustWasm);
        if (!result.HasValue())
        {
            const jaguar::Error& error = result.GetError();
            // Reparse failures because RunBppLbf shares serialization errors across stages.
            // This separates input decode traps from downstream serialization errors.
            if (error.code == jaguar::ErrorCode::SerializationFailure
                && !jaguar::ParseExternalBPInstanceJson(
                        input, jaguar::JsonParseOptions{true})
                        .HasValue())
            {
                __builtin_trap();
            }
            if (error.code == jaguar::ErrorCode::InvalidInput
                && error.message.rfind("Importer error: invalid rectangle,", 0) == 0)
            {
                const auto failure = FirstRustBppRectangleFailure(input);
                if (failure.has_value())
                {
                    if (failure->traps)
                        __builtin_trap();
                    throw std::runtime_error(failure->errorMessage);
                }
            }
            if (error.code == jaguar::ErrorCode::UnsupportedShape
                && (error.message == "Importer error: quality zone polygons are unsupported"
                    || error.message == "Importer error: quality zone multipolygons are unsupported"))
            {
                __builtin_trap();
            }
            if (error.code == jaguar::ErrorCode::InvalidInput
                && (error.message == "Importer error: quality zone is outside the supported range"
                    || error.message == "Importer error: bin stock exceeds size_t"))
            {
                __builtin_trap();
            }
            throw std::runtime_error(result.GetError().message);
        }
        return ToJsResult(result.Value(), config);
    }
    catch (const std::exception& error)
    {
        ThrowJsString(error.what());
    }
}

void InitLogger()
{
}
}

EMSCRIPTEN_BINDINGS(lbf_wasm_module)
{
    emscripten::function("run_lbf_spp_wasm", &RunSpp);
    emscripten::function("run_lbf_bpp_wasm", &RunBpp);
    emscripten::function("init_logger_wasm", &InitLogger);
}
