(function (root, factory) {
  const api = factory(root);
  root.C2DConverter = api;
  if (typeof module !== "undefined" && module.exports) {
    module.exports = api;
    module.exports.default = api;
  }
})(typeof globalThis !== "undefined" ? globalThis : window, function (root) {
  const SQLITE_BASE_URL = "https://cdnjs.cloudflare.com/ajax/libs/sql.js/1.13.0/";
  const PAKO_URL = "https://cdnjs.cloudflare.com/ajax/libs/pako/2.1.0/pako.min.js";
  const CLIPPER_URL = "https://cdn.jsdelivr.net/npm/clipper-lib@6.4.2/clipper.js";
  const SQLITE_URL = `${SQLITE_BASE_URL}sql-wasm.js`;
  const CLIPPER_SCALE = 100000;
  const RAPID_RATE_MM_PER_MIN = 3000;
  const state = {
    dependencyPromises: new Map(),
    sqlModulePromise: null,
  };

  function isC2DFileName(fileName) {
    return /\.c2d$/i.test(fileName || "");
  }

  function getOutputFileName(fileName) {
    const baseName = (fileName || "input.c2d").replace(/\.[^.]+$/, "");
    return `${baseName}.gcode`;
  }

  function buildAcceptAttribute(existingAccept) {
    const values = new Set(
      String(existingAccept || "")
        .split(",")
        .map((value) => value.trim())
        .filter(Boolean)
    );
    values.add(".c2d");
    values.add(".C2D");
    return [...values].join(", ");
  }

  async function loadScript(url, globalName) {
    if (globalName && root[globalName]) {
      return root[globalName];
    }
    if (state.dependencyPromises.has(url)) {
      return state.dependencyPromises.get(url);
    }
    if (!root.document) {
      throw new Error(`Unable to load ${url} outside a browser context.`);
    }

    const promise = new Promise((resolve, reject) => {
      const existingScript = [...root.document.getElementsByTagName("script")].find((script) => script.src === url);
      if (existingScript) {
        existingScript.addEventListener("load", () => resolve(globalName ? root[globalName] : undefined), { once: true });
        existingScript.addEventListener("error", () => reject(new Error(`Failed to load ${url}`)), { once: true });
        return;
      }

      const script = root.document.createElement("script");
      script.src = url;
      script.async = true;
      script.onload = () => resolve(globalName ? root[globalName] : undefined);
      script.onerror = () => reject(new Error(`Failed to load ${url}`));
      root.document.head.appendChild(script);
    });

    state.dependencyPromises.set(url, promise);
    return promise;
  }

  async function ensureDependencies() {
    await Promise.all([
      root.pako ? Promise.resolve(root.pako) : loadScript(PAKO_URL, "pako"),
      root.ClipperLib ? Promise.resolve(root.ClipperLib) : loadScript(CLIPPER_URL, "ClipperLib"),
      root.initSqlJs ? Promise.resolve(root.initSqlJs) : loadScript(SQLITE_URL, "initSqlJs"),
    ]);

    if (!state.sqlModulePromise) {
      state.sqlModulePromise = root.initSqlJs({
        locateFile: (file) => `${SQLITE_BASE_URL}${file}`,
      });
    }

    return {
      sql: await state.sqlModulePromise,
      inflate: root.pako,
      clipperLib: root.ClipperLib,
    };
  }

  async function convertUploadFile(file) {
    if (!isC2DFileName(file?.name)) {
      return {
        converted: false,
        file,
        gcode: null,
        warnings: [],
      };
    }

    const bytes = new Uint8Array(await file.arrayBuffer());
    const result = await convertC2DBytes(bytes, file.name);
    return {
      ...result,
      converted: true,
      file: createTextFile(getOutputFileName(file.name), result.gcode, file.lastModified),
    };
  }

  async function convertC2DBytes(bytes, fileName) {
    const dependencies = await ensureDependencies();
    const parsed = parseC2DBytes(bytes, fileName, dependencies);
    const generated = generateGcodeFromParsed(parsed, fileName, dependencies);
    return {
      fileName,
      outputFileName: getOutputFileName(fileName),
      sourceType: parsed.sourceType,
      warnings: generated.warnings,
      stats: generated.stats,
      gcode: generated.gcode,
    };
  }

  function parseC2DBytes(bytes, fileName, dependencies = {}) {
    if (looksLikeSQLite(bytes)) {
      return parseSQLiteC2D(bytes, fileName, dependencies);
    }
    return parseEmbeddedJsonC2D(bytes, fileName);
  }

  function looksLikeSQLite(bytes) {
    const header = new TextDecoder().decode(bytes.slice(0, 16));
    return header === "SQLite format 3\u0000";
  }

  function parseSQLiteC2D(bytes, fileName, dependencies = {}) {
    if (!dependencies.sql) {
      throw new Error("SQL.js is required to import this C2D file.");
    }
    if (!dependencies.inflate) {
      throw new Error("Pako is required to import this C2D file.");
    }

    const db = new dependencies.sql.Database(bytes);
    const params = rowsToMap(execRows(db, "SELECT key, value FROM params"));
    const itemRows = execRows(db, "SELECT id, uuid, name, type, version, data FROM items ORDER BY id");
    const elements = [];
    const elementsByUuid = new Map();
    const toolpaths = [];
    const warnings = [];

    for (const row of itemRows) {
      const [id, uuid, name, type, version, data] = row;
      if (!(data instanceof Uint8Array)) {
        continue;
      }

      const payload = decodeItemPayload(data, dependencies.inflate);
      if (payload == null) {
        if (type !== "model") {
          warnings.push(`Item ${id} (${type}/${name || "unnamed"}) ignored: payload is not JSON.`);
        }
        continue;
      }

      if (type === "element") {
        const element = {
          ...payload,
          __containerType: "sqlite",
          __itemType: type,
          __itemName: name,
          __version: version,
          uuid: payload.id || uuid || `item-${id}`,
        };
        elements.push(element);
        elementsByUuid.set(element.uuid, element);
        continue;
      }

      if (type === "toolpath") {
        toolpaths.push({
          ...payload,
          uuid: payload.uuid || uuid || `toolpath-${id}`,
          __itemName: name,
          visible: payload.enabled !== false,
        });
      }
    }

    db.close();

    return {
      fileName,
      sourceType: "sqlite-c2d",
      document: {
        width: toNumber(params.width, 0),
        height: toNumber(params.height, 0),
        thickness: toNumber(params.thickness, 0),
        zeroX: toNumber(params.zero_x, 0),
        zeroY: toNumber(params.zero_y, 0),
        zeroZ: toNumber(params.zero_z, 0),
        safeZ: toNumber(params.retract, 8),
        displayMm: params.display_mm !== "0",
        material: params.material || "",
        machine: params.machine || "",
        buildNum: params.build_num || "",
      },
      params,
      elements,
      elementsByUuid,
      toolpaths,
      parseWarnings: warnings,
    };
  }

  function parseEmbeddedJsonC2D(bytes, fileName) {
    const json = extractJsonObjectFromBytes(bytes);
    const doc = json.DOCUMENT_VALUES || {};
    const elements = [];
    const elementsByUuid = new Map();
    let syntheticCounter = 0;

    const push = (element) => {
      const uuid = element.id || element.uuid || `legacy-${syntheticCounter++}`;
      const normalized = {
        ...element,
        uuid,
        __containerType: "legacy-json",
      };
      elements.push(normalized);
      elementsByUuid.set(uuid, normalized);
    };

    for (const circle of json.CIRCLE_OBJECTS || []) {
      push({ ...circle, geometryType: "circle" });
    }
    for (const curve of json.CURVE_OBJECTS || []) {
      push({ ...curve, geometryType: "curve" });
    }
    for (const rect of json.RECT_OBJECTS || []) {
      push({ ...rect, geometryType: "rectangle" });
    }
    for (const poly of json.REGULAR_POLYGON_OBJECTS || []) {
      push({ ...poly, geometryType: "regularPolygon" });
    }
    for (const textObject of json.TEXT_OBJECTS || []) {
      push({ ...textObject, geometryType: "text" });
    }

    const toolpaths = elements.map((element, index) => ({
      type: "contour",
      name: `Contour ${index + 1}`,
      uuid: `legacy-toolpath-${index + 1}`,
      enabled: true,
      visible: true,
      ofset_dir: 0,
      elements: [{ uuid: element.uuid }],
    }));

    return {
      fileName,
      sourceType: "legacy-json-c2d",
      document: {
        width: toNumber(doc.WIDTH, 0),
        height: toNumber(doc.HEIGHT, 0),
        thickness: toNumber(doc.THICKNESS, 0),
        zeroX: toNumber(doc.ZERO_X, 0),
        zeroY: toNumber(doc.ZERO_Y, 0),
        zeroZ: toNumber(doc.ZERO_Z, 0),
        safeZ: 8,
        displayMm: true,
        material: "",
        machine: "",
        buildNum: "legacy-json",
      },
      params: doc,
      elements,
      elementsByUuid,
      toolpaths,
      parseWarnings: [
        "Legacy JSON file detected: toolpaths were not present, so one centerline contour per geometry was generated.",
      ],
    };
  }

  function decodeItemPayload(blob, inflateLib) {
    const decoder = new TextDecoder();
    const attempts = [
      () => decoder.decode(inflateLib.inflate(blob)),
      () => decoder.decode(blob),
    ];

    for (const attempt of attempts) {
      try {
        return JSON.parse(attempt());
      } catch {
        // Continue.
      }
    }
    return null;
  }

  function extractJsonObjectFromBytes(bytes) {
    const start = bytes.indexOf(0x7b);
    if (start === -1) {
      throw new Error("No usable JSON was found in the file.");
    }

    let depth = 0;
    let inString = false;
    let escaped = false;
    for (let index = start; index < bytes.length; index += 1) {
      const char = bytes[index];
      if (inString) {
        if (escaped) {
          escaped = false;
          continue;
        }
        if (char === 0x5c) {
          escaped = true;
          continue;
        }
        if (char === 0x22) {
          inString = false;
        }
        continue;
      }

      if (char === 0x22) {
        inString = true;
        continue;
      }
      if (char === 0x7b) {
        depth += 1;
        continue;
      }
      if (char === 0x7d) {
        depth -= 1;
        if (depth === 0) {
          const text = new TextDecoder().decode(bytes.slice(start, index + 1));
          return JSON.parse(text);
        }
      }
    }

    throw new Error("JSON appears truncated or the end of the document could not be found.");
  }

  function execRows(db, sql) {
    const result = db.exec(sql);
    return result.length ? result[0].values : [];
  }

  function rowsToMap(rows) {
    const output = {};
    for (const [key, value] of rows) {
      output[key] = value;
    }
    return output;
  }

  function generateGcodeFromParsed(parsed, fileName, dependencies = {}) {
    const warnings = [...parsed.parseWarnings];
    const lines = [];
    const safeZ = sanitizePositiveNumber(parsed.document.safeZ, 8);
    const zeroX = toNumber(parsed.document.zeroX, 0);
    const zeroY = toNumber(parsed.document.zeroY, 0);
    const activeToolpaths = (parsed.toolpaths || []).filter((toolpath) => toolpath.enabled !== false && toolpath.visible !== false);
    let motionState = { x: 0, y: 0, z: safeZ, timeSec: 0 };
    let currentSpindle = null;
    let currentFeedRate = null;
    let cutCount = 0;
    let skippedToolpaths = 0;

    lines.push(`(Generated from ${fileName || parsed.fileName || "input.c2d"})`);
    lines.push(`(Source type: ${parsed.sourceType})`);
    lines.push("G21");
    lines.push("G90");
    lines.push("G17");
    lines.push("G94");
    lines.push("G54");
    lines.push(`G0 Z${formatNumber(safeZ)}`);

    function emitRapid(target) {
      const next = {
        x: target.x ?? motionState.x,
        y: target.y ?? motionState.y,
        z: target.z ?? motionState.z,
      };
      const parts = [];
      if (!numbersEqual(next.x, motionState.x)) parts.push(`X${formatNumber(next.x)}`);
      if (!numbersEqual(next.y, motionState.y)) parts.push(`Y${formatNumber(next.y)}`);
      if (!numbersEqual(next.z, motionState.z)) parts.push(`Z${formatNumber(next.z)}`);
      if (!parts.length) {
        return;
      }
      lines.push(`G0 ${parts.join(" ")}`);
      motionState = appendMotion(motionState, next, RAPID_RATE_MM_PER_MIN);
    }

    function emitFeedChange(rate) {
      if (numbersEqual(currentFeedRate, rate)) {
        return;
      }
      lines.push(`G1 F${formatNumber(rate)}`);
      currentFeedRate = rate;
    }

    function emitPlunge(z, rate) {
      if (numbersEqual(z, motionState.z)) {
        return;
      }
      const command = currentFeedRate === null || !numbersEqual(currentFeedRate, rate)
        ? `G1 Z${formatNumber(z)} F${formatNumber(rate)}`
        : `G1 Z${formatNumber(z)}`;
      lines.push(command);
      currentFeedRate = rate;
      motionState = appendMotion(motionState, {
        x: motionState.x,
        y: motionState.y,
        z,
      }, rate);
    }

    function emitCut(point, rate) {
      const [x, y] = point;
      const parts = [];
      if (!numbersEqual(x, motionState.x)) parts.push(`X${formatNumber(x)}`);
      if (!numbersEqual(y, motionState.y)) parts.push(`Y${formatNumber(y)}`);
      if (!parts.length) {
        return;
      }
      emitFeedChange(rate);
      lines.push(`G1 ${parts.join(" ")}`);
      motionState = appendMotion(motionState, { x, y, z: motionState.z }, rate);
    }

    for (const toolpath of activeToolpaths) {
      if ((toolpath.type || "contour") !== "contour") {
        skippedToolpaths += 1;
        warnings.push(`Toolpath "${toolpath.name || toolpath.uuid}" ignored: type ${toolpath.type} is not supported yet.`);
        continue;
      }

      const toolRadius = Math.max(toNumber(toolpath.tool?.diameter, 0) / 2, 0);
      const stockToLeave = Math.max(toNumber(toolpath.stock_to_leave, 0), 0);
      const offsetDirection = normalizeOffsetDirection(toolpath.ofset_dir);
      const offsetAmount = offsetDirection === 0 ? 0 : toolRadius + stockToLeave;
      const feed = sanitizePositiveNumber(toolpath.speeds?.feedrate, 1);
      const plunge = sanitizePositiveNumber(toolpath.speeds?.plungerate, feed);
      const spindle = Math.max(0, Math.round(sanitizePositiveNumber(toolpath.speeds?.rpm, 0)));
      const startDepth = Math.max(0, toNumber(toolpath.start_depth, 0));
      const endDepth = Math.max(startDepth, toNumber(toolpath.end_depth, startDepth));
      const stepdown = Math.max(0.001, sanitizePositiveNumber(toolpath.stepdown, Math.max(endDepth - startDepth, 0.001)));
      const depthPasses = buildDepthPasses(startDepth, endDepth, stepdown);
      const elementRefs = toolpath.elements || [];
      const resolvedPaths = [];

      if (!elementRefs.length) {
        warnings.push(`Toolpath "${toolpath.name || toolpath.uuid}" ignored: no referenced geometry.`);
        continue;
      }

      for (const elementRef of elementRefs) {
        const element = parsed.elementsByUuid.get(elementRef.uuid);
        if (!element) {
          warnings.push(`Element ${elementRef.uuid} was not found for toolpath "${toolpath.name || toolpath.uuid}".`);
          continue;
        }
        const elementPaths = elementToPolylines(element, 0.2, warnings);
        for (const path of elementPaths) {
          const transformed = translateForWorkZero(path, zeroX, zeroY);
          const offsetPaths = offsetAmount > 0
            ? offsetClosedPath(transformed, offsetAmount, offsetDirection, warnings, toolpath.name || toolpath.uuid, dependencies.clipperLib)
            : [transformed];
          for (const offsetPath of offsetPaths) {
            resolvedPaths.push(offsetPath);
          }
        }
      }

      if (!resolvedPaths.length) {
        warnings.push(`Toolpath "${toolpath.name || toolpath.uuid}" ignored: no machinable path could be generated.`);
        continue;
      }

      const optimizedPaths = optimizePathSequence(resolvedPaths, [motionState.x, motionState.y]);

      if (spindle !== currentSpindle) {
        currentSpindle = spindle;
        if (spindle > 0) {
          lines.push(`M3 S${spindle}`);
        }
      }

      lines.push(`(Toolpath: ${toolpath.name || toolpath.uuid})`);
      lines.push(`(Offset: ${offsetDirectionLabel(offsetDirection)}, start ${formatNumber(startDepth)}mm, end ${formatNumber(endDepth)}mm, stepdown ${formatNumber(stepdown)}mm)`);

      if (!toolpath.ignore_tabs && hasTabs(parsed, toolpath)) {
        warnings.push(`Toolpath "${toolpath.name || toolpath.uuid}": tabs were detected but are not interpreted yet, continuous cutting was generated.`);
      }
      if (toolpath.enable_ramping) {
        warnings.push(`Toolpath "${toolpath.name || toolpath.uuid}": ramping was requested but is not implemented yet, so a vertical plunge was used.`);
      }

      for (const path of optimizedPaths) {
        if (path.points.length < 2) {
          continue;
        }
        const startPoint = path.points[0];
        emitRapid({ z: safeZ });
        emitRapid({ x: startPoint[0], y: startPoint[1] });

        for (const [passIndex, passDepth] of depthPasses.entries()) {
          if (passIndex > 0 && !path.closed) {
            emitRapid({ z: safeZ });
            emitRapid({ x: startPoint[0], y: startPoint[1] });
          }
          cutCount += 1;
          emitPlunge(-passDepth, plunge);
          for (let index = 1; index < path.points.length; index += 1) {
            emitCut(path.points[index], feed);
          }
          if (path.closed) {
            emitCut(startPoint, feed);
          }
        }

        emitRapid({ z: safeZ });
      }
    }

    emitRapid({ x: 0, y: 0 });
    emitRapid({ z: safeZ });
    if (currentSpindle !== null && currentSpindle > 0) {
      lines.push("M5");
    }
    lines.push("M30");

    if (!cutCount) {
      warnings.push("No cuts were generated. Check the toolpath types present in the file.");
    }
    if (skippedToolpaths > 0) {
      warnings.push(`${skippedToolpaths} non-contour toolpath(s) were ignored.`);
    }

    return {
      gcode: lines.join("\n"),
      warnings: uniqueStrings(warnings),
      stats: {
        activeToolpaths: activeToolpaths.length,
        cutCount,
        safeZ,
        totalDurationSec: motionState.timeSec,
      },
    };
  }

  function hasTabs(parsed, toolpath) {
    return (toolpath.elements || []).some((ref) => {
      const element = parsed.elementsByUuid.get(ref.uuid);
      return Array.isArray(element?.tabs) && element.tabs.length > 0;
    });
  }

  function translateForWorkZero(path, zeroX, zeroY) {
    return {
      ...path,
      points: path.points.map(([x, y]) => [x - zeroX, y - zeroY]),
    };
  }

  function elementToPolylines(element, tolerance, warnings) {
    if (Array.isArray(element.rendered) && element.rendered.length) {
      return normalizePaths(renderedTextToPaths(element));
    }

    if (Array.isArray(element.points) && Array.isArray(element.point_type) && Array.isArray(element.cp1) && Array.isArray(element.cp2)) {
      return normalizePaths([genericCurveElementToPath(element, tolerance)].filter(Boolean));
    }

    if (Array.isArray(element.points) && Array.isArray(element.point_type) && Array.isArray(element.control_point_1) && Array.isArray(element.control_point_2)) {
      return normalizePaths([legacyCurveElementToPath(element, tolerance)].filter(Boolean));
    }

    switch (element.geometryType) {
      case "circle":
        return normalizePaths([circleToPath(element)]);
      case "regularPolygon":
        return normalizePaths([regularPolygonToPath(element)]);
      case "rectangle":
        return normalizePaths([legacyRectangleToPath(element, tolerance, warnings)]);
      default:
        warnings.push(`Geometry ${element.geometryType || "unknown"} (${element.uuid}) is not supported.`);
        return [];
    }
  }

  function genericCurveElementToPath(element, tolerance) {
    const points = element.points || [];
    const pointTypes = element.point_type || [];
    const cp1 = element.cp1 || [];
    const cp2 = element.cp2 || [];
    const position = element.position || [0, 0];
    const isClosed = pointTypes[pointTypes.length - 1] === 4;
    const effectiveCount = isClosed ? pointTypes.length - 1 : pointTypes.length;
    if (effectiveCount < 2 || points.length < 2) {
      return null;
    }

    const output = [addPoints(points[0], position)];
    for (let index = 1; index < effectiveCount; index += 1) {
      const segmentType = pointTypes[index];
      const target = addPoints(points[index], position);
      if (segmentType === 3) {
        const start = output[output.length - 1];
        const handle1 = addPoints(cp1[index] || points[index - 1], position);
        const handle2 = addPoints(cp2[index] || points[index], position);
        appendFlattenedCubic(output, start, handle1, handle2, target, tolerance);
      } else {
        output.push(target);
      }
    }

    return {
      closed: isClosed,
      points: dedupeSequentialPoints(output, 1e-6),
      source: element.uuid,
    };
  }

  function legacyCurveElementToPath(element, tolerance) {
    const points = element.points || [];
    const pointTypes = element.point_type || [];
    const handles1 = element.control_point_1 || [];
    const handles2 = element.control_point_2 || [];
    const position = element.position || [0, 0];
    const isClosed = pointTypes[pointTypes.length - 1] === 4;
    const effectiveCount = isClosed ? pointTypes.length - 1 : pointTypes.length;
    if (effectiveCount < 2 || points.length < 2) {
      return null;
    }

    const output = [addPoints(points[0], position)];
    for (let index = 1; index < effectiveCount; index += 1) {
      const previousType = pointTypes[index - 1];
      const target = addPoints(points[index], position);
      if (previousType === 3) {
        const start = output[output.length - 1];
        const handle1 = addPoints(handles2[index - 1] || points[index - 1], position);
        const handle2 = addPoints(handles1[index] || points[index], position);
        appendFlattenedCubic(output, start, handle1, handle2, target, tolerance);
      } else if (previousType === 1 || previousType === 0) {
        output.push(target);
      }
    }

    if (isClosed && pointTypes[0] === 3 && points.length > 1) {
      const start = output[output.length - 1];
      const target = addPoints(points[0], position);
      const handle1 = addPoints(handles2[effectiveCount - 1] || points[effectiveCount - 1], position);
      const handle2 = addPoints(handles1[0] || points[0], position);
      appendFlattenedCubic(output, start, handle1, handle2, target, tolerance);
    }

    return {
      closed: isClosed,
      points: dedupeSequentialPoints(output, 1e-6),
      source: element.uuid,
    };
  }

  function renderedTextToPaths(element) {
    const transform = element.transform;
    const position = element.position || [0, 0];
    return element.rendered
      .filter((subpath) => Array.isArray(subpath) && subpath.length > 1)
      .map((subpath) => {
        const points = subpath.map((point) => {
          const translated = addPoints(point, position);
          return applyTransform(translated, transform);
        });
        return {
          closed: true,
          points: dedupeSequentialPoints(points, 1e-6),
          source: element.uuid,
        };
      });
  }

  function applyTransform(point, transform) {
    if (!Array.isArray(transform) || transform.length !== 9) {
      return point;
    }
    const [m00, m01, , m10, m11, , tx, ty] = transform;
    return [
      point[0] * m00 + point[1] * m10 + tx,
      point[0] * m01 + point[1] * m11 + ty,
    ];
  }

  function circleToPath(element) {
    const [cx, cy] = element.position || [0, 0];
    const radius = Math.max(0.001, toNumber(element.radius, 0));
    const steps = Math.max(24, Math.ceil((2 * Math.PI * radius) / 2));
    const points = [];
    for (let index = 0; index < steps; index += 1) {
      const angle = (Math.PI * 2 * index) / steps;
      points.push([cx + Math.cos(angle) * radius, cy + Math.sin(angle) * radius]);
    }
    return {
      closed: true,
      points,
      source: element.uuid,
    };
  }

  function regularPolygonToPath(element) {
    const [cx, cy] = element.position || [0, 0];
    const sides = Math.max(3, Math.round(toNumber(element.num_sides, 3)));
    const radius = Math.max(0.001, toNumber(element.radius, 0));
    const rotation = degreesToRadians(toNumber(element.rotation, 0));
    const points = [];
    for (let index = 0; index < sides; index += 1) {
      const angle = rotation + (Math.PI * 2 * index) / sides;
      points.push([cx + Math.cos(angle) * radius, cy + Math.sin(angle) * radius]);
    }
    return {
      closed: true,
      points,
      source: element.uuid,
    };
  }

  function legacyRectangleToPath(element, tolerance, warnings) {
    const [cx, cy] = element.position || [0, 0];
    const width = Math.max(0.001, toNumber(element.width, 0));
    const height = Math.max(0.001, toNumber(element.height, 0));
    const radius = Math.max(0, toNumber(element.radius, 0));
    const cornerType = toNumber(element.corner_type, 0);
    if (cornerType > 0 && radius > 0) {
      warnings.push(`Legacy rectangle ${element.uuid}: special corners were simplified as rounded polylines.`);
    }

    const halfW = width / 2;
    const halfH = height / 2;
    const effectiveRadius = Math.min(radius, halfW, halfH);
    const points = [];
    if (effectiveRadius <= 0) {
      points.push([cx + halfW, cy + halfH], [cx + halfW, cy - halfH], [cx - halfW, cy - halfH], [cx - halfW, cy + halfH]);
    } else {
      const arcSegments = Math.max(4, Math.ceil((Math.PI * effectiveRadius / 2) / Math.max(0.2, tolerance)));
      appendArcPoints(points, [cx + halfW - effectiveRadius, cy + halfH - effectiveRadius], effectiveRadius, 0, -Math.PI / 2, arcSegments);
      appendArcPoints(points, [cx + halfW - effectiveRadius, cy - halfH + effectiveRadius], effectiveRadius, -Math.PI / 2, -Math.PI, arcSegments, true);
      appendArcPoints(points, [cx - halfW + effectiveRadius, cy - halfH + effectiveRadius], effectiveRadius, -Math.PI, -Math.PI * 1.5, arcSegments, true);
      appendArcPoints(points, [cx - halfW + effectiveRadius, cy + halfH - effectiveRadius], effectiveRadius, -Math.PI * 1.5, -Math.PI * 2, arcSegments, true);
    }

    return {
      closed: true,
      points: rotatePoints(points, degreesToRadians(toNumber(element.rotation, 0)), [cx, cy]),
      source: element.uuid,
    };
  }

  function appendArcPoints(output, center, radius, startAngle, endAngle, segments, skipFirst = false) {
    for (let index = 0; index <= segments; index += 1) {
      if (skipFirst && index === 0) {
        continue;
      }
      const t = index / segments;
      const angle = startAngle + (endAngle - startAngle) * t;
      output.push([center[0] + Math.cos(angle) * radius, center[1] + Math.sin(angle) * radius]);
    }
  }

  function rotatePoints(points, angle, center) {
    if (!angle) {
      return points;
    }
    const [cx, cy] = center;
    const sin = Math.sin(angle);
    const cos = Math.cos(angle);
    return points.map(([x, y]) => {
      const dx = x - cx;
      const dy = y - cy;
      return [cx + dx * cos - dy * sin, cy + dx * sin + dy * cos];
    });
  }

  function appendFlattenedCubic(output, p0, p1, p2, p3, tolerance) {
    const points = flattenCubicBezier(p0, p1, p2, p3, tolerance);
    for (let index = 1; index < points.length; index += 1) {
      output.push(points[index]);
    }
  }

  function flattenCubicBezier(p0, p1, p2, p3, tolerance) {
    const output = [p0];
    flattenCubicRecursive(output, p0, p1, p2, p3, tolerance, 0);
    return output;
  }

  function flattenCubicRecursive(output, p0, p1, p2, p3, tolerance, depth) {
    if (depth > 12 || cubicFlatEnough(p0, p1, p2, p3, tolerance)) {
      output.push(p3);
      return;
    }

    const p01 = midpoint(p0, p1);
    const p12 = midpoint(p1, p2);
    const p23 = midpoint(p2, p3);
    const p012 = midpoint(p01, p12);
    const p123 = midpoint(p12, p23);
    const p0123 = midpoint(p012, p123);

    flattenCubicRecursive(output, p0, p01, p012, p0123, tolerance, depth + 1);
    flattenCubicRecursive(output, p0123, p123, p23, p3, tolerance, depth + 1);
  }

  function cubicFlatEnough(p0, p1, p2, p3, tolerance) {
    const d1 = distancePointToLine(p1, p0, p3);
    const d2 = distancePointToLine(p2, p0, p3);
    return Math.max(d1, d2) <= tolerance;
  }

  function distancePointToLine(point, lineStart, lineEnd) {
    const [x, y] = point;
    const [x1, y1] = lineStart;
    const [x2, y2] = lineEnd;
    const dx = x2 - x1;
    const dy = y2 - y1;
    const denominator = Math.hypot(dx, dy);
    if (denominator === 0) {
      return Math.hypot(x - x1, y - y1);
    }
    return Math.abs(dy * x - dx * y + x2 * y1 - y2 * x1) / denominator;
  }

  function midpoint(a, b) {
    return [(a[0] + b[0]) / 2, (a[1] + b[1]) / 2];
  }

  function offsetClosedPath(path, amount, direction, warnings, label, clipperLib) {
    if (direction === 0 || amount <= 0) {
      return [path];
    }
    if (!path.closed) {
      warnings.push(`Toolpath "${label}": an offset was requested on an open curve, so centerline cutting was kept.`);
      return [path];
    }
    if (!clipperLib) {
      warnings.push(`Toolpath "${label}": offset computation is unavailable, so centerline cutting was kept.`);
      return [path];
    }

    const sourcePath = removeClosingDuplicate(path.points);
    if (sourcePath.length < 3) {
      return [path];
    }

    const baseArea = Math.abs(signedArea(sourcePath));
    const candidates = [];
    for (const delta of [amount, -amount]) {
      const solution = runClipperOffset(sourcePath, delta, clipperLib);
      if (!solution.length) {
        continue;
      }
      const totalArea = solution.reduce((sum, candidate) => sum + Math.abs(signedArea(candidate.points)), 0);
      candidates.push({ delta, totalArea, paths: solution });
    }

    if (!candidates.length) {
      warnings.push(`Toolpath "${label}": offset computation failed, so centerline cutting was kept.`);
      return [path];
    }

    const target = direction < 0
      ? candidates.filter((candidate) => candidate.totalArea < baseArea)
      : candidates.filter((candidate) => candidate.totalArea > baseArea);
    const chosen = (target.length ? target : candidates)
      .sort((a, b) => Math.abs(Math.abs(a.delta) - amount) - Math.abs(Math.abs(b.delta) - amount))[0];

    return chosen.paths;
  }

  function runClipperOffset(points, delta, clipperLib) {
    const clipperPath = points.map(([x, y]) => ({ X: Math.round(x * CLIPPER_SCALE), Y: Math.round(y * CLIPPER_SCALE) }));
    const clipperOffset = new clipperLib.ClipperOffset(2, 0.25 * CLIPPER_SCALE);
    const solution = new clipperLib.Paths();
    clipperOffset.AddPath(clipperPath, clipperLib.JoinType.jtRound, clipperLib.EndType.etClosedPolygon);
    clipperOffset.Execute(solution, delta * CLIPPER_SCALE);

    return solution.map((path) => ({
      closed: true,
      points: path.map((point) => [point.X / CLIPPER_SCALE, point.Y / CLIPPER_SCALE]),
    }));
  }

  function optimizePathSequence(paths, startPoint) {
    const remaining = paths.slice();
    const optimized = [];
    let cursor = startPoint;

    while (remaining.length) {
      let bestIndex = 0;
      let bestPath = orientPathForNearestStart(remaining[0], cursor);
      let bestDistance = pointDistance(cursor, bestPath.points[0]);

      for (let index = 1; index < remaining.length; index += 1) {
        const candidate = orientPathForNearestStart(remaining[index], cursor);
        const distance = pointDistance(cursor, candidate.points[0]);
        if (distance < bestDistance) {
          bestIndex = index;
          bestPath = candidate;
          bestDistance = distance;
        }
      }

      optimized.push(bestPath);
      remaining.splice(bestIndex, 1);
      cursor = bestPath.closed ? bestPath.points[0] : bestPath.points[bestPath.points.length - 1];
    }

    return optimized;
  }

  function orientPathForNearestStart(path, referencePoint) {
    if (!path.points?.length) {
      return path;
    }

    if (path.closed) {
      return rotateClosedPath(path, findNearestPointIndex(path.points, referencePoint));
    }

    const firstPoint = path.points[0];
    const lastPoint = path.points[path.points.length - 1];
    return pointDistance(referencePoint, lastPoint) < pointDistance(referencePoint, firstPoint) ? reversePath(path) : path;
  }

  function rotateClosedPath(path, startIndex) {
    if (!path.points?.length || startIndex <= 0) {
      return path;
    }
    return {
      ...path,
      points: path.points.slice(startIndex).concat(path.points.slice(0, startIndex)),
    };
  }

  function reversePath(path) {
    return {
      ...path,
      points: path.points.slice().reverse(),
    };
  }

  function findNearestPointIndex(points, referencePoint) {
    let bestIndex = 0;
    let bestDistance = Number.POSITIVE_INFINITY;
    for (let index = 0; index < points.length; index += 1) {
      const distance = pointDistance(points[index], referencePoint);
      if (distance < bestDistance) {
        bestDistance = distance;
        bestIndex = index;
      }
    }
    return bestIndex;
  }

  function buildDepthPasses(startDepth, endDepth, stepdown) {
    if (endDepth <= 0) {
      return [];
    }
    if (endDepth <= startDepth) {
      return [endDepth];
    }
    const passes = [];
    let current = startDepth;
    while (current + stepdown < endDepth) {
      current += stepdown;
      passes.push(current);
    }
    if (!passes.length || passes[passes.length - 1] !== endDepth) {
      passes.push(endDepth);
    }
    return passes;
  }

  function appendMotion(current, target, rateMmPerMin) {
    const safeRate = Math.max(0.001, rateMmPerMin);
    const distance = distance3D(current, target);
    const durationSec = distance === 0 ? 0 : (distance / safeRate) * 60;
    return {
      x: target.x,
      y: target.y,
      z: target.z,
      timeSec: current.timeSec + durationSec,
    };
  }

  function distance3D(a, b) {
    const dx = b.x - a.x;
    const dy = b.y - a.y;
    const dz = b.z - a.z;
    return Math.hypot(dx, dy, dz);
  }

  function sanitizePositiveNumber(value, fallback) {
    const numeric = toNumber(value, fallback);
    return numeric > 0 ? numeric : fallback;
  }

  function toNumber(value, fallback = 0) {
    const numeric = Number.parseFloat(value);
    return Number.isFinite(numeric) ? numeric : fallback;
  }

  function formatNumber(value) {
    return Number(value).toFixed(3).replace(/\.000$/, "").replace(/(\.\d*[1-9])0+$/, "$1");
  }

  function signedArea(points) {
    let sum = 0;
    for (let index = 0; index < points.length; index += 1) {
      const current = points[index];
      const next = points[(index + 1) % points.length];
      sum += current[0] * next[1] - next[0] * current[1];
    }
    return sum / 2;
  }

  function dedupeSequentialPoints(points, epsilon) {
    const output = [];
    for (const point of points) {
      const previous = output[output.length - 1];
      if (!previous || pointDistance(previous, point) > epsilon) {
        output.push(point);
      }
    }
    return output;
  }

  function normalizePaths(paths) {
    return paths
      .map((path) => {
        const points = dedupeSequentialPoints(path.points || [], 1e-6);
        if (path.closed && points.length > 2 && pointDistance(points[0], points[points.length - 1]) < 1e-6) {
          return {
            ...path,
            points: points.slice(0, -1),
          };
        }
        return {
          ...path,
          points,
        };
      })
      .filter((path) => path.points.length > 1);
  }

  function pointDistance(a, b) {
    return Math.hypot(a[0] - b[0], a[1] - b[1]);
  }

  function numbersEqual(a, b, epsilon = 1e-6) {
    return Math.abs(a - b) <= epsilon;
  }

  function removeClosingDuplicate(points) {
    if (points.length < 2) {
      return points.slice();
    }
    if (pointDistance(points[0], points[points.length - 1]) < 1e-6) {
      return points.slice(0, -1);
    }
    return points.slice();
  }

  function addPoints(a, b) {
    return [toNumber(a?.[0], 0) + toNumber(b?.[0], 0), toNumber(a?.[1], 0) + toNumber(b?.[1], 0)];
  }

  function normalizeOffsetDirection(value) {
    const numeric = Math.round(toNumber(value, 0));
    if (numeric < 0) return -1;
    if (numeric > 0) return 1;
    return 0;
  }

  function offsetDirectionLabel(direction) {
    if (direction < 0) return "inside";
    if (direction > 0) return "outside";
    return "centerline";
  }

  function degreesToRadians(value) {
    return (value * Math.PI) / 180;
  }

  function uniqueStrings(values) {
    return [...new Set(values.filter(Boolean))];
  }

  function createTextFile(fileName, content, lastModified) {
    try {
      return new File([content], fileName, {
        type: "text/plain;charset=utf-8",
        lastModified: lastModified || Date.now(),
      });
    } catch {
      const blob = new Blob([content], { type: "text/plain;charset=utf-8" });
      blob.name = fileName;
      blob.lastModified = lastModified || Date.now();
      return blob;
    }
  }

  return {
    buildAcceptAttribute,
    convertC2DBytes,
    convertUploadFile,
    generateGcodeFromParsed,
    getOutputFileName,
    isC2DFileName,
    parseC2DBytes,
  };
});
