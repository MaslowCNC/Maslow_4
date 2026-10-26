(function (root, factory) {
    const api = factory();
    if (typeof module === "object" && module.exports) {
        module.exports = api;
    } else {
        root.AnchorSolver = api;
    }
})(typeof globalThis !== "undefined" ? globalThis : this, function () {
    "use strict";

    const measurementNames = ["top", "right", "bottom", "left", "tlToBr", "trToBl"];

    function distance(x1, y1, x2, y2) {
        return Math.hypot(x2 - x1, y2 - y1);
    }

    function predict(parameters) {
        const [brX, tlX, tlY, trX, trY] = parameters;
        return {
            top: distance(tlX, tlY, trX, trY),
            right: distance(trX, trY, brX, 0),
            bottom: brX,
            left: distance(0, 0, tlX, tlY),
            tlToBr: distance(tlX, tlY, brX, 0),
            trToBl: distance(trX, trY, 0, 0),
        };
    }

    function residuals(parameters, measurements) {
        const predicted = predict(parameters);
        return measurementNames.map((name) => predicted[name] - measurements[name]);
    }

    function sumSquares(values) {
        return values.reduce((total, value) => total + value * value, 0);
    }

    function initialGuess(measurements) {
        const baseline = measurements.bottom;
        const tlX = (measurements.left ** 2 + baseline ** 2 - measurements.tlToBr ** 2) / (2 * baseline);
        const trX = (measurements.trToBl ** 2 + baseline ** 2 - measurements.right ** 2) / (2 * baseline);
        const tlY = Math.sqrt(Math.max(measurements.left ** 2 - tlX ** 2, 1));
        const trY = Math.sqrt(Math.max(measurements.trToBl ** 2 - trX ** 2, 1));
        return [baseline, tlX, tlY, trX, trY];
    }

    function jacobian(parameters) {
        const [brX, tlX, tlY, trX, trY] = parameters;
        const top = Math.max(distance(tlX, tlY, trX, trY), Number.EPSILON);
        const right = Math.max(distance(trX, trY, brX, 0), Number.EPSILON);
        const left = Math.max(distance(0, 0, tlX, tlY), Number.EPSILON);
        const tlToBr = Math.max(distance(tlX, tlY, brX, 0), Number.EPSILON);
        const trToBl = Math.max(distance(trX, trY, 0, 0), Number.EPSILON);

        return [
            [0, (tlX - trX) / top, (tlY - trY) / top, (trX - tlX) / top, (trY - tlY) / top],
            [(brX - trX) / right, 0, 0, (trX - brX) / right, trY / right],
            [1, 0, 0, 0, 0],
            [0, tlX / left, tlY / left, 0, 0],
            [(brX - tlX) / tlToBr, (tlX - brX) / tlToBr, tlY / tlToBr, 0, 0],
            [0, 0, 0, trX / trToBl, trY / trToBl],
        ];
    }

    function solveLinearSystem(matrix, vector) {
        const size = vector.length;
        const augmented = matrix.map((row, index) => [...row, vector[index]]);

        for (let column = 0; column < size; column += 1) {
            let pivot = column;
            for (let row = column + 1; row < size; row += 1) {
                if (Math.abs(augmented[row][column]) > Math.abs(augmented[pivot][column])) {
                    pivot = row;
                }
            }
            if (Math.abs(augmented[pivot][column]) < 1e-12) {
                return null;
            }
            [augmented[column], augmented[pivot]] = [augmented[pivot], augmented[column]];
            const divisor = augmented[column][column];
            for (let entry = column; entry <= size; entry += 1) {
                augmented[column][entry] /= divisor;
            }
            for (let row = 0; row < size; row += 1) {
                if (row === column) continue;
                const factor = augmented[row][column];
                for (let entry = column; entry <= size; entry += 1) {
                    augmented[row][entry] -= factor * augmented[column][entry];
                }
            }
        }
        return augmented.map((row) => row[size]);
    }

    function step(parameters, measurements, damping) {
        const errors = residuals(parameters, measurements);
        const derivatives = jacobian(parameters);
        const normal = Array.from({ length: 5 }, () => Array(5).fill(0));
        const gradient = Array(5).fill(0);

        for (let row = 0; row < derivatives.length; row += 1) {
            for (let column = 0; column < 5; column += 1) {
                gradient[column] += derivatives[row][column] * errors[row];
                for (let other = 0; other < 5; other += 1) {
                    normal[column][other] += derivatives[row][column] * derivatives[row][other];
                }
            }
        }
        for (let index = 0; index < 5; index += 1) {
            normal[index][index] += damping;
        }
        const delta = solveLinearSystem(normal, gradient.map((value) => -value));
        return delta && parameters.map((value, index) => value + delta[index]);
    }

    function validate(measurements) {
        for (const name of measurementNames) {
            if (!Number.isFinite(measurements[name]) || measurements[name] <= 0) {
                throw new Error("Enter a positive measurement for every side and diagonal.");
            }
        }
        const triangles = [
            ["top", "right", "tlToBr"],
            ["right", "bottom", "trToBl"],
            ["bottom", "left", "tlToBr"],
            ["left", "top", "trToBl"],
        ];
        for (const names of triangles) {
            const lengths = names.map((name) => measurements[name]).sort((a, b) => a - b);
            if (lengths[0] + lengths[1] <= lengths[2]) {
                throw new Error("These measurements cannot form a frame. Recheck the longest side or diagonal.");
            }
        }
    }

    function solve(measurements) {
        validate(measurements);
        let parameters = initialGuess(measurements);
        let damping = 1e-3;
        let score = sumSquares(residuals(parameters, measurements));

        for (let iteration = 0; iteration < 100; iteration += 1) {
            const candidate = step(parameters, measurements, damping);
            if (!candidate || candidate[0] <= 0 || candidate[2] <= 0 || candidate[4] <= 0) {
                damping *= 10;
                continue;
            }
            const candidateScore = sumSquares(residuals(candidate, measurements));
            if (candidateScore < score) {
                const improvement = score - candidateScore;
                parameters = candidate;
                score = candidateScore;
                damping = Math.max(damping / 3, 1e-12);
                if (improvement < 1e-12) break;
            } else {
                damping *= 10;
            }
        }

        const predicted = predict(parameters);
        const differences = Object.fromEntries(measurementNames.map((name) => [name, predicted[name] - measurements[name]]));
        const [brX, tlX, tlY, trX, trY] = parameters;
        return {
            anchors: {
                tl: { x: tlX, y: tlY },
                tr: { x: trX, y: trY },
                bl: { x: 0, y: 0 },
                br: { x: brX, y: 0 },
            },
            predicted,
            residuals: differences,
            rmsResidual: Math.sqrt(score / measurementNames.length),
            estimatedMeasurementError: Math.sqrt(score),
        };
    }

    return { solve, measurementNames };
});