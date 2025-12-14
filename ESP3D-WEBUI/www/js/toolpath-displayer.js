// Display the XY-plane projection of a GCode toolpath on a 2D canvas

var root = window;

const canvas = document.getElementById("small-toolpath");
const scale = window.devicePixelRatio;
const width = window.innerWidth; 
canvas.width = width * scale;
canvas.height = (width / 2) * scale;
var tp = canvas.getContext("2d", { willReadFrequently: true });
var tpRect;

tp.lineWidth = 0.1;
tp.lineCap = 'round';
tp.strokeStyle = 'black';

var cameraAngle = 2; // Default to top-down view

// Default fallback values (will be replaced by actual configuration values)
var tlX = -8.339;
var tlY = 2209;
var trX = 3505; 
var trY = 2209;
var blX = 0;
var blY = 0;
var brX = 3505;
var brY = 0;

// Function to update anchor points from configuration
function updateAnchorPointsFromConfig() {
    if (globalThis.initialGuess) {
        if (globalThis.initialGuess.tl) {
            tlX = globalThis.initialGuess.tl.x || tlX;
            tlY = globalThis.initialGuess.tl.y || tlY;
        }
        if (globalThis.initialGuess.tr) {
            trX = globalThis.initialGuess.tr.x || trX;
            trY = globalThis.initialGuess.tr.y || trY;
        }
        if (globalThis.initialGuess.bl) {
            blX = globalThis.initialGuess.bl.x || blX;
            blY = globalThis.initialGuess.bl.y || blY;
        }
        if (globalThis.initialGuess.br) {
            brX = globalThis.initialGuess.br.x || brX;
            brY = globalThis.initialGuess.br.y || brY;
        }
    }
}

//Draw buttons
const tlC = document.getElementById("tlBtn").getContext("2d");
tlC.fillStyle = "#b69fcb";
tlC.fillRect(0, 0, 500, 500);
tlC.beginPath();
tlC.moveTo(90, 40);
tlC.lineTo(90, 140);
tlC.lineTo(230, 40);
tlC.lineTo(90, 40);
tlC.closePath();
tlC.lineWidth = 5;
tlC.strokeStyle = 'white';
tlC.fillStyle = 'white';
tlC.fill();
tlC.stroke();

const trC = document.getElementById("trBtn").getContext("2d");
trC.fillStyle = "#b69fcb";
trC.fillRect(0, 0, 500, 500);
trC.beginPath();
trC.moveTo(90, 40);
trC.lineTo(230, 140);
trC.lineTo(230, 40);
trC.lineTo(90, 40);
trC.closePath();
trC.lineWidth = 5;
trC.strokeStyle = 'white';
trC.fillStyle = 'white';
trC.fill();
trC.stroke();

const blC = document.getElementById("blBtn").getContext("2d");
blC.fillStyle = "#b69fcb";
blC.fillRect(0, 0, 500, 500);
blC.beginPath();
blC.moveTo(90, 40);
blC.lineTo(230, 140);
blC.lineTo(90, 140);
blC.lineTo(90, 40);
blC.closePath();
blC.lineWidth = 5;
blC.strokeStyle = 'white';
blC.fillStyle = 'white';
blC.fill();
blC.stroke();

const brC = document.getElementById("brBtn").getContext("2d");
brC.fillStyle = "#b69fcb";
brC.fillRect(0, 0, 500, 500);
brC.beginPath();
brC.moveTo(90, 140);
brC.lineTo(230, 140);
brC.lineTo(230, 40);
brC.lineTo(90, 140);
brC.closePath();
brC.lineWidth = 5;
brC.strokeStyle = 'white';
brC.fillStyle = 'white';
brC.fill();
brC.stroke();

const upC = document.getElementById("upBtn").getContext("2d");
upC.fillStyle = "#9d88c0";
upC.fillRect(0, 0, 500, 500);
// #rect441
upC.beginPath();
upC.fillStyle = 'white';
upC.lineWidth = 1;
upC.rect(60+49.213840, 99.622299, 93.976021, 74.721062);
upC.fill();
    
// #path608
upC.beginPath();
upC.strokeStyle = 'white';
upC.lineWidth = 1;
upC.lineCap = 'butt';
upC.lineJoin = 'miter';
upC.moveTo(60+5.109692, 104.666810);
upC.lineTo(60+94.679220, 4.145211);
upC.lineTo(60+189.305070, 103.959000);
upC.lineTo(60+5.109692, 104.666810);
upC.closePath();
upC.stroke();
upC.fill();



const dnC = document.getElementById("dnBtn").getContext("2d");
dnC.fillStyle = "#9d88c0";
dnC.fillRect(0, 0, 500, 500);
// #rect441
dnC.save();
dnC.transform(1.000000, 0.000000, 0.000000, -1.000000, 0.000000, 0.000000);
dnC.fillStyle = 'white';
dnC.lineWidth = 1;
dnC.rect(60 + 49.213840, -75.901474, 93.976021, 74.721062);
dnC.fill();
dnC.restore();
    
// #path608
dnC.beginPath();
dnC.strokeStyle = 'white';
dnC.fillStyle = 'white';
dnC.lineWidth = 1;
dnC.lineCap = 'butt';
dnC.lineJoin = 'miter';
dnC.moveTo(60 + 5, 70 - 20);
dnC.lineTo(60 + 94, 171 - 20);
dnC.lineTo(60 + 189, 71 - 20);
dnC.lineTo(60 + 5, 70 - 20);
dnC.closePath();
dnC.stroke();
dnC.fill();

const rC = document.getElementById("rBtn").getContext("2d");
rC.fillStyle = "#9d88c0";
rC.fillRect(0, 0, 500, 500);
// #g1100
rC.save();
rC.transform(0.000000, 1.000000, -1.000000, 0.000000, 187.481000, 0.273690);
    
// #rect441
rC.fillStyle = 'white';
rC.lineWidth = 1;
rC.rect(-20 + 49.213840, 99.622299 - 80, 93.976021, 74.721062);
rC.fill();
    
// #path608
rC.beginPath();
rC.strokeStyle = 'white';
rC.lineWidth = 1;
rC.lineCap = 'butt';
rC.lineJoin = 'miter';
rC.moveTo(-20+5.109692, 104.666810 - 80);
rC.lineTo(-20+94.679220, 4.145213 - 80);
rC.lineTo(-20+189.305070, 103.959000 - 80);
rC.closePath();
rC.stroke();
rC.fill();
rC.restore();


const lC = document.getElementById("lBtn").getContext("2d");
lC.fillStyle = "#9d88c0";
lC.fillRect(0, 0, 500, 500);
// #g1100
lC.save();
lC.transform(0.000000, 1.000000, 1.000000, 0.000000, 11.957500, 0.273690);
    
// #rect441
lC.fillStyle = 'white';
lC.lineWidth = 1;
lC.rect(-20 + 49.213840, 99.622299, 93.976021, 74.721062);
lC.fill();
    
// #path608
lC.beginPath();
lC.strokeStyle = 'white';
lC.lineWidth = 1;
lC.lineCap = 'butt';
lC.lineJoin = 'miter';
lC.moveTo(-20 + 5.109692, 104.666810);
lC.lineTo(-20 + 94.679220, 4.145213);
lC.lineTo(-20 + 189.305070, 103.959000);
lC.closePath();
lC.stroke();
lC.fill();
lC.restore();

const hC = document.getElementById("hBtn").getContext("2d");

const xO = 55;
const yO = -45;

