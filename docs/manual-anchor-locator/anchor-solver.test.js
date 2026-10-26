const assert = require("node:assert/strict");
const { solve } = require("./anchor-solver.js");

function close(actual, expected, tolerance = 1e-6) {
    assert.ok(Math.abs(actual - expected) <= tolerance, `${actual} is not within ${tolerance} of ${expected}`);
}

const rectangle = solve({
    top: 3000,
    right: 2000,
    bottom: 3000,
    left: 2000,
    tlToBr: Math.hypot(3000, 2000),
    trToBl: Math.hypot(3000, 2000),
});
close(rectangle.anchors.tl.x, 0);
close(rectangle.anchors.tl.y, 2000);
close(rectangle.anchors.tr.x, 3000);
close(rectangle.anchors.tr.y, 2000);
close(rectangle.anchors.br.x, 3000);
close(rectangle.estimatedMeasurementError, 0);

const known = {
    tl: { x: 125, y: 3230 },
    tr: { x: 4310, y: 3210 },
    bl: { x: 0, y: 0 },
    br: { x: 4340, y: 0 },
};
const length = (a, b) => Math.hypot(a.x - b.x, a.y - b.y);
const noisy = solve({
    top: length(known.tl, known.tr) + 1.0,
    right: length(known.tr, known.br) - 0.5,
    bottom: length(known.bl, known.br) + 0.25,
    left: length(known.bl, known.tl) - 0.75,
    tlToBr: length(known.tl, known.br) + 0.4,
    trToBl: length(known.tr, known.bl) - 0.3,
});
assert.ok(noisy.rmsResidual < 1);
assert.ok(noisy.estimatedMeasurementError > 0);
assert.ok(Math.abs(noisy.anchors.tl.x - known.tl.x) < 3);
assert.ok(Math.abs(noisy.anchors.tr.y - known.tr.y) < 3);

assert.throws(
    () => solve({ top: 100, right: 100, bottom: 1000, left: 100, tlToBr: 100, trToBl: 100 }),
    /cannot form a frame/
);

console.log("anchor-solver tests passed");