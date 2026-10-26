(function () {
    "use strict";

    const labels = {
        top: "A · Top",
        right: "B · Right",
        bottom: "C · Bottom",
        left: "D · Left",
        tlToBr: "E · TL–BR",
        trToBl: "F · TR–BL",
    };
    const example = {
        top: 4169.7,
        right: 3230.9,
        bottom: 4340.4,
        left: 3233.6,
        tlToBr: 5299.5,
        trToBl: 5386.0,
    };
    const form = document.getElementById("measurementForm");
    const canvas = document.getElementById("frameCanvas");
    const context = canvas.getContext("2d");
    let latestResult = null;

    function readMeasurements() {
        return Object.fromEntries(AnchorSolver.measurementNames.map((name) => [name, Number(form.elements[name].value)]));
    }

    function format(value, decimals = 2) {
        return `${value.toFixed(decimals)} mm`;
    }

    function qualityFor(error) {
        if (error <= 1) return { label: "Excellent", color: "#1d5b4f" };
        if (error <= 3) return { label: "Good", color: "#48705d" };
        if (error <= 8) return { label: "Check fit", color: "#a56b0a" };
        return { label: "Remeasure", color: "#b64e3b" };
    }

    function coordinateText(result) {
        const { tl, tr, bl, br } = result.anchors;
        return [
            `tlX: ${tl.x.toFixed(3)}`,
            `tlY: ${tl.y.toFixed(3)}`,
            `trX: ${tr.x.toFixed(3)}`,
            `trY: ${tr.y.toFixed(3)}`,
            `blX: ${bl.x.toFixed(3)}`,
            `blY: ${bl.y.toFixed(3)}`,
            `brX: ${br.x.toFixed(3)}`,
            `brY: ${br.y.toFixed(3)}`,
        ].join("\n");
    }

    function renderResult(result, measurements) {
        latestResult = result;
        const quality = qualityFor(result.estimatedMeasurementError);
        const points = [
            ["TL", result.anchors.tl],
            ["TR", result.anchors.tr],
            ["BL", result.anchors.bl],
            ["BR", result.anchors.br],
        ];

        document.getElementById("quality").textContent = quality.label;
        document.getElementById("quality").style.color = quality.color;
        document.getElementById("estimatedError").textContent = format(result.estimatedMeasurementError);
        document.getElementById("rmsError").textContent = format(result.rmsResidual);
        document.getElementById("coordinateList").innerHTML = points.map(([name, point]) =>
            `<div class="coordinate"><b>${name}</b><span>X ${format(point.x, 3)}</span><span>Y ${format(point.y, 3)}</span></div>`
        ).join("");
        document.getElementById("residualTable").innerHTML = AnchorSolver.measurementNames.map((name) =>
            `<tr><td>${labels[name]}</td><td>${measurements[name].toFixed(1)}</td><td>${result.residuals[name] >= 0 ? "+" : ""}${result.residuals[name].toFixed(2)}</td></tr>`
        ).join("");
        document.getElementById("errorBanner").style.display = "none";
        document.getElementById("results").classList.remove("is-invalid");
        document.getElementById("resultState").textContent = "using all 6 measurements";
        draw(result);
    }

    function showError(error) {
        latestResult = null;
        const banner = document.getElementById("errorBanner");
        banner.textContent = error.message;
        banner.style.display = "block";
        document.getElementById("results").classList.add("is-invalid");
        document.getElementById("resultState").textContent = "measurement issue";
    }

    function resizeCanvas() {
        const ratio = window.devicePixelRatio || 1;
        const bounds = canvas.getBoundingClientRect();
        canvas.width = Math.round(bounds.width * ratio);
        canvas.height = Math.round(bounds.height * ratio);
        context.setTransform(ratio, 0, 0, ratio, 0, 0);
        if (latestResult) draw(latestResult);
    }

    function draw(result) {
        const width = canvas.clientWidth;
        const height = canvas.clientHeight;
        context.clearRect(0, 0, width, height);
        const points = result.anchors;
        const all = Object.values(points);
        const minX = Math.min(...all.map((point) => point.x));
        const maxX = Math.max(...all.map((point) => point.x));
        const maxY = Math.max(...all.map((point) => point.y));
        const padding = Math.min(width, height) * 0.16;
        const scale = Math.min((width - padding * 2) / Math.max(maxX - minX, 1), (height - padding * 2) / Math.max(maxY, 1));
        const offsetX = (width - (maxX - minX) * scale) / 2 - minX * scale;
        const offsetY = (height - maxY * scale) / 2;
        const map = (point) => ({ x: offsetX + point.x * scale, y: height - offsetY - point.y * scale });
        const screen = Object.fromEntries(Object.entries(points).map(([name, point]) => [name, map(point)]));

        context.lineCap = "round";
        context.lineJoin = "round";
        context.strokeStyle = "rgba(29, 91, 79, 0.3)";
        context.lineWidth = 1.5;
        context.setLineDash([7, 7]);
        for (const [start, end] of [["tl", "br"], ["tr", "bl"]]) {
            context.beginPath();
            context.moveTo(screen[start].x, screen[start].y);
            context.lineTo(screen[end].x, screen[end].y);
            context.stroke();
        }

        context.setLineDash([]);
        context.strokeStyle = "#1d5b4f";
        context.lineWidth = 4;
        context.beginPath();
        context.moveTo(screen.tl.x, screen.tl.y);
        context.lineTo(screen.tr.x, screen.tr.y);
        context.lineTo(screen.br.x, screen.br.y);
        context.lineTo(screen.bl.x, screen.bl.y);
        context.closePath();
        context.stroke();

        context.font = '600 13px "IBM Plex Mono", monospace';
        context.textAlign = "center";
        context.textBaseline = "middle";
        for (const [name, point] of Object.entries(screen)) {
            context.beginPath();
            context.fillStyle = name.startsWith("t") ? "#f2c14e" : "#db6c55";
            context.arc(point.x, point.y, 15, 0, Math.PI * 2);
            context.fill();
            context.fillStyle = "#18312e";
            context.fillText(name.toUpperCase(), point.x, point.y + 0.5);
            if (width >= 500) {
                context.fillStyle = "#445b56";
                const source = points[name];
                context.fillText(`(${source.x.toFixed(1)}, ${source.y.toFixed(1)})`, point.x, point.y + (name.startsWith("t") ? -28 : 30));
            }
        }
    }

    function calculate(event) {
        if (event) event.preventDefault();
        try {
            const measurements = readMeasurements();
            renderResult(AnchorSolver.solve(measurements), measurements);
        } catch (error) {
            showError(error);
        }
    }

    document.getElementById("exampleButton").addEventListener("click", () => {
        for (const [name, value] of Object.entries(example)) form.elements[name].value = value;
        calculate();
    });
    document.getElementById("copyButton").addEventListener("click", async (event) => {
        if (!latestResult) return;
        await navigator.clipboard.writeText(coordinateText(latestResult));
        const button = event.currentTarget;
        button.textContent = "Copied";
        window.setTimeout(() => { button.textContent = "Copy coordinates"; }, 1400);
    });
    form.addEventListener("submit", calculate);
    window.addEventListener("resize", resizeCanvas);

    for (const [name, value] of Object.entries(example)) form.elements[name].value = value;
    resizeCanvas();
    calculate();
})();