import { beforeEach, describe, expect, test } from "bun:test";
import {
  MaslowErrMsgNoKey,
  MaslowErrMsgNoValue,
  MaslowErrMsgNoMatchingKey,
  MaslowErrMsgKeyValueCantUse,
  MaslowErrMsgKeyValueSuffix,
  maslowMsgHandling
} from "../maslow";

describe('maslowMsgHandling', () => {
  beforeEach(() => {
    // Set up the DOM elements and initial values
    document.body.innerHTML = `
              <input id="retractionForce" value="" />
              <input id="spoilboardThickness" value="" />
              <input id="workThickness" value="" />
              <input id="applyTensionBeltRetractionLimit" value="" />
              <input id="applyTensionAllowLimiting" value="" />
              <input id="machineWidth" value="" />
              <input id="machineHeight" value="" />
          `;
    global.loadedValues = {};
    global.initialGuess = {
      tr: { x: 0, y: 0, z: 0 },
      tl: { x: 0, y: 0, z: 0 },
      br: { x: 0, y: 0, z: 0 },
      bl: { x: 0, y: 0, z: 0 }
    };
    global.acceptableCalibrationThreshold = 0.5;
  });

  const inputValidation = [
    ["invalidMessage", `${MaslowErrMsgKeyValueCantUse} ${MaslowErrMsgKeyValueSuffix}invalidMessage`],
    ["=value", `${MaslowErrMsgNoKey} ${MaslowErrMsgKeyValueSuffix}=value`],
    ["key=", `${MaslowErrMsgNoValue} ${MaslowErrMsgKeyValueSuffix}key=`],
    ["unknownKey=value", `${MaslowErrMsgNoMatchingKey} ${MaslowErrMsgKeyValueSuffix}unknownKey=value`],
  ];

  test.each(inputValidation)("Input %p results in %p", (inp, expected) => {
    const result = maslowMsgHandling(inp);
    expect(result).toBe(expected);
  });

  const stdActions = [
    ["Retract_Current_Threshold", "1500", "retractionForce"],
    ["spoilboardThickness", "5.5", "spoilboardThickness"],
    ["workThickness", "19.0", "workThickness"],
    ["Apply_Tension_Belt_Retraction_Limit", "300", "applyTensionBeltRetractionLimit"],
    ["Apply_Tension_Allow_Limiting", "true", "applyTensionAllowLimiting"],
  ];

  const noErrorResult = (key, value) => {
    const result = maslowMsgHandling(`${key}=${value}`);
    expect(result).toBe("");
  }

  test.each(stdActions)("Key %p sets value %p into %p", (key, value, outputValueName) => {
    noErrorResult(key, value);
    expect(global.loadedValues[outputValueName]).toBe(value);
  });

  const setDim = (key, value, outDim, outValue) => {
    noErrorResult(key, value);
    if (typeof outValue === "undefined") {
      outValue = parseFloat(value);
    }
    if (Array.isArray(outDim)) {
      expect(global.initialGuess[outDim[0]][outDim[1]]).toBe(outValue);
    } else {
      expect(global[outDim]).toBe(outValue);
    }
  };

  const fullDimensionActions = [
    ["trX", "3000", "machineWidth", ["tr", "x"]],
    ["trY", "2000", "machineHeight", ["tr", "y"]],
  ];

  test.each(fullDimensionActions)("Key %p with value %p sets %p and %p.%p as well", (key, value, outputValueName, outDim) => {
    setDim(key, value, outDim, undefined);
    expect(global.loadedValues[outputValueName]).toBe(value);
  });

  const stdDimensionActions = [
    ["trZ", "50", ["tr", "z"]],
    ["tlX", "20", ["tl", "x"]],
    ["tlY", "10", ["tl", "y"]],
    ["tlZ", "40", ["tl", "z"]],
    ["brX", "20", ["br", "x"]],
    ["brY", "15", ["br", "y"], 0],
    ["brZ", "40", ["br", "z"]],
    ["blX", "16", ["bl", "x"], 0],
    ["blY", "17", ["bl", "y"], 0],
    ["blZ", "70", ["bl", "z"]],
    ["Acceptable_Calibration_Threshold", "1500", "acceptableCalibrationThreshold"],
  ];

  test.each(stdDimensionActions)("Key %p with value %p sets %p", (key, value, outDim, outValue = undefined) => {
    setDim(key, value, outDim, outValue);
  });
});