// #path5094
hC.beginPath();
hC.fillStyle = 'rgb(183, 161, 208)';
hC.strokeStyle = 'rgb(0, 0, 0)';
hC.lineWidth = 0.472615;
hC.lineCap = 'butt';
hC.lineJoin = 'miter';
hC.moveTo(xO + 55.719343, 197.549650 + yO);
hC.lineTo(xO + 152.150650, 197.549650 + yO);
hC.lineTo(xO + 152.609520, 74.078285 + yO);
hC.lineTo(xO + 132.404810, 73.680279 + yO);
hC.lineTo(xO + 131.393420, 110.310850 + yO);
hC.lineTo(xO + 103.475730, 84.035976 + yO);
hC.lineTo(xO + 54.341657, 131.433070 + yO);
hC.fill();
hC.stroke();
    
// #rect1898
hC.beginPath();
hC.fillStyle = 'rgb(218, 208, 230)';
hC.lineWidth = 0.472615;
hC.rect(xO + 74.087212, 146.169600 + yO, 29.847790, 50.981743);
hC.fill();
    
// #path13430
hC.beginPath();
hC.fillStyle = 'rgb(151, 132, 181)';
hC.strokeStyle = 'rgb(0, 0, 0)';
hC.lineWidth = 0.472615;
hC.lineCap = 'butt';
hC.lineJoin = 'miter';
hC.moveTo(xO + 103.475730, 84.035976 + yO);
hC.lineTo(xO + 167.304170, 144.974770 + yO);
hC.lineTo(xO + 181.080090, 132.229340 + yO);
hC.lineTo(xO + 103.016580, 56.951581 + yO);
hC.lineTo(xO + 24.953156, 131.432760 + yO);
hC.lineTo(xO + 40.565818, 144.974770 + yO);
hC.fill();
hC.stroke();

//---------------------------

const playC = document.getElementById("playBtn").getContext("2d");
playC.fillStyle = "#4aa85c";
playC.fillRect(0, 0, 500, 500);

playC.beginPath();
playC.strokeStyle = 'white';
playC.fillStyle = 'white';
playC.lineWidth = 1;
playC.lineCap = 'butt';
playC.lineJoin = 'miter';
playC.moveTo(60 + 44.053484, 147.608260 - 35);
playC.lineTo(60 + 44.053484, 68.502834 - 35);
playC.lineTo(60 + 112.311470, 106.828610 - 35);
playC.closePath();
playC.fill();
playC.stroke();


const stopC = document.getElementById("stopBtn").getContext("2d");
stopC.fillStyle = "#cd654c";
stopC.fillRect(0, 0, 500, 500);

stopC.strokeStyle = 'white';
stopC.fillStyle = 'white';
stopC.beginPath();
stopC.fillStyle = 'white';
stopC.lineWidth = 1;
stopC.rect(60 + 44, 65 - 35, 100, 80);
stopC.fill();
stopC.stroke();

var tpUnits = 'G21';

var tpBbox = {
    min: {
        x: Infinity,
        y: Infinity,
        z: Infinity
    },
    max: {
        x: -Infinity,
        y: -Infinity,
        z: -Infinity
    }
};

// Separate tracking for just the job/gcode bounding box (excluding machine bounds)
var jobBbox = {
    min: {
        x: Infinity,
        y: Infinity,
        z: Infinity
    },
    max: {
        x: -Infinity,
        y: -Infinity,
        z: -Infinity
    }
};

// Storage for toolpath points to compute convex hull
var jobToolpathPoints = [];
var jobEnvelopePoints = []; // Simplified convex hull points (up to 100)

var bboxIsSet = false;
var jobBboxIsSet = false;
var initialMovesForBbox = true; // Track if we're still in initial positioning phase

var resetBbox = function() {
    tpBbox.min.x = Infinity;
    tpBbox.min.y = Infinity;
    tpBbox.min.z = Infinity;
    tpBbox.max.x = -Infinity;
    tpBbox.max.y = -Infinity;
    tpBbox.max.z = -Infinity;
    bboxIsSet = false;
    
    // Also reset job bounding box
    jobBbox.min.x = Infinity;
    jobBbox.min.y = Infinity;
    jobBbox.min.z = Infinity;
    jobBbox.max.x = -Infinity;
    jobBbox.max.y = -Infinity;
    jobBbox.max.z = -Infinity;
    jobBboxIsSet = false;
    initialMovesForBbox = true;
    
    // Reset toolpath points
    jobToolpathPoints = [];
    jobEnvelopePoints = [];
}

// Helper functions for job bounding box
var jobBboxExists = function() {
    return jobBboxIsSet && 
           isFinite(jobBbox.min.x) && isFinite(jobBbox.min.y) && isFinite(jobBbox.min.z) &&
           isFinite(jobBbox.max.x) && isFinite(jobBbox.max.y) && isFinite(jobBbox.max.z);
}

var getJobBoundingBox = function() {
    if (!jobBboxExists()) {
        return null;
    }
    return {
        min: { x: jobBbox.min.x, y: jobBbox.min.y, z: jobBbox.min.z },
        max: { x: jobBbox.max.x, y: jobBbox.max.y, z: jobBbox.max.z }
    };
}

// Convex Hull algorithm (Andrew's monotone chain)
var computeConvexHull = function(points) {
    if (points.length < 3) {
        return points;
    }
    
    // Sort points lexicographically (first by x, then by y)
    var sorted = points.slice().sort(function(a, b) {
        return a.x !== b.x ? a.x - b.x : a.y - b.y;
    });
    
    // Build lower hull
    var lower = [];
    for (var i = 0; i < sorted.length; i++) {
        while (lower.length >= 2 && 
               crossProduct(lower[lower.length - 2], lower[lower.length - 1], sorted[i]) <= 0) {
            lower.pop();
        }
        lower.push(sorted[i]);
    }
    
    // Build upper hull
    var upper = [];
    for (var i = sorted.length - 1; i >= 0; i--) {
        while (upper.length >= 2 && 
               crossProduct(upper[upper.length - 2], upper[upper.length - 1], sorted[i]) <= 0) {
            upper.pop();
        }
        upper.push(sorted[i]);
    }
    
    // Remove last point of each half because it's repeated
    lower.pop();
    upper.pop();
    
    return lower.concat(upper);
}

// Cross product for convex hull
var crossProduct = function(o, a, b) {
    return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}

// Douglas-Peucker algorithm for simplifying polylines
var simplifyPolyline = function(points, maxPoints) {
    if (points.length <= maxPoints) {
        return points;
    }
    
    // Use iterative approach to reduce points to maxPoints
    var epsilon = 0.1; // Start with small tolerance
    var simplified = points;
    
    // Increase epsilon until we have <= maxPoints
    while (simplified.length > maxPoints && epsilon < 1000) {
        simplified = douglasPeucker(points, epsilon);
        epsilon *= 1.5;
    }
    
    return simplified;
}

// Douglas-Peucker recursive algorithm
var douglasPeucker = function(points, epsilon) {
    if (points.length < 3) {
        return points;
    }
    
    // Find the point with maximum distance
    var dmax = 0;
    var index = 0;
    var end = points.length - 1;
    
    for (var i = 1; i < end; i++) {
        var d = perpendicularDistance(points[i], points[0], points[end]);
        if (d > dmax) {
            index = i;
            dmax = d;
        }
    }
    
    // If max distance is greater than epsilon, recursively simplify
    var result = [];
    if (dmax > epsilon) {
        var recResults1 = douglasPeucker(points.slice(0, index + 1), epsilon);
        var recResults2 = douglasPeucker(points.slice(index), epsilon);
        
        // Concatenate results
        result = recResults1.slice(0, -1).concat(recResults2);
    } else {
        result = [points[0], points[end]];
    }
    
    return result;
}

// Calculate perpendicular distance from point to line
var perpendicularDistance = function(point, lineStart, lineEnd) {
    var dx = lineEnd.x - lineStart.x;
    var dy = lineEnd.y - lineStart.y;
    
    var mag = Math.sqrt(dx * dx + dy * dy);
    if (mag === 0) {
        return Math.sqrt((point.x - lineStart.x) * (point.x - lineStart.x) + 
                        (point.y - lineStart.y) * (point.y - lineStart.y));
    }
    
    var u = ((point.x - lineStart.x) * dx + (point.y - lineStart.y) * dy) / (mag * mag);
    
    var ix = lineStart.x + u * dx;
    var iy = lineStart.y + u * dy;
    
    return Math.sqrt((point.x - ix) * (point.x - ix) + (point.y - iy) * (point.y - iy));
}

// Compute the job envelope from collected toolpath points
var computeJobEnvelope = function() {
    if (jobToolpathPoints.length < 3) {
        jobEnvelopePoints = [];
        return;
    }
    
    // Compute convex hull
    var hull = computeConvexHull(jobToolpathPoints);
    
    // Simplify to at most 100 points
    jobEnvelopePoints = simplifyPolyline(hull, 100);
}

// Project the 3D toolpath onto the 2D Canvas
// The coefficients determine the type of projection
// Matrix multiplication written out
var xx = 0.707;
var xy = 0.707;
var xz = 0.0;
var yx = -0.707/2;
var yy = 0.707/2;
var yz = 1.0;
var isoView = function() {
    xx = 0.707;
    xy = 0.707;
    xz = 0.0;
    yx = -0.707;
    yy = 0.707;
    yz = 1.0;
}
var obliqueView = function() {
    xx = 0.707;
    xy = 0.707;
    xz = 0.0;
    yx = -0.707/2;
    yy = 0.707/2;
    yz = 1.0;
}
var topView = function() {
    xx = 1.0;
    xy = 0.0;
    xz = 0.0;
    yx = 0.0;
    yy = 1.0;
    yz = 0.0;
}
const projection = (wpos) => ({ x: wpos.x * xx + wpos.y * xy + wpos.z * xz, y: wpos.x * yx + wpos.y * yy + wpos.z * yz });

var formatLimit = function(mm) {
    return (tpUnits == 'G20') ? (mm/25.4).toFixed(3)+'"' : mm.toFixed(2)+'mm';
}

var toolX = null;
var toolY = null;
var toolSave = null;
var toolRadius = 6;
var toolRectWH = toolRadius*2 + 4;  // Slop to encompass the entire image area

var drawTool = function(dpos) {
    pp = projection(dpos)
    toolX = xToPixel(pp.x)-toolRadius-2;
    toolY = yToPixel(pp.y)-toolRadius-2;
    
    // Validate coordinates before calling getImageData to prevent canvas errors
    if (!isFinite(toolX) || !isFinite(toolY)) {
        return; // Skip drawing if coordinates are invalid
    }
    
    toolSave = tp.getImageData(toolX, toolY, toolRectWH, toolRectWH);

    tp.beginPath();
    tp.strokeStyle = 'magenta';
    tp.fillStyle = 'magenta';
    tp.arc(pp.x, pp.y, toolRadius/scaler, 0, Math.PI*2, true);
    tp.fill();
    tp.stroke();
}

var drawOrigin = function(radius) {
    po = projection({x: 0.0, y:0.0, z:0.0})
    tp.beginPath();
    tp.strokeStyle = 'red';
    tp.arc(po.x, po.y, radius, 0, Math.PI*2, false);
    tp.moveTo(-radius*1.5, 0);
    tp.lineTo(radius*1.5, 0);
    tp.moveTo(0,-radius*1.5);
    tp.lineTo(0, radius*1.5);
    tp.stroke();
}

var drawJobBoundingBox = function() {
    if (!bboxIsSet || !jobBboxExists()) {
        return;
    }
    
    // Get the job bounding box
    var bbox = getJobBoundingBox();
    if (!bbox) {
        return;
    }
    
    // Use envelope points if available (shaped boundary), otherwise fall back to rectangle
    if (jobEnvelopePoints.length > 0) {
        // Draw the shaped envelope
        tp.beginPath();
        tp.strokeStyle = 'blue';
        tp.lineWidth = 2.0 / scaler;
        
        // Project and draw the first point
        var firstPoint = projection({x: jobEnvelopePoints[0].x, y: jobEnvelopePoints[0].y, z: bbox.min.z});
        tp.moveTo(firstPoint.x, firstPoint.y);
        
        // Draw lines to all other envelope points
        for (var i = 1; i < jobEnvelopePoints.length; i++) {
            var p = projection({x: jobEnvelopePoints[i].x, y: jobEnvelopePoints[i].y, z: bbox.min.z});
            tp.lineTo(p.x, p.y);
        }
        
        // Close the path
        tp.lineTo(firstPoint.x, firstPoint.y);
        tp.stroke();
    } else {
        // Fallback to simple rectangle
        // Project the corners of the job bounding box
        const p0 = projection({x: bbox.min.x, y: bbox.min.y, z: bbox.min.z});
        const p1 = projection({x: bbox.max.x, y: bbox.min.y, z: bbox.min.z});
        const p2 = projection({x: bbox.max.x, y: bbox.max.y, z: bbox.min.z});
        const p3 = projection({x: bbox.min.x, y: bbox.max.y, z: bbox.min.z});
        
        // Draw the bounding box rectangle
        tp.beginPath();
        tp.strokeStyle = 'blue';
        tp.lineWidth = 2.0 / scaler;
        tp.moveTo(p0.x, p0.y);
        tp.lineTo(p1.x, p1.y);
        tp.lineTo(p2.x, p2.y);
        tp.lineTo(p3.x, p3.y);
        tp.lineTo(p0.x, p0.y);
        tp.stroke();
    }
    
    // Restore line width
    tp.lineWidth = 0.5 / scaler;
}

var drawMachineBounds = function() {
    
    //Work codinates offset the maxTravel part centers it in the view so 0,0 is the middle of the sheet
    var woodWidth = 2438;
    var woodHeight = 2438/2;

    //Project onto the camera view
    const p0 = projection({x: -woodWidth/2, y: -woodHeight/2, z: 0});
    const p1 = projection({x: woodWidth/2, y: -woodHeight/2, z: 0});
    const p2 = projection({x: woodWidth/2, y: woodHeight/2, z: 0});
    const p3 = projection({x: -woodWidth/2, y: woodHeight/2, z: 0});

    //This is used to fit everything in the camera view later
    tpBbox.min.x = Math.min(tpBbox.min.x, p0.x);
    tpBbox.min.y = Math.min(tpBbox.min.y, p0.y);
    tpBbox.max.x = Math.max(tpBbox.max.x, p2.x);
    tpBbox.max.y = Math.max(tpBbox.max.y, p2.y);
    bboxIsSet = true;

    //Draw to the actual display
    tp.beginPath();
    tp.moveTo(p0.x, p0.y);
    tp.lineTo(p0.x, p0.y);
    tp.lineTo(p1.x, p1.y);
    tp.lineTo(p2.x, p2.y);
    tp.lineTo(p3.x, p3.y);
    tp.lineTo(p0.x, p0.y);
    tp.strokeStyle = "green";
    tp.stroke();

}

var drawMachineBelts = function() {
    console.log("Draw belts");

    // Update anchor points from current configuration
    updateAnchorPointsFromConfig();

    const tl = projection({x: tlX - trX/2, y: tlY/2, z: 0});
    const tr = projection({x: trX/2, y: trY/2, z: 0});
    const bl = projection({x: blX - brX/2, y: blY - tlY/2, z: 0});
    const br = projection({x: brX/2, y: brY - trY/2, z: 0});

    tpBbox.min.x = Math.min(tpBbox.min.x, bl.x);
    tpBbox.min.y = Math.min(tpBbox.min.y, bl.y);
    tpBbox.max.x = Math.max(tpBbox.max.x, tr.x);
    tpBbox.max.y = Math.max(tpBbox.max.y, tr.y);

    tp.beginPath();
    tp.strokeStyle = "grey";
    tp.moveTo(0, 0);
    tp.lineTo(tl.x, tl.y);
    tp.moveTo(0, 0);
    tp.lineTo(tr.x, tr.y);
    tp.moveTo(0, 0);
    tp.lineTo(bl.x, bl.y);
    tp.moveTo(0, 0);
    tp.lineTo(br.x, br.y);
    tp.stroke();

    tp.fillStyle = "black";
    tp.beginPath();
    tp.arc(tl.x, tl.y, 10, 0, 2 * Math.PI);
    tp.closePath();
    tp.fill();
    tp.beginPath();
    tp.arc(tr.x, tr.y, 10, 0, 2 * Math.PI);
    tp.closePath();
    tp.fill();
    tp.beginPath();
    tp.arc(br.x, br.y, 10, 0, 2 * Math.PI);
    tp.closePath();
    tp.fill();
    tp.beginPath();
    tp.arc(bl.x, bl.y, 10, 0, 2 * Math.PI);
    tp.closePath();
    tp.fill();
    

    const squareSize = projection({x: 50, y: 0, z: 0});


    var i = bl.x;
    var j = bl.y;
    while(i < tr.x){
        while(j < tr.y){
            drawARect(i,j,squareSize.x, computPositonGradient(i, j, tl, tr, bl, br));
            j = j + squareSize.x;
        }
        j = bl.y;
        i = i + squareSize.x;
    }
}

var checkMinBeltLength = function(x1, y1, x2, y2){
    const dist = Math.sqrt((x1-x2)*(x1-x2) + (y1-y2)*(y1-y2));
    if(dist < 1200){
        return 1 - dist/1200;
    }
    else{
        return 0;
    }
}

var computPositonGradient = function(x,y, tl, tr, bl, br){
    var opacity = 0;
    
    //Check distance from the mounting points
    opacity = opacity + checkMinBeltLength(x,y,tl.x, tl.y);
    opacity = opacity + checkMinBeltLength(x,y,tr.x, tr.y);
    opacity = opacity + checkMinBeltLength(x,y,bl.x, bl.y);
    opacity = opacity + checkMinBeltLength(x,y,br.x, br.y);

    opacity = Math.max(opacity, computeTension(x,y, tl, tr, bl, br));

    return opacity;
}

var computeTension = function(x,y, tl, tr, bl, br){
    const A = Math.atan((y-tl.y)/(tr.x - x));
    const B = Math.atan((y-tl.y)/(x-tl.x));

    const T1 = 1 / (Math.cos(A) * Math.sin(B) / Math.cos(B) + Math.sin(A));
    const T2 = 1 / (Math.cos(B) * Math.sin(A) / Math.cos(A) + Math.sin(B));

    const T1Scaled = T1/-3;
    const T2Scaled = T2/-3; //This is some arbitrary scaling to make it look right in terms of color

    const max = Math.max(T1Scaled, T2Scaled);

    if(max > .15){
        return max;
    }
    else{
        return 0;
    }
}

// License: MIT - https://opensource.org/licenses/MIT
// Author: Michele Locati <michele@locati.it>
// Source: https://gist.github.com/mlocati/7210513
function perc2color(perc) {
    console.log(perc);
    var r, g, b = 0;
    if(perc < 50) {
        r = 255;
        g = Math.round(5.1 * perc);
    }
    else {
        g = 255;
        r = Math.round(510 - 5.10 * perc);
    }
    var h = r * 0x10000 + g * 0x100 + b * 0x1;

    console.log(r + " " + g + " " + b)
    return "rgba("+r+", "+g+", "+b+", .3)";//'#' + ('000000' + h.toString(16)).slice(-6);
}

var drawARect = function(x,y,size, opacity){

    const posP = projection({x: x - size/2, y: y - size/2, z: 0});
    tp.beginPath();
    tp.fillStyle = perc2color(100 - 100*opacity);//"rgba(255, 0, 0, " + opacity + ")";
    tp.rect(posP.x, posP.y, size, size);
    tp.fill();
}

var xOffset = 0;
var yOffset = 0;
var scaler = 1;
var xToPixel = function(x) { return scaler * x + xOffset; }
var yToPixel = function(y) { return -scaler * y + yOffset; }
var pixelToX = function(px) { return (px - xOffset) / scaler; }
var pixelToY = function(py) { return (py - yOffset) / -scaler; }

var clearCanvas = function() {
    // Reset the transform and clear the canvas
    tp.setTransform(1,0,0,1,0,0);

//    if (tpRect == undefined) {
        var tpRect = canvas.parentNode.getBoundingClientRect();
        // canvas.width = tpRect.width ? tpRect.width : 400;
        // canvas.height = tpRect.height ? tpRect.height : 400;
//    }

    tp.fillStyle = "white";
    tp.fillRect(0, 0, canvas.width, canvas.height);
}

var transformCanvas = function() {
    toolSave = null;

    clearCanvas();

    var imageWidth;
    var imageHeight;
    var inset;
    if (!bboxIsSet) {
        // imageWidth = canvas.width;
        // imageHeight = canvas.height;
        inset = 0;
        scaler = 1;
        xOffset = 0;
        yOffset = 0;
        return;
    }

    var imageWidth = tpBbox.max.x - tpBbox.min.x;
    var imageHeight = tpBbox.max.y - tpBbox.min.y;
    if (imageWidth == 0) {
        imageWidth = 1;
    }
    if (imageHeight == 0) {
        imageHeight = 1;
    }
    var shrink = 0.90;
    inset = 5;
    var scaleX = (canvas.width - inset*2) / imageWidth;
    var scaleY = (canvas.height - inset*2) / imageHeight;
    var minScale = Math.min(scaleX, scaleY);

    scaler = minScale * shrink;
    if (scaler < 0) {
        scaler = -scaler;
    }
    xOffset = inset - tpBbox.min.x * scaler;
    yOffset = (canvas.height-inset) - tpBbox.min.y * (-scaler);

    // Canvas coordinates of image bounding box top and right
    var imageTop = scaler * imageHeight;
    var imageRight = scaler * imageWidth;

    // Show the X and Y limit coordinates of the GCode program.
    // We do this before scaling because after we invert the Y coordinate,
    // text would be displayed upside-down.
    // tp.fillStyle = "black";
    // tp.font = "14px Ariel";
    // tp.textAlign = "center";
    // tp.textBaseline = "bottom";
    // tp.fillText(formatLimit(tpBbox.min.y), imageRight/2, canvas.height-inset);
    // tp.textBaseline = "top";
    // tp.fillText(formatLimit(tpBbox.max.y), imageRight/2, canvas.height-inset - imageTop);
    // tp.textAlign = "left";
    // tp.textBaseline = "center";
    // tp.fillText(formatLimit(tpBbox.min.x), inset, canvas.height-inset - imageTop/2);
    // tp.textAlign = "right";
    // tp.textBaseline = "center";
    // tp.fillText(formatLimit(tpBbox.max.x), inset+imageRight, canvas.height-inset - imageTop/2);
    // Transform the path coordinate system so the image fills the canvas
    // with a small inset, and +Y goes upward.
    // The net transform from image space (x,y) to pixel space (x',y') is:
    //   x' =  scaler*x + xOffset
    //   y' = -scaler*y + yOffset
    // We use setTransform() instead of a sequence of scale() and translate() calls
    // because we need to perform the transform manually for getImageData(), which
    // uses pixel coordinates, and there is no standard way to read back the current
    // transform matrix.

    tp.setTransform(scaler, 0, 0, -scaler, xOffset, yOffset);

    tp.lineWidth = 0.5 / scaler;

    drawOrigin(imageWidth * 0.04);
}
var wrappedDegrees = function(radians) {
    var degrees = radians * 180 / Math.PI;
    return degrees >= 0 ? degrees : degrees + 360;
}

var bboxHandlers = {
    addLine: function(modal, start, end) {
	// Update tpUnits in case it changed in a previous line
        tpUnits = modal.units;

        ps = projection(start);
        pe = projection(end);

        // Update overall bounding box for display (includes all moves for proper canvas scaling)
        tpBbox.min.x = Math.min(tpBbox.min.x, ps.x, pe.x);
        tpBbox.min.y = Math.min(tpBbox.min.y, ps.y, pe.y);
        tpBbox.min.z = Math.min(tpBbox.min.z, start.z, end.z);
        tpBbox.max.x = Math.max(tpBbox.max.x, ps.x, pe.x);
        tpBbox.max.y = Math.max(tpBbox.max.y, ps.y, pe.y);
        tpBbox.max.z = Math.max(tpBbox.max.z, start.z, end.z);
        bboxIsSet = true;
        
        // Update job bounding box in world coordinates
        // Exclude G0 rapid moves and skip initial positioning moves
        // Only start tracking once actual cutting (non-G0 XY movement) begins
        if (modal.motion !== 'G0') {
            // Check if this is actual XY cutting movement (not just Z or feed rate change)
            var hasXYMovement = (start.x !== end.x || start.y !== end.y);
            
            if (hasXYMovement) {
                // Once we have actual XY cutting movement, we're no longer in initial moves
                initialMovesForBbox = false;
            }
            
            // Only add to job bounds once we've started actual cutting
            if (!initialMovesForBbox) {
                jobBbox.min.x = Math.min(jobBbox.min.x, end.x);
                jobBbox.min.y = Math.min(jobBbox.min.y, end.y);
                jobBbox.min.z = Math.min(jobBbox.min.z, end.z);
                jobBbox.max.x = Math.max(jobBbox.max.x, end.x);
                jobBbox.max.y = Math.max(jobBbox.max.y, end.y);
                jobBbox.max.z = Math.max(jobBbox.max.z, end.z);
                jobBboxIsSet = true;
                
                // Collect end point for envelope calculation
                jobToolpathPoints.push({x: end.x, y: end.y});
            }
        }
    },
    addArcCurve: function(modal, start, end, center, extraRotations) {
        // To determine the precise bounding box of a circular arc we
	// must account for the possibility that the arc crosses one or
	// more axes.  If so, the bounding box includes the "bulges" of
	// the arc across those axes.

	// Update units in case it changed in a previous line
        tpUnits = modal.units;

        ps = projection(start);
        pc = projection(center);
        pe = projection(end);

	// Coordinates relative to the center of the arc (PROJECTED coordinates for display)
	var sx = ps.x - pc.x;
	var sy = ps.y - pc.y;
	var ex = pe.x - pc.x;
	var ey = pe.y - pc.y;

        var radius = Math.hypot(sx, sy);
        
        // Also calculate in WORLD coordinates for job bounding box
        var world_sx = start.x - center.x;
        var world_sy = start.y - center.y;
        var world_ex = end.x - center.x;
        var world_ey = end.y - center.y;
        var world_radius = Math.hypot(world_sx, world_sy);

	// Axis crossings - plus and minus x and y
	var px = false;
	var py = false;
	var mx = false;
	var my = false;
	
	// World coordinate axis crossings for job bounding box
	var world_px = false;
	var world_py = false;
	var world_mx = false;
	var world_my = false;

	// Note: We use one atan2() call to detect full circles (matching firmware behavior),
	// but the rest of the decision tree avoids transcendental functions for efficiency.
	// Every path through the axis crossing logic is 4 or 5 simple comparisons.
	
	// Check for full circle or multi-rotation arcs using same logic as FluidNC firmware
	// See firmware/FluidNC/src/MotionControl.cpp lines 142-160 and Config.h line 179
	// ARC_ANGULAR_TRAVEL_EPSILON = 5E-7 radians
	var ARC_ANGULAR_TRAVEL_EPSILON = 5e-7;
	
	// Calculate angular travel (CCW angle between start and end from center)
	// Same calculation as firmware: atan2(r_axis0 * rt_axis1 - r_axis1 * rt_axis0, r_axis0 * rt_axis0 + r_axis1 * rt_axis1)
	var angular_travel = Math.atan2(sx * ey - sy * ex, sx * ex + sy * ey);
	
	// Check if angular travel is near zero (full circle) using firmware's epsilon
	var isFullCircle = (extraRotations >= 1) || 
	                   (Math.abs(angular_travel) <= ARC_ANGULAR_TRAVEL_EPSILON);
	
	// Calculate axis crossings for PROJECTED coordinates (for display)
	if (ey >= 0) {              // End in upper half plane
	    if (ex > 0) {             // End in quadrant 0 - X+ Y+
		if (sy >= 0) {          // Start in upper half plane
		    if (sx > 0) {         // Start in quadrant 0 - X+ Y+
			if (isFullCircle && sx <= ex) {     // wraparound
			    px = py = mx = my = true;
			}
		    } else {              // Start in quadrant 1 - X- Y+
			mx = my = px = true;
		    }
		} else {                // Start in lower half plane
		    if (sx > 0) {         // Start in quadrant 3 - X+ Y-
			px = true;
		    } else {              // Start in quadrant 2 - X- Y-
			my = px = true;
		    }
		}
	    } else {                  // End in quadrant 1 - X- Y+
		if (sy >= 0) {          // Start in upper half plane
		    if (sx > 0) {         // Start in quadrant 0 - X+ Y+
			py = true;
		    } else {              // Start in quadrant 1 - X- Y+
			if (isFullCircle && sx <= ex) {     // wraparound
			    px = py = mx = my = true;
			}
		    }
		} else {                // Start in lower half plane
		    if (sx > 0) {         // Start in quadrant 3 - X+ Y-
			px = py = true;
		    } else {              // Start in quadrant 2 - X- Y-
			my = px = py = true;
		    }
		}
	    }
	} else {                    // ey < 0 - end in lower half plane
	    if (ex > 0) {             // End in quadrant 3 - X+ Y+
		if (sy >= 0) {          // Start in upper half plane
		    if (sx > 0) {         // Start in quadrant 0 - X+ Y+
			py = mx = my = true;
		    } else {              // Start in quadrant 1 - X- Y+
			mx = my = true;
		    }
		} else {                // Start in lower half plane
		    if (sx > 0) {         // Start in quadrant 3 - X+ Y-
			if (isFullCircle && sx >= ex) {      // wraparound
			    px = py = mx = my = true;
			}
		    } else {              // Start in quadrant 2 - X- Y-
			my = true;
		    }
		}
	    } else {                  // End in quadrant 2 - X- Y+
		if (sy >= 0) {          // Start in upper half plane
		    if (sx > 0) {         // Start in quadrant 0 - X+ Y+
			py = mx = true;
		    } else {              // Start in quadrant 1 - X- Y+
			mx = true;
		    }
		} else {                // Start in lower half plane
		    if (sx > 0) {         // Start in quadrant 3 - X+ Y-
			px = py = mx = true;
		    } else {              // Start in quadrant 2 - X- Y-
			if (isFullCircle && sx >= ex) {      // wraparound
			    px = py = mx = my = true;
			}
		    }
		}
	    }
	}
	var maxX = px ? pc.x + radius : Math.max(ps.x, pe.x);
	var maxY = py ? pc.y + radius : Math.max(ps.y, pe.y);
	var minX = mx ? pc.x - radius : Math.min(ps.x, pe.x);
	var minY = my ? pc.y - radius : Math.min(ps.y, pe.y);
	
	// Check for full circle or multi-rotation arcs in world coordinates
	// Use same logic as firmware (see above for projected coordinates)
	var world_angular_travel = Math.atan2(world_sx * world_ey - world_sy * world_ex, world_sx * world_ex + world_sy * world_ey);
	var world_isFullCircle = (extraRotations >= 1) || 
	                         (Math.abs(world_angular_travel) <= ARC_ANGULAR_TRAVEL_EPSILON);
	
	// Calculate axis crossings for WORLD coordinates (for job bounding box)
	if (world_ey >= 0) {              // End in upper half plane
	    if (world_ex > 0) {             // End in quadrant 0 - X+ Y+
		if (world_sy >= 0) {          // Start in upper half plane
		    if (world_sx > 0) {         // Start in quadrant 0 - X+ Y+
			if (world_isFullCircle && world_sx <= world_ex) {     // wraparound
			    world_px = world_py = world_mx = world_my = true;
			}
		    } else {              // Start in quadrant 1 - X- Y+
			world_mx = world_my = world_px = true;
		    }
		} else {                // Start in lower half plane
		    if (world_sx > 0) {         // Start in quadrant 3 - X+ Y-
			world_px = true;
		    } else {              // Start in quadrant 2 - X- Y-
			world_my = world_px = true;
		    }
		}
	    } else {                  // End in quadrant 1 - X- Y+
		if (world_sy >= 0) {          // Start in upper half plane
		    if (world_sx > 0) {         // Start in quadrant 0 - X+ Y+
			world_py = true;
		    } else {              // Start in quadrant 1 - X- Y+
			if (world_isFullCircle && world_sx <= world_ex) {     // wraparound
			    world_px = world_py = world_mx = world_my = true;
			}
		    }
		} else {                // Start in lower half plane
		    if (world_sx > 0) {         // Start in quadrant 3 - X+ Y-
			world_px = world_py = true;
		    } else {              // Start in quadrant 2 - X- Y-
			world_my = world_px = world_py = true;
		    }
		}
	    }
	} else {                    // world_ey < 0 - end in lower half plane
	    if (world_ex > 0) {             // End in quadrant 3 - X+ Y+
		if (world_sy >= 0) {          // Start in upper half plane
		    if (world_sx > 0) {         // Start in quadrant 0 - X+ Y+
			world_py = world_mx = world_my = true;
		    } else {              // Start in quadrant 1 - X- Y+
			world_mx = world_my = true;
		    }
		} else {                // Start in lower half plane
		    if (world_sx > 0) {         // Start in quadrant 3 - X+ Y-
			if (world_isFullCircle && world_sx >= world_ex) {      // wraparound
			    world_px = world_py = world_mx = world_my = true;
			}
		    } else {              // Start in quadrant 2 - X- Y-
			world_my = true;
		    }
		}
	    } else {                  // End in quadrant 2 - X- Y+
		if (world_sy >= 0) {          // Start in upper half plane
		    if (world_sx > 0) {         // Start in quadrant 0 - X+ Y+
			world_py = world_mx = true;
		    } else {              // Start in quadrant 1 - X- Y+
			world_mx = true;
		    }
		} else {                // Start in lower half plane
		    if (world_sx > 0) {         // Start in quadrant 3 - X+ Y-
			world_px = world_py = world_mx = true;
		    } else {              // Start in quadrant 2 - X- Y-
			if (world_isFullCircle && world_sx >= world_ex) {      // wraparound
			    world_px = world_py = world_mx = world_my = true;
			}
		    }
		}
	    }
	}
	
	// Now calculate world coordinate bounding box for job bounds
	// Use both start and end positions when axis is not crossed
	var world_maxX = world_px ? center.x + world_radius : Math.max(start.x, end.x);
	var world_maxY = world_py ? center.y + world_radius : Math.max(start.y, end.y);
	var world_minX = world_mx ? center.x - world_radius : Math.min(start.x, end.x);
	var world_minY = world_my ? center.y - world_radius : Math.min(start.y, end.y);

	var minZ = Math.min(start.z, end.z);
	var maxZ = Math.max(start.z, end.z);

        const p0 = projection({x: minX, y: minY, z: minZ});
        const p1 = projection({x: minX, y: maxY, z: minZ});
        const p2 = projection({x: maxX, y: maxY, z: minZ});
        const p3 = projection({x: maxX, y: minY, z: minZ});
        const p4 = projection({x: minX, y: minY, z: maxZ});
        const p5 = projection({x: minX, y: maxY, z: maxZ});
        const p6 = projection({x: maxX, y: maxY, z: maxZ});
        const p7 = projection({x: maxX, y: minY, z: maxZ});

	tpBbox.min.x = Math.min(tpBbox.min.x, p0.x, p1.x, p2.x, p3.x, p4.x, p5.x, p6.x, p7.x);
	tpBbox.min.y = Math.min(tpBbox.min.y, p0.y, p1.y, p2.y, p3.y, p4.y, p5.y, p6.y, p7.y);
	tpBbox.min.z = Math.min(tpBbox.min.z, minZ);
	tpBbox.max.x = Math.max(tpBbox.max.x, p0.x, p1.x, p2.x, p3.x, p4.x, p5.x, p6.x, p7.x);
	tpBbox.max.y = Math.max(tpBbox.max.y, p0.y, p1.y, p2.y, p3.y, p4.y, p5.y, p6.y, p7.y);
	tpBbox.max.z = Math.max(tpBbox.max.z, maxZ);
        bboxIsSet = true;
        
        // Arc moves (G2/G3) are always cutting moves
        // Check if this is the first cutting move - if so, we need to be careful about the start position
        var wasInitialMoves = initialMovesForBbox;
        initialMovesForBbox = false;
        
        // Update job bounding box in world coordinates for arc
        // Only add bounds if we were already past initial moves (to exclude arc starting from rapid position)
        if (!wasInitialMoves) {
            jobBbox.min.x = Math.min(jobBbox.min.x, world_minX);
            jobBbox.min.y = Math.min(jobBbox.min.y, world_minY);
            jobBbox.min.z = Math.min(jobBbox.min.z, minZ);
            jobBbox.max.x = Math.max(jobBbox.max.x, world_maxX);
            jobBbox.max.y = Math.max(jobBbox.max.y, world_maxY);
            jobBbox.max.z = Math.max(jobBbox.max.z, maxZ);
            jobBboxIsSet = true;
        } else {
            // For the first arc, only include the end point (not the start from rapid)
            jobBbox.min.x = Math.min(jobBbox.min.x, end.x);
            jobBbox.min.y = Math.min(jobBbox.min.y, end.y);
            jobBbox.min.z = Math.min(jobBbox.min.z, end.z);
            jobBbox.max.x = Math.max(jobBbox.max.x, end.x);
            jobBbox.max.y = Math.max(jobBbox.max.y, end.y);
            jobBbox.max.z = Math.max(jobBbox.max.z, end.z);
            jobBboxIsSet = true;
        }
        
        // Collect arc points for envelope calculation (skip start point - it may be from rapid move)
        if (modal.motion !== 'G0') {
            // Sample points along the arc (excluding start point)
            var deltaX1 = start.x - center.x;
            var deltaY1 = start.y - center.y;
            var radius = Math.hypot(deltaX1, deltaY1);
            var deltaX2 = end.x - center.x;
            var deltaY2 = end.y - center.y;
            var theta1 = Math.atan2(deltaY1, deltaX1);
            var theta2 = Math.atan2(deltaY2, deltaX2);
            var cw = modal.motion === "G2";
        
        if (!cw && theta2 < theta1) {
            theta2 += Math.PI * 2;
        } else if (cw && theta2 > theta1) {
            theta2 -= Math.PI * 2;
        }
            
            // Sample arc with enough points to capture its shape (start at i=1 to skip start point)
            var deltaTheta = theta2 - theta1;
            var numSamples = Math.max(5, Math.ceil(Math.abs(deltaTheta) / (Math.PI / 8))); // At least 5 samples
            var dt = deltaTheta / numSamples;
            
            for (var i = 1; i <= numSamples; i++) {
                var theta = theta1 + i * dt;
                var px = center.x + radius * Math.cos(theta);
                var py = center.y + radius * Math.sin(theta);
                jobToolpathPoints.push({x: px, y: py});
            }
        }
    }
};
var initialMoves = true;
var displayHandlers = {
    addLine: function(modal, start, end) {
        var motion = modal.motion;
        if (motion == 'G0') {
            tp.strokeStyle = initialMoves ? 'red' : 'green';
        } else {
            tp.strokeStyle = 'black';
            // Don't cancel initialMoves on no-motion G1 (e.g. G1 F30)
            // or on Z-only moves
            if (start.x != end.x || start.y != end.y) {
                initialMoves = false;
            }
        }

        ps = projection(start);
        pe = projection(end);
        tp.beginPath();
        // tp.moveTo(start.x, start.y);
        // tp.lineTo(end.x, end.y);
        tp.moveTo(ps.x, ps.y);
        tp.lineTo(pe.x, pe.y);
        tp.stroke();
    },
    addArcCurve: function(modal, start, end, center, extraRotations) {
        var motion = modal.motion;

        var deltaX1 = start.x - center.x;
        var deltaY1 = start.y - center.y;
        var radius = Math.hypot(deltaX1, deltaY1);
        var deltaX2 = end.x - center.x;
        var deltaY2 = end.y - center.y;
        var theta1 = Math.atan2(deltaY1, deltaX1);
        var theta2 = Math.atan2(deltaY2, deltaX2);
        var cw = modal.motion == "G2";
        if (!cw && theta2 < theta1) {
            theta2 += Math.PI * 2;
        } else if (cw && theta2 > theta1) {
            theta2 -= Math.PI * 2;
        }
	if (theta1 == theta2) {
	    theta2 += Math.PI * ((cw) ? -2 : 2);
	}
        if (extraRotations > 1) {
            theta2 += (extraRotations-1) * Math.PI * ((cw) ? -2 : 2);;
        }

        initialMoves = false;

        tp.beginPath();
        tp.strokeStyle = 'black';
        deltaTheta = theta2 - theta1;
        n = 10 * Math.ceil(Math.abs(deltaTheta) / Math.PI);
        dt = (deltaTheta) / n;
        dz = (end.z - start.z) / n;
        ps = projection(start);
        tp.moveTo(ps.x, ps.y);
        next = {};
        theta = theta1;
        next.z = start.z;
        for (i = 0; i < n; i++) {
            theta += dt;
            next.x = center.x + radius * Math.cos(theta);
            next.y = center.y + radius * Math.sin(theta);
            next.z += dz;
            pe = projection(next)
            tp.lineTo(pe.x, pe.y);
        }
        tp.stroke();
    },
};

var ToolpathDisplayer = function() {
};

// var offset;

ToolpathDisplayer.prototype.clear = function() {
    clearCanvas();
}

ToolpathDisplayer.prototype.showToolpath = function(gcode, modal, initialPosition) {
    // Update anchor points from current configuration before displaying
    updateAnchorPointsFromConfig();
    
    cameraAngle = cameraAngle;

    var drawBounds = false;
    var drawBelts  = false;

    switch (cameraAngle) {
      case 0:
        obliqueView();
        break;
      case 1:
        obliqueView();
        drawBounds = true;
        break;
      case 2:
        topView();
        break;
      case 3:
        topView();
        drawBounds = true;
        break;
      case 4:
        topView();
        drawBounds = true;
        drawBelts  = true;
        break;
      default:
        obliqueView();
    }

    resetBbox();
    bboxHandlers.position = initialPosition;
    bboxHandlers.modal = modal;

    if(drawBounds){
        drawMachineBounds(); //Adds the machine bounds to the bounding box...this does not draw
    }
    if(drawBelts){
        drawMachineBelts(); //Adds the belts to the bounding box...does not draw yet
    }

    var gcodeLines = gcode.split('\n');
    new Toolpath(bboxHandlers).loadFromLinesSync(gcodeLines);
    
    // Compute the envelope from collected toolpath points
    computeJobEnvelope();
    
    transformCanvas();
    if (!bboxIsSet) {
        return;
    }
    initialMoves = true;
    displayHandlers.position = initialPosition;
    displayHandlers.modal = modal;
    new Toolpath(displayHandlers).loadFromLinesSync(gcodeLines);

    drawTool(initialPosition);

    // Draw job bounding box if available
    drawJobBoundingBox();

    if(drawBounds){
        drawMachineBounds(); //Actually draws the bounding box
    }
    if(drawBelts){
        drawMachineBelts(); //Actually draws the belts
    }
};

ToolpathDisplayer.prototype.reDrawTool = function(modal, dpos) {
    if (toolSave != null) {
        tp.putImageData(toolSave, toolX, toolY);
        drawTool(dpos);
    } else {
        // If no toolSave exists (no GCode loaded), initialize canvas and draw tool position
        this.showToolPosition(modal, dpos);
    }
}

ToolpathDisplayer.prototype.showToolPosition = function(modal, position) {
    // Update anchor points from current configuration
    updateAnchorPointsFromConfig();
    
    // Set up camera view based on current angle
    var drawBounds = false;
    var drawBelts  = false;

    switch (cameraAngle) {
      case 0:
        obliqueView();
        break;
      case 1:
        obliqueView();
        drawBounds = true;
        break;
      case 2:
        topView();
        break;
      case 3:
        topView();
        drawBounds = true;
        break;
      case 4:
        topView();
        drawBounds = true;
        drawBelts  = true;
        break;
      default:
        obliqueView();
    }

    // Initialize bounding box with machine bounds
    resetBbox();
    
    // Always draw machine bounds to establish a coordinate system
    drawMachineBounds();
    if(drawBelts){
        drawMachineBelts();
    }
    
    // Transform canvas to fit the bounds
    transformCanvas();
    
    // Only draw if we have a valid bounding box
    if (bboxIsSet) {
        // Draw visible elements based on camera angle
        if(drawBounds){
            drawMachineBounds();
        }
        if(drawBelts){
            drawMachineBelts();
        }
        
        // Draw the tool at current position
        drawTool(position);
    }
}

ToolpathDisplayer.prototype.cycleCameraAngle = function(gcode, modal, position) {
    cameraAngle = cameraAngle + 1;
    if(cameraAngle > 4){
        cameraAngle = 0;
    }

    tpDisplayer().showToolpath(gcode, modal, position);
}

let displayer = new ToolpathDisplayer();

const tpDisplayer = () => {
	if (!displayer) {
		displayer = new ToolpathDisplayer();
	}
	return displayer;
}

/** Expects a simple array with 3 elements, and converts it to an xyz object */
const arrayToXYZ = (arr) => {
	// Provide safe defaults if array is invalid or contains non-finite values
	const safeArr = arr && Array.isArray(arr) ? arr : [0, 0, 0];
	return { 
		x: isFinite(safeArr[0]) ? safeArr[0] : 0, 
		y: isFinite(safeArr[1]) ? safeArr[1] : 0, 
		z: isFinite(safeArr[2]) ? safeArr[2] : 0 
	};
};

const updateGcodeViewerAngle = () => {
	const gcode = getValue("tablettab_gcode");
	tpDisplayer().cycleCameraAngle(gcode, gCodeModal, arrayToXYZ(WPOS));
};

// Left-click switches view angle
canvas.addEventListener("mouseup", function(event) {
    // Only switch view on left-click
    if (event.button === 0) {
        updateGcodeViewerAngle();
    }
});

// Create custom context menu element
var contextMenu = document.createElement('div');
contextMenu.id = 'canvas-context-menu';
contextMenu.style.cssText = 'position: fixed; background: white; border: 1px solid #ccc; box-shadow: 2px 2px 8px rgba(0,0,0,0.2); padding: 8px 12px; font-size: 14px; cursor: pointer; z-index: 10000; display: none; border-radius: 4px;';
document.body.appendChild(contextMenu);

// Hide context menu when clicking elsewhere
document.addEventListener('click', function() {
    contextMenu.style.display = 'none';
});

// Right-click handler for "move here" functionality
canvas.addEventListener("contextmenu", function(event) {
    // Only show context menu for top-down views (cameraAngle 2, 3, or 4)
    if (cameraAngle < 2) {
        return; // Allow default context menu for non-top-down views
    }
    
    // Check if we have a bounding box set
    if (!bboxIsSet) {
        return; // No GCode loaded
    }
    
    event.preventDefault(); // Prevent default browser context menu
    
    // Get canvas bounding rectangle to calculate relative position
    const rect = canvas.getBoundingClientRect();
    
    // Calculate click position in canvas coordinates
    // Canvas may be scaled/stretched to fit the display, so we need to convert properly
    const canvasX = (event.clientX - rect.left) * (canvas.width / rect.width);
    const canvasY = (event.clientY - rect.top) * (canvas.height / rect.height);
    
    // Use job bounding box if available, otherwise use display bounding box
    // For top view, try to get jobBbox first (world coordinates), fall back to tpBbox
    let worldBox;
    if (jobBboxExists()) {
        worldBox = getJobBoundingBox();
    } else if (bboxIsSet && cameraAngle >= 2 && cameraAngle <= 4) {
        // For top view, tpBbox contains world coordinates since projection is identity
        // But it may include machine bounds, so we need to be careful
        // For now, use tpBbox as a fallback
        worldBox = {
            min: { x: tpBbox.min.x, y: tpBbox.min.y, z: tpBbox.min.z },
            max: { x: tpBbox.max.x, y: tpBbox.max.y, z: tpBbox.max.z }
        };
    }
    
    if (!worldBox) {
        return;
    }
    
    // For top view, we need to map canvas pixels to world coordinates
    // Get the projected job bounding box (what's actually displayed)
    const boxP0 = projection({x: worldBox.min.x, y: worldBox.min.y, z: 0});
    const boxP1 = projection({x: worldBox.max.x, y: worldBox.max.y, z: 0});
    
    // Calculate the pixel coordinates of the bounding box corners
    const boxPixelMinX = xToPixel(boxP0.x);
    const boxPixelMinY = yToPixel(boxP1.y); // Note: Y is inverted
    const boxPixelMaxX = xToPixel(boxP1.x);
    const boxPixelMaxY = yToPixel(boxP0.y);
    
    // Calculate the relative position within the bounding box (0 to 1)
    const relX = (canvasX - boxPixelMinX) / (boxPixelMaxX - boxPixelMinX);
    const relY = (canvasY - boxPixelMinY) / (boxPixelMaxY - boxPixelMinY);
    
    // Map to world coordinates
    // Note: Y is inverted in canvas (top = max, bottom = min), so invert relY
    const worldX = worldBox.min.x + relX * (worldBox.max.x - worldBox.min.x);
    const worldY = worldBox.max.y - relY * (worldBox.max.y - worldBox.min.y);
    
    // Validate that coordinates are finite and within reasonable bounds
    if (!isFinite(worldX) || !isFinite(worldY)) {
        return;
    }
    
    // Show custom context menu
    contextMenu.textContent = `Move to: X${worldX.toFixed(2)}, Y${worldY.toFixed(2)}`;
    contextMenu.style.left = event.clientX + 'px';
    contextMenu.style.top = event.clientY + 'px';
    contextMenu.style.display = 'block';
    
    // Handle click on context menu
    contextMenu.onclick = function(e) {
        e.stopPropagation();
        contextMenu.style.display = 'none';
        if (typeof move === 'function') {
            move({ X: worldX, Y: worldY });
        }
    };
}); 
var refreshGcode = function() {
    const gcode = getValue("tablettab_gcode");
    tpDisplayer().showToolpath(gcode, gCodeModal, arrayToXYZ(WPOS));
    updateJobBoundsDisplay();
}

// Function to update the job bounds display
var updateJobBoundsDisplay = function() {
    const boundsInfo = document.getElementById("job-bounds-info");
    const boundsText = document.getElementById("job-bounds-text");
    const traceButton = document.getElementById("tablettab_trace_boundary");
    
    if (!boundsInfo || !boundsText || !traceButton) {
        return;
    }
    
    if (jobBboxExists()) {
        const bbox = getJobBoundingBox();
        const width = (bbox.max.x - bbox.min.x).toFixed(1);
        const height = (bbox.max.y - bbox.min.y).toFixed(1);
        const zRange = (bbox.max.z - bbox.min.z).toFixed(1);
        
        boundsText.innerHTML = `Size: ${width} × ${height} mm<br>Z: ${bbox.min.z.toFixed(1)} to ${bbox.max.z.toFixed(1)} mm (${zRange}mm range)`;
        boundsInfo.style.display = "block";
        traceButton.style.display = "block";
    } else {
        boundsText.innerHTML = "No file loaded";
        boundsInfo.style.display = "none";
        traceButton.style.display = "none";
    }
}

// Function to trace the job boundary
var traceBoundary = function() {
    if (!jobBboxExists()) {
        alert("No job loaded or no movement commands found in GCode");
        return;
    }
    
    const bbox = getJobBoundingBox();
    const currentPos = arrayToXYZ(WPOS);
    
    // Create the boundary tracing commands
    var commands = [`G90`]; // Absolute positioning
    
    // Use envelope points if available (shaped boundary), otherwise fall back to rectangle
    if (jobEnvelopePoints.length > 0) {
        // Trace the shaped envelope
        for (var i = 0; i < jobEnvelopePoints.length; i++) {
            commands.push(`G0 X${jobEnvelopePoints[i].x.toFixed(3)} Y${jobEnvelopePoints[i].y.toFixed(3)}`);
        }
        // Return to first point to close the shape
        commands.push(`G0 X${jobEnvelopePoints[0].x.toFixed(3)} Y${jobEnvelopePoints[0].y.toFixed(3)}`);
    } else {
        // Fallback to simple rectangle
        commands.push(`G0 X${bbox.min.x.toFixed(3)} Y${bbox.min.y.toFixed(3)}`); // Move to bottom-left corner
        commands.push(`G0 X${bbox.max.x.toFixed(3)} Y${bbox.min.y.toFixed(3)}`); // Move to bottom-right corner
        commands.push(`G0 X${bbox.max.x.toFixed(3)} Y${bbox.max.y.toFixed(3)}`); // Move to top-right corner
        commands.push(`G0 X${bbox.min.x.toFixed(3)} Y${bbox.max.y.toFixed(3)}`); // Move to top-left corner
        commands.push(`G0 X${bbox.min.x.toFixed(3)} Y${bbox.min.y.toFixed(3)}`); // Back to bottom-left corner
    }
    
    // Return to original position
    commands.push(`G0 X${currentPos.x.toFixed(3)} Y${currentPos.y.toFixed(3)}`);
    
    // Confirm before starting
    var pointCount = jobEnvelopePoints.length > 0 ? jobEnvelopePoints.length : 4;
    if (confirm(`Trace boundary? This will move the machine around the job perimeter using ${pointCount} points.\n\nBounds: ${bbox.min.x.toFixed(1)},${bbox.min.y.toFixed(1)} to ${bbox.max.x.toFixed(1)},${bbox.max.y.toFixed(1)}\n\nZ-axis will not move.`)) {
        // Send all commands as a single G-code string for smooth, continuous execution
        var gcode = commands.join('\n');
        SendPrinterCommand(gcode);
    }
}

// document.getElementById("small-toolpath").addEventListener("mouseup", updateGcodeViewerAngle); 
