"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const vm = require("node:vm");

const frontend = fs.readFileSync(process.argv[2], "utf8");
const index = fs.readFileSync(process.argv[3], "utf8");
const javascriptAsset = process.argv[4];
const katexCss = fs.readFileSync(process.argv[5], "utf8");
const fontDirectory = process.argv[6];
const appCss = fs.readFileSync(process.argv[7], "utf8");
const config = fs.readFileSync(process.argv[8], "utf8");
const runKindle = fs.readFileSync(process.argv[9], "utf8");
const simulatorIndex = fs.readFileSync(process.argv[10], "utf8");
const simulatorJs = fs.readFileSync(process.argv[11], "utf8");
const simulatorCss = fs.readFileSync(process.argv[12], "utf8");
const simulatorHostFonts = fs.readFileSync(process.argv[13], "utf8");
const httpServer = fs.readFileSync(process.argv[14], "utf8");
const whisperTouch = fs.readFileSync(process.argv[15], "utf8");
const bionicFontDirectory = process.argv[16];
const potionFontconfig = fs.readFileSync(process.argv[17], "utf8");

assert.doesNotMatch(frontend, /window\.katex|katex\.render|loadKatex|katexState|data-expr/);
assert.match(index, /vendor\/katex\/katex\.min\.css\?v=0\.16\.25-native/);
assert.equal(fs.existsSync(javascriptAsset), false);
assert.ok(index.indexOf("vendor/katex/katex.min.css") < index.indexOf("app.css"),
  "application compatibility CSS must load after KaTeX CSS");
assert.doesNotMatch(index, /class="eyebrow"/);
assert.match(index, /<div class="section-heading">\s*<h2>Notion Pages<\/h2>\s*<div class="search-row">/);
assert.match(index, /id="sort-opened"[^>]*aria-pressed="true">Opened<\/button><button id="sort-edited"[^>]*>Edited<\/button>/);
assert.match(index, /id="page-pin"[^>]*aria-label="Pin page"[^>]*>&#9734;<\/button>\s*<button id="pages-home"/);
assert.match(index,
  /id="night"[\s\S]*id="rotation"[^>]*title="Rotation locked"[^>]*aria-label="Rotation locked"[^>]*>⌽<\/button>[\s\S]*id="refresh"/);
assert.match(index,
  /<h3>Reading<\/h3>[\s\S]*Page font[\s\S]*Bionic Reading[\s\S]*Experimental/);
assert.match(index,
  /name="bionic-reading" value="off"[\s\S]*name="bionic-reading" value="on"/);
assert.match(appCss, /\.header-actions\s*{[\s\S]*width:\s*439px;[\s\S]*font-size:\s*0;/);
assert.match(appCss,
  /\.header-actions button,[\s\S]*?\.header-actions \.header-button\.icon-button\s*{[\s\S]*width:\s*69px;[\s\S]*height:\s*69px;/);
assert.match(runKindle, /'supportedOrientation','UDLR'/);
assert.match(runKindle, /LD_PRELOAD=.*libmesquite-whisper-touch\.so \/usr\/bin\/mesquite/);
assert.match(runKindle,
  /FONTCONFIG_FILE=\$POTION_ROOT\/etc\/fontconfig-potion\.conf/);
assert.match(potionFontconfig, /<include ignore_missing="no">\/etc\/fonts\/fonts\.conf<\/include>/);
assert.match(potionFontconfig,
  /<dir>\/mnt\/us\/potion\/share\/potion\/vendor\/fast-font\/fonts<\/dir>/);
assert.doesNotMatch(potionFontconfig, /\/usr\/share\/fonts|\/etc\/fonts\/conf\.d/);
assert.deepEqual(fs.readdirSync(bionicFontDirectory).sort(), [
  "PotionFastSans-Bold.otf",
  "PotionFastSans-BoldItalic.otf",
  "PotionFastSans-Italic.otf",
  "PotionFastSans-Regular.otf"
]);
assert.match(whisperTouch, /win_mgr_utils_new_application_name/);
assert.match(whisperTouch, /win_mgr_utils_add_is_wisper_touch_supported/);
assert.match(whisperTouch, /win_mgr_utils_new_name\(0, "application"\)/);
assert.match(whisperTouch, /win_mgr_utils_add_is_wisper_touch_supported\(name, 1\)/);
assert.doesNotMatch(frontend, /\/api\/input|\/api\/simulator\/input|pollInput|waitForInput/);
assert.doesNotMatch(httpServer, /\/api\/input|\/api\/simulator\/input|gpiokey|\/dev\/input|input_event/);
assert.match(httpServer, /select\(highest \+ 1, &set, nullptr, nullptr, nullptr\)/);
assert.match(httpServer,
  /retrieve_image\(url, image, error\)[\s\S]*?CachePolicy::proxied_image/);
assert.match(httpServer,
  /is_immutable_asset_path\(relative\)[\s\S]*?CachePolicy::immutable_asset/);
assert.match(httpServer, /vendor\/fast-font\/[\s\S]*?\.otf/);
assert.match(simulatorJs, /potionSimulatorPageButton/);
assert.match(simulatorJs,
  /link\.href="\/simulator\/host-fonts\.css"/,
  "host-only font declarations must be injected by the simulator wrapper");
assert.equal((simulatorHostFonts.match(/font-family:\s*"Potion Fast Sans"/g) || []).length, 4);
for (const face of ["Regular", "Italic", "Bold", "BoldItalic"])
  assert.match(simulatorHostFonts,
    new RegExp(`url\\("/vendor/fast-font/fonts/PotionFastSans-${face}\\.otf"\\)`));
assert.doesNotMatch(appCss, /@font-face[\s\S]*Potion Fast Sans/,
  "Kindle application CSS must keep using process-local Fontconfig, not web-font loading");
assert.doesNotMatch(runKindle, /host-fonts\.css/,
  "the Kindle launch path must not acquire simulator font declarations");

const bionicLogic = frontend.match(
  /\/\* BIONIC_READING_LOGIC_BEGIN \*\/([\s\S]*?)\/\* BIONIC_READING_LOGIC_END \*\//);
assert.ok(bionicLogic, "missing Bionic Reading logic test boundary");
assert.match(bionicLogic[1], /currentReadingPosition\(\)[\s\S]*applyBionicClass\(\)[\s\S]*beginReadingRestore\(position, false\)/,
  "Bionic reflow must preserve Potion's logical block/fraction anchor");
assert.match(bionicLogic[1], /setBusy\(true\)[\s\S]*setBusy\(false\)/);
assert.match(bionicLogic[1], /root\.offsetHeight/,
  "Bionic activation must finish its one-time reflow inside the busy operation");
assert.doesNotMatch(bionicLogic[1],
  /createElement|createTextNode|innerHTML\s*=\s*page|textContent\s*=|MutationObserver|setInterval/,
  "Bionic Reading must not rewrite page text or create per-word DOM");
assert.match(frontend, /bionicReading = settings\.bionicReading === true/);
assert.match(frontend, /key=bionicReading&value=/);
assert.match(frontend,
  /selectedRadio\("bionic-reading", bionicReading \? "on" : "off"\)/);

assert.match(appCss,
  /#page-content\.bionic-reading \.potion-editable-content,[\s\S]*#page-content\.bionic-reading blockquote\s*\{[\s\S]*font-family:\s*"Potion Fast Sans"[\s\S]*text-rendering:\s*optimizeLegibility/);
assert.doesNotMatch(appCss,
  /(?:^|\n)\s*(?:body|\.page-content)\.bionic-reading[^\{]*\{[^}]*Potion Fast Sans/,
  "Bionic family must never be scoped at an application-wide ancestor");
assert.match(appCss,
  /#page-content\.bionic-reading \.potion-editable-content code,[\s\S]*font-family:\s*monospace/);
assert.match(appCss,
  /#page-content\.bionic-reading \.math,[\s\S]*#page-content\.bionic-reading \.katex[\s\S]*text-rendering:\s*auto/);
assert.doesNotMatch(appCss,
  /#page-content\.bionic-reading\s+(?:h[1-6]|table|th|td|\.toggle-summary|\.child-page)[^{,]*[,{][^}]*Potion Fast Sans/);
{
  const match = frontend.match(
    /var pageButtonDownCode = 0,([\s\S]*?)function saveSetting\(key, value\)/);
  assert.ok(match, "missing direct page-button handler");
  const calls = [];
  const listeners = {};
  const context = {
    Date,
    pageButtonMode: "normal",
    readyForInput: function() { return true; },
    pageScroll: function(direction) { calls.push(direction); },
    document: {addEventListener: function(name, listener) { listeners[name] = listener; }},
    window: {event: null, location: {protocol: "file:"}}
  };
  function key(code) {
    return {keyCode: code, prevented: 0, stopped: 0,
      preventDefault: function() { this.prevented += 1; },
      stopPropagation: function() { this.stopped += 1; }};
  }
  vm.createContext(context);
  vm.runInContext("var pageButtonDownCode = 0," + match[1], context);
  const forward = key(34);
  listeners.keydown(forward);
  listeners.keydown(key(34));
  assert.deepEqual(calls, [1], "held/repeated keydown scrolls exactly once");
  assert.equal(forward.prevented, 1);
  assert.equal(forward.stopped, 1);
  listeners.keyup(key(34));
  listeners.keydown(key(33));
  assert.deepEqual(calls, [1, -1]);
  listeners.keyup(key(33));
  context.pageButtonMode = "reversed";
  listeners.keydown(key(33));
  assert.deepEqual(calls, [1, -1, 1],
    "reversed mode maps backward to downward scrolling");
}
assert.match(simulatorIndex,
  /id="orientation"[\s\S]*value="portrait">Portrait<[\s\S]*value="landscape">Landscape</);
assert.match(simulatorJs, /orientation="portrait"/,
  "the simulator defaults to portrait");
assert.match(simulatorJs,
  /orientation==="landscape"\?\{width:current\.height,height:current\.width\}/,
  "landscape swaps the simulated viewport dimensions");
assert.match(simulatorJs, /initEvent\("orientationchange",false,false\)/);
assert.match(simulatorCss, /\.toolbar #orientation\s*\{[\s\S]*min-width:\s*110px;/);
assert.match(appCss, /\.section-heading\s*{[\s\S]*display:\s*table;[\s\S]*height:\s*74px;/);
assert.match(appCss, /\.section-heading h2\s*{[\s\S]*display:\s*table-cell;[\s\S]*width:\s*270px;/);
assert.match(appCss, /\.search-row\s*{[\s\S]*display:\s*table-cell;[\s\S]*width:\s*auto;/);
assert.match(appCss, /\.page-sort button\.active\s*{[\s\S]*background:\s*#111;[\s\S]*color:\s*#fff;/);
assert.match(appCss, /\.page-list\s*{[\s\S]*top:\s*74px;/);
assert.match(appCss, /\.page-row\s*{[\s\S]*position:\s*relative;/);
assert.match(appCss, /\.page-pin\s*{[\s\S]*position:\s*absolute;[\s\S]*font:\s*39px/);
assert.match(frontend, /pageSortMode = "opened"/);
assert.match(frontend, /key=pageSortMode&value=/);
assert.match(frontend, /\/api\/pages\/" \+ encodeURIComponent\(pageId\) \+ "\/pin"/);
assert.match(frontend, /page\.pinned === true/);
assert.doesNotMatch(frontend, /localStorage/);

// Mesquite retains !important declarations in the live CSSStyleDeclaration
// after removeAttribute("style"). Verify that night-palette restoration clears
// the live properties/cssText as well as restoring the original attribute.
const nightPaletteMatch = frontend.match(
  /\/\* NIGHT_PALETTE_LOGIC_BEGIN \*\/([\s\S]*?)\/\* NIGHT_PALETTE_LOGIC_END \*\//);
assert.ok(nightPaletteMatch, "missing night-palette test boundary");
function nightPaletteElement(originalStyle) {
  let cssText = originalStyle === null ? "" : originalStyle;
  const attributes = {};
  if (originalStyle !== null) attributes.style = originalStyle;
  const style = {
    removeProperty: function(property) {
      const pattern = new RegExp("(?:^|;)\\s*" + property + "\\s*:[^;]*;?", "ig");
      cssText = cssText.replace(pattern, "");
    },
    setProperty: function(property, value, priority) {
      cssText += (cssText ? ";" : "") + property + ":" + value +
        (priority ? " !" + priority : "");
    }
  };
  Object.defineProperty(style, "cssText", {
    get: function() { return cssText; },
    set: function(value) { cssText = String(value); }
  });
  return {
    style,
    getAttribute: function(name) {
      return Object.prototype.hasOwnProperty.call(attributes, name) ?
        attributes[name] : null;
    },
    setAttribute: function(name, value) { attributes[name] = value; },
    // Deliberately leave style.cssText untouched, matching the Mesquite defect.
    removeAttribute: function(name) { delete attributes[name]; }
  };
}
{
  const context = {nightStyledElements: [], window: {}};
  vm.createContext(context);
  vm.runInContext(nightPaletteMatch[1], context);

  const classStyledHighlight = nightPaletteElement(null);
  context.setNightStyle(classStyledHighlight, "background-color", "rgb(55,55,55)");
  assert.match(classStyledHighlight.style.cssText, /background-color/);
  context.restoreNightPalette({});
  assert.equal(classStyledHighlight.style.cssText, "",
    "day mode clears Mesquite's retained temporary highlight color");
  assert.equal(classStyledHighlight.getAttribute("style"), null);

  const inlineStyled = nightPaletteElement("font-weight:bold");
  context.setNightStyle(inlineStyled, "color", "rgb(190,190,190)");
  context.restoreNightPalette({});
  assert.equal(inlineStyled.style.cssText, "font-weight:bold",
    "day mode restores unrelated original inline styles exactly");
}

// Rotation uses the WAF device API when present, persists through potiond,
// locks the exact current direction where window.orientation exposes it, and
// remains harmless in the desktop simulator.
const rotationMatch = frontend.match(
  /\/\* ROTATION_LOGIC_BEGIN \*\/([\s\S]*?)\/\* ROTATION_LOGIC_END \*\//);
assert.ok(rotationMatch, "missing rotation logic test boundary");
function rotationContext(options) {
  const button = { attributes: {}, setAttribute: function(key, value) {
    this.attributes[key] = value;
  }};
  const calls = [];
  const context = {
    rotationMode: "auto",
    window: {
      innerWidth: 1072,
      innerHeight: 1448,
      orientation: options.orientation
    },
    id: function() { return button; },
    encodeURIComponent,
    warning: function(error) { calls.push("warning:" + error); },
    request: function(method, path, body, done) {
      calls.push(method + " " + path + " " + body);
      done(options.failure ? "save failed" : null);
    }
  };
  if (options.device) context.window.kindle = { device: {
    setOrientation: function(value) { calls.push("orientation:" + value); }
  }};
  vm.createContext(context);
  vm.runInContext(rotationMatch[1], context);
  return { context, button, calls };
}
const autoRotation = rotationContext({ device: true, orientation: 0 });
autoRotation.context.chooseRotationMode("auto", true);
assert.equal(autoRotation.context.rotationMode, "auto");
assert.equal(autoRotation.button.innerHTML, "⌽");
assert.equal(autoRotation.button.attributes["aria-label"], "Rotation locked");
assert.deepEqual(autoRotation.calls,
  ["orientation:auto", "POST /api/settings key=rotationMode&value=auto"]);

const lockedRotation = rotationContext({ device: true, orientation: -90 });
lockedRotation.context.chooseRotationMode("locked", false);
assert.equal(lockedRotation.context.rotationMode, "locked");
assert.equal(lockedRotation.button.innerHTML, "⟳");
assert.equal(lockedRotation.button.attributes["aria-label"], "Auto rotation");
assert.deepEqual(lockedRotation.calls, ["orientation:landscapeRight"]);

const simulatorRotation = rotationContext({ device: false });
assert.doesNotThrow(function() {
  simulatorRotation.context.chooseRotationMode("locked", false);
});
assert.deepEqual(simulatorRotation.calls, []);
assert.match(frontend, /settings\.rotationMode === "locked"/);
assert.match(frontend, /key=rotationMode&value=/);

// Page navigation is an in-memory ES5 model after GET /api/pages. Exercise
// the production comparator/update functions rather than a duplicate model.
const navigationMatch = frontend.match(
  /\/\* PAGE_NAVIGATION_LOGIC_BEGIN \*\/([\s\S]*?)\/\* PAGE_NAVIGATION_LOGIC_END \*\//);
assert.ok(navigationMatch, "missing page navigation logic test boundary");
const navigation = { pageList: [], pageSortMode: "opened", Number };
vm.createContext(navigation);
vm.runInContext(navigationMatch[1], navigation);

function resetPages(pages, mode) {
  navigation.pageList = pages;
  navigation.pageSortMode = mode || "opened";
  navigation.sortPages();
  return navigation.pageList.map((page) => page.id);
}

const fixture = () => [
  { id: "a", title: "A", edited: "2026-01-04", opened: 40, pinned: false },
  { id: "b", title: "B", edited: "2026-01-03", opened: 30, pinned: false },
  { id: "c", title: "C", edited: "2026-01-02", opened: 0, pinned: false },
  { id: "d", title: "D", edited: "2026-01-01", opened: 10, pinned: true }
];

assert.deepEqual(resetPages(fixture()), ["d", "a", "b", "c"],
  "pinned pages precede unpinned pages and unopened pages sort last");
navigation.updatePageMetadata("c", "pinned", true);
navigation.sortPages();
assert.deepEqual(navigation.pageList.map((page) => page.id), ["d", "c", "a", "b"],
  "pinning a visible page moves it into the pinned group immediately");
navigation.updatePageMetadata("d", "pinned", false);
navigation.sortPages();
assert.deepEqual(navigation.pageList.map((page) => page.id), ["c", "a", "b", "d"],
  "unpinning returns a page to its opened-time position");
navigation.updatePageMetadata("d", "opened", 50);
navigation.sortPages();
assert.deepEqual(navigation.pageList.map((page) => page.id), ["c", "d", "a", "b"],
  "opening an older page moves it to the top of its unpinned group");
assert.deepEqual(resetPages(fixture(), "edited"), ["d", "a", "b", "c"],
  "Edited mode orders each pin group by edited timestamp");

const chooseMatch = frontend.match(
  /(function choosePageSort\(mode\) \{[\s\S]*?\n    \})\n\n    function showPages/);
assert.ok(chooseMatch, "missing choosePageSort");
function exerciseChoose(failure) {
  const buttons = { "sort-opened": {}, "sort-edited": {} };
  const calls = [];
  const context = {
    busy: false,
    pageSortSaveBusy: false,
    pageSortMode: "opened",
    id: (name) => buttons[name],
    setSortButtons: () => calls.push("buttons:" + context.pageSortMode),
    renderSortedPages: () => calls.push("render:" + context.pageSortMode),
    warning: () => calls.push("warning"),
    encodeURIComponent,
    request: (method, path, body, done) => {
      calls.push(method + " " + path);
      done(failure ? "save failed" : null);
    }
  };
  vm.createContext(context);
  vm.runInContext(chooseMatch[1], context);
  context.choosePageSort("edited");
  return { context, calls };
}
const successfulSort = exerciseChoose(false);
assert.equal(successfulSort.context.pageSortMode, "edited");
assert.deepEqual(successfulSort.calls,
  ["buttons:edited", "render:edited", "POST /api/settings"]);
assert.ok(!successfulSort.calls.some((call) => call.indexOf("GET /api/pages") >= 0),
  "sort switching must not fetch pages");
const failedSort = exerciseChoose(true);
assert.equal(failedSort.context.pageSortMode, "opened");
assert.deepEqual(failedSort.calls,
  ["buttons:edited", "render:edited", "POST /api/settings",
    "buttons:opened", "render:opened", "warning"]);

const chooseSource = chooseMatch[1];
const pinSource = frontend.match(
  /function setPagePinned\(pageId, pinned, button\) \{([\s\S]*?)\n    \}\n\n    function pageDate/)[1];
const showSource = frontend.match(
  /function showPages\(positionSaved\) \{([\s\S]*?)\n    \}\n\n    function loadPages/)[1];
const loadSource = frontend.match(
  /function loadPages\(\) \{([\s\S]*?)\n    \}\n\n    function openPage/)[1];
assert.doesNotMatch(chooseSource, /loadPages|GET[^\n]*\/api\/pages/);
assert.doesNotMatch(pinSource, /loadPages|GET[^\n]*\/api\/pages/);
assert.match(pinSource, /updatePageMetadata\(pageId, "pinned", pinned\)[\s\S]*renderSortedPages\(false\)/);
assert.equal((pinSource.match(/button\.disabled = false/g) || []).length, 2,
  "pin buttons must be re-enabled after both failed and successful writes");
assert.match(pinSource,
  /updatePageMetadata\(pageId, "pinned", pinned\)[\s\S]*button\.disabled = false;[\s\S]*renderSortedPages\(false\)/);
assert.match(showSource, /renderSortedPages\(false\)/);
assert.match(loadSource, /"GET",[\s\S]*"\/api\/pages\?query="/,
  "search/initial load must still fetch page metadata");
assert.match(frontend, /id\("search-button"\)\.onclick = loadPages/);
assert.match(frontend, /id\("search"\)\.onkeydown = function\(event\)[\s\S]*event\.keyCode === 13[\s\S]*loadPages\(\)/);
assert.match(frontend,
  /updatePageMetadata\([\s\S]*currentPageId,[\s\S]*"opened",[\s\S]*Math\.floor\(new Date\(\)\.getTime\(\) \/ 1000\)/);

const fontReferences = [...katexCss.matchAll(/fonts\/([^)'\"]+\.(?:woff2?|ttf))/g)]
  .map((match) => match[1]);
assert.equal(fontReferences.length, 20);
assert.equal(new Set(fontReferences).size, 20);
assert.ok(fontReferences.every((name) => name.endsWith(".woff")),
  "Mesquite-verified WOFF must be the only advertised font format");
for (const name of fontReferences) {
  assert.ok(fs.existsSync(`${fontDirectory}/${name}`), `missing KaTeX font ${name}`);
}
for (const family of ["KaTeX_AMS", "KaTeX_Main", "KaTeX_Math",
  "KaTeX_Size1", "KaTeX_Size2", "KaTeX_Size3", "KaTeX_Size4"]) {
  assert.match(katexCss, new RegExp(`font-family:[\"']?${family}[\"']?`));
}

// Mesquite repairs: reconstruct only ordinary scripts, then restore every
// native KaTeX two-row vlist baseline generically instead of rebuilding
// fractions, operator limits, radicals, or matrices one at a time.
assert.match(frontend, /function repairKindleScripts\(root\)/);
assert.doesNotMatch(frontend, /function repairKindleFractions\(root\)/);
assert.doesNotMatch(frontend, /function repairKindleLimits\(root\)/);
assert.doesNotMatch(frontend, /function repairKindleOperatorBaselines\(root\)/);
assert.doesNotMatch(frontend, /function repairKindleFractionBaselines\(root\)/);
assert.doesNotMatch(frontend, /function repairKindleNestedVlistBaselines/);
assert.doesNotMatch(frontend, /getElementsByClassName\("op-limits"\)/);
assert.match(frontend, /function isInsideMathStructure\(node, className\)/);
assert.match(frontend, /repairKindleScripts\(root\)[\s\S]*?isInsideMathStructure\(node, "mfrac"\)/);
assert.match(frontend, /function repairKindleVlistBaselines\(root\)/);
assert.match(frontend, /getElementsByClassName\("vlist-t2"\)/);
assert.match(frontend, /\.style\.verticalAlign = "-" \+ depth \+ "em"/);
assert.match(frontend, /Mesquite ignores the second row when deriving the baseline/);
assert.doesNotMatch(frontend, /repairKindleFractionClearance/);
assert.match(frontend, /function repairKindleMath\(root\)[\s\S]*?repairKindleScripts\(root\);[\s\S]*?repairKindleVlistBaselines\(root\);[\s\S]*?\n\s*}/);
assert.doesNotMatch(appCss, /potion-(?:fraction|frac-|op-)/);
assert.match(appCss, /\.katex \.mfrac \.frac-line\s*{\s*border-bottom-width:\s*2px;/);
assert.doesNotMatch(appCss, /\.mfrac \.frac-line[\s\S]*?!important/);
assert.match(frontend, /function repairMathNearViewport\(\)/);
assert.match(frontend, /root\.scrollTop \+ root\.clientHeight \* 2\.5/);
assert.match(frontend, /repaired >= 16/);
assert.match(frontend, /function scheduleMathRepair\(delay\)/);

// The mechanical loading iris is driven by the existing busy state, uses a
// single ES5 timeout loop, and occupies a fixed non-reflowing header slot.
assert.match(index, /<canvas id="busy-iris" class="busy-iris" width="50" height="50"/);
assert.match(frontend, /function drawIrisFrame\(\)/);
assert.match(frontend, /for \(i = 0; i < 6; \+\+i\)/);
assert.match(frontend, /radii = \[5, 8, 12, 16, 16, 12, 8, 5\]/);
assert.match(frontend, /irisRotation \+= Math\.PI \/ 18/);
assert.match(frontend, /irisTimer = window\.setTimeout\(advanceIris, 275\)/);
assert.match(frontend, /function setBusy\(value\)/);
assert.doesNotMatch(frontend, /requestAnimationFrame/);
assert.equal((frontend.match(/busy\s*=\s*(?:true|false)/g) || []).length, 1,
  "only the initial declaration may assign busy directly");
assert.match(appCss, /\.busy-iris\s*{[\s\S]*position:\s*fixed;[\s\S]*width:\s*50px;[\s\S]*visibility:\s*hidden;/);

// Selection formatting remains a small ES5 interaction: one block only,
// exact UTF-16 offsets, block PATCH through potiond, and no page reload.
assert.match(index, /id="selection-menu"[\s\S]*data-format="highlight"[\s\S]*data-format="bold"[\s\S]*data-format="underline"[\s\S]*data-format="clear"/);
assert.match(frontend, /startContent !== endContent/);
assert.match(frontend, /logicalNodeText\(before\.cloneContents\(\)\)\.length/);
assert.match(frontend, /\/api\/pages\/" \+ encodeURIComponent\(pageId\) \+ "\/format/);
assert.match(frontend, /function applySelectionLocally\(state, format, enabled\)/);
assert.match(frontend, /function selectionFormatState\(content, start, end\)/);
assert.match(frontend, /function logicalNodeText\(node\)/);
assert.match(frontend, /getElementsByClassName\("notion-page-link"\)/);
assert.match(frontend, /node\.tagName\.toLowerCase\(\) === "br"[\s\S]*return "\\n"/);
assert.match(frontend, /getAttribute\("data-potion-atomic"\)[\s\S]*return "\\ufffc"/);
assert.match(frontend, /logicalNodeText\(before\.cloneContents\(\)\)\.length/);
assert.match(frontend, /blockText = logicalNodeText\(startContent\)/);
assert.doesNotMatch(frontend, /blockText:\s*startContent\.textContent/);
assert.match(frontend, /result && result\.enabled/);
assert.match(frontend, /format === "clear"[\s\S]*tag === "strong"[\s\S]*classes\.indexOf\(" notion-color "\)/);
assert.match(frontend, /formatSaveBusy/);
assert.doesNotMatch(frontend,
  /function formatSelection\(format\)[\s\S]*?setBusy\(true\)[\s\S]*?\/\* OPTIMISTIC_FORMATTING_END \*\//,
  "formatting must not put the entire application into the busy state");

// Formatting mutates the selected block and closes its popup before the XHR
// completes. One write is serialized at a time; a failure restores the exact
// sanitized block markup, including nested formatting and links.
const optimisticMatch = frontend.match(
  /\/\* OPTIMISTIC_FORMATTING_BEGIN \*\/([\s\S]*?)\/\* OPTIMISTIC_FORMATTING_END \*\//);
assert.ok(optimisticMatch, "missing optimistic-formatting test boundary");
function optimisticContext() {
  const calls = [];
  const requests = [];
  const root = {};
  const content = {
    innerHTML: '<a href="https://example.com"><strong class="notion-color">linked</strong></a>',
    parentNode: root
  };
  const state = {
    content: content,
    range: {},
    editableIndex: 2,
    start: 1,
    end: 5,
    blockText: "linked text",
    selectedText: "inke",
    formats: {bold: false, underline: false, highlight: false}
  };
  const context = {
    selectionState: state,
    currentPageId: "page-id",
    busy: false,
    formatSaveBusy: false,
    id: function(name) { assert.equal(name, "page-content"); return root; },
    applySelectionLocally: function(localState, format, enabled) {
      calls.push("apply:" + format + ":" + enabled);
      localState.content.innerHTML = "optimistic";
      return true;
    },
    clearSelectionMenu: function(clearNative) { calls.push("clear:" + clearNative); },
    collectReadingBlocks: function() { calls.push("blocks"); },
    applyNightPageAppearance: function() { calls.push("night"); },
    updateScroll: function() { calls.push("scroll"); },
    setSelectionButtonsDisabled: function(value) { calls.push("disabled:" + value); },
    warning: function(message) { calls.push("warning:" + message); },
    request: function(method, url, body, callback) {
      calls.push("request");
      requests.push({method: method, url: url, body: body, callback: callback});
    },
    logicalRange: function() { calls.push("range"); return {}; },
    encodeURIComponent: encodeURIComponent
  };
  vm.createContext(context);
  vm.runInContext(optimisticMatch[1], context);
  return {context: context, calls: calls, requests: requests, content: content, state: state};
}

const optimisticFailure = optimisticContext();
optimisticFailure.context.formatSelection("bold");
assert.equal(optimisticFailure.content.innerHTML, "optimistic",
  "the selected DOM changes before the backend responds");
assert.ok(optimisticFailure.calls.indexOf("clear:true") < optimisticFailure.calls.indexOf("request"),
  "the selection popup closes before the background request starts");
assert.equal(optimisticFailure.requests.length, 1);
optimisticFailure.context.formatSelection("underline");
assert.equal(optimisticFailure.requests.length, 1,
  "rapid formatting actions cannot create overlapping writes");
optimisticFailure.requests[0].callback("Notion rejected the edit", null);
assert.equal(optimisticFailure.content.innerHTML,
  '<a href="https://example.com"><strong class="notion-color">linked</strong></a>',
  "a failed write restores the exact prior nested markup and link");
assert.ok(optimisticFailure.calls.indexOf(
  "warning:Formatting could not be saved and was reverted. Notion rejected the edit") >= 0);
assert.equal(optimisticFailure.context.formatSaveBusy, false);

const optimisticSuccess = optimisticContext();
optimisticSuccess.context.formatSelection("underline");
optimisticSuccess.requests[0].callback(null, {enabled: true});
assert.equal(optimisticSuccess.content.innerHTML, "optimistic",
  "a successful matching write leaves the optimistic DOM unchanged");
assert.equal(optimisticSuccess.calls.filter(function(call) {
  return call.indexOf("apply:") === 0;
}).length, 1, "success does not apply the same formatting twice");
assert.doesNotMatch(frontend, /localStorage|sessionStorage/);
assert.doesNotMatch(frontend, /location\.reload/);
assert.match(appCss, /\.selection-menu\s*{[\s\S]*position:\s*fixed;[\s\S]*z-index:\s*500;/);
assert.match(appCss, /\.selection-menu button\.selection-active/);
assert.match(appCss, /\.page-content a,[\s\S]*?\.page-content u[\s\S]*?padding-bottom:\s*5px;[\s\S]*?background-image:\s*url\("data:image\/png;base64,/);
assert.match(appCss, /background-position:\s*left bottom;[\s\S]*?background-repeat:\s*repeat-x;/);
assert.match(appCss, /\.page-content a\s*{\s*color:\s*#111;/);
assert.doesNotMatch(appCss, /\.page-content u\s*{[^}]*color:/);
assert.match(appCss, /\.page-content a u\s*{[^}]*padding-bottom:\s*0;[^}]*background-image:\s*none;/);
assert.match(appCss, /\.night-mode \.page-content a\s*{\s*color:\s*#eee;/);
assert.match(appCss, /\.night-mode \.page-content a u\s*{[^}]*background-image:\s*none;/);
assert.match(config, /<kindle:param name="tap" value="no"\/>/);
assert.match(config, /<kindle:param name="multi_tap" value="no"\/>/);
assert.match(config, /<kindle:param name="hold" value="no"\/>/);
assert.doesNotMatch(config, /<kindle:param name="drag"/);
assert.doesNotMatch(config, /<kindle:param name="swipe"/);
assert.match(frontend, /document\.caretRangeFromPoint/);
assert.match(frontend, /selectionHoldTimer = window\.setTimeout\(beginCustomSelection, 700\)/);
assert.match(frontend, /elapsed >= 700/);
assert.match(frontend, /function moveCustomSelection\(event\)/);
assert.match(frontend,
  /function finishSelectionHold\(event\)[\s\S]*if \(held\) beginCustomSelection\(\);[\s\S]*if \(selectionHoldActive\) \{[\s\S]*moveCustomSelection\(event\);/);
const finishMatch = frontend.match(
  /(function finishSelectionHold\(event\) \{[\s\S]*?\n    \})\n\n    function setSelectionButtonsDisabled/);
assert.ok(finishMatch, "missing finishSelectionHold");
const finishCalls = [];
const finishContext = {
  selectionHoldStartedAt: Date.now() - 800,
  selectionHoldTimer: 1,
  selectionHoldActive: false,
  selectionDragActive: false,
  selectionDragInspectTimer: 17,
  selectionSuppressClick: true,
  clearSelectionHoldTimer: function() { finishContext.selectionHoldTimer = null; },
  clearDragSelectionInspection: function() {
    finishCalls.push("clear-drag");
    finishContext.selectionDragInspectTimer = null;
  },
  beginCustomSelection: function() {
    finishCalls.push("begin");
    finishContext.selectionHoldActive = true;
  },
  moveCustomSelection: function(event) {
    finishCalls.push("move:" + event.marker);
    finishContext.selectionDragInspectTimer = 18;
  },
  inspectSelection: function(source) { finishCalls.push("inspect:" + source); },
  window: { setTimeout: function() {} },
  Date: Date
};
vm.createContext(finishContext);
vm.runInContext(finishMatch[1], finishContext);
finishContext.finishSelectionHold({ marker: "release" });
assert.deepEqual(finishCalls,
  ["clear-drag", "begin", "move:release", "clear-drag", "inspect:SELECT"],
  "a delayed hold timer must extend to the release point instead of collapsing to one word");
assert.equal(finishContext.selectionDragInspectTimer, null,
  "release cancels both the old timer and any inspection scheduled by the final move");
assert.match(frontend, /document\.onmousemove = moveCustomSelection;/);
assert.match(frontend, /document\.ontouchmove = moveCustomSelection;/);
assert.match(frontend,
  /document\.onmouseup = function\(event\)[\s\S]*finishSelectionHold\(event \|\| window\.event\);[\s\S]*scheduleSelectionInspection\(\);/);
assert.doesNotMatch(frontend, /id\("page-content"\)\.onmousemove/);
assert.doesNotMatch(frontend, /id\("page-content"\)\.onmouseup/);
assert.match(frontend,
  /id\("page-content"\)\.onscroll = function\(\) \{\s*if \(!selectionHoldActive\) clearSelectionMenu\(true\);/);
assert.match(frontend,
  /function applyAppearance\(persist\) \{\s*clearSelectionMenu\(true\);/);
assert.match(frontend, /source === "NATIVE" && selectionHoldTimer !== null/);
assert.match(frontend, /selectionDragActive/);
assert.match(frontend, /point\.x < rect\.left/);
assert.doesNotMatch(frontend, /selectionDebug|\/api\/debug\/selection/);
assert.doesNotMatch(index, /selection-debug/);

// Updating the native Range stays synchronous for every drag event, while
// expensive logical-offset/format/menu inspection is limited to one timer.
const dragThrottleMatch = frontend.match(
  /\/\* DRAG_SELECTION_THROTTLE_BEGIN \*\/([\s\S]*?)\/\* DRAG_SELECTION_THROTTLE_END \*\//);
assert.ok(dragThrottleMatch, "missing drag-selection throttle test boundary");
const dragThrottleCalls = [];
const dragThrottleTimers = [];
const dragThrottleContext = {
  selectionDragInspectTimer: null,
  selectionHoldActive: true,
  SELECTION_DRAG_INSPECT_MS: 80,
  inspectSelection: function(source) { dragThrottleCalls.push("inspect:" + source); },
  window: {
    setTimeout: function(callback, delay) {
      dragThrottleCalls.push("timer:" + delay);
      dragThrottleTimers.push({callback: callback, cancelled: false});
      return dragThrottleTimers.length;
    },
    clearTimeout: function(timer) {
      dragThrottleCalls.push("clear:" + timer);
      dragThrottleTimers[timer - 1].cancelled = true;
    }
  }
};
vm.createContext(dragThrottleContext);
vm.runInContext(dragThrottleMatch[1], dragThrottleContext);
dragThrottleContext.scheduleDragSelectionInspection();
dragThrottleContext.scheduleDragSelectionInspection();
assert.deepEqual(dragThrottleCalls, ["timer:80"],
  "rapid moves create at most one inspection timer");
dragThrottleTimers[0].callback();
assert.deepEqual(dragThrottleCalls, ["timer:80", "inspect:DRAG"],
  "the timer inspects an active drag");
dragThrottleContext.scheduleDragSelectionInspection();
dragThrottleContext.clearDragSelectionInspection();
assert.equal(dragThrottleContext.selectionDragInspectTimer, null,
  "clearing selection cancels the pending drag inspection");
assert.equal(dragThrottleTimers[1].cancelled, true);
dragThrottleContext.selectionHoldActive = false;
dragThrottleContext.scheduleDragSelectionInspection();
dragThrottleTimers[2].callback();
assert.equal(dragThrottleCalls.filter(function(call) { return call === "inspect:DRAG"; }).length, 1,
  "an inactive delayed callback cannot overwrite final release state");
assert.match(frontend,
  /function clearSelectionMenu\(clearNative\)[\s\S]*clearDragSelectionInspection\(\)/,
  "clearing selection cancels pending drag inspection");

const moveMatch = frontend.match(
  /(function moveCustomSelection\(event\) \{[\s\S]*?\n    \})\n\n    function finishSelectionHold/);
assert.ok(moveMatch, "missing moveCustomSelection");
const moveCalls = [];
const anchorNode = {
  compareDocumentPosition: function() { return 0; }
};
const endpointNode = {};
const moveContext = {
  selectionHoldActive: true,
  selectionDragActive: true,
  selectionHoldTimer: null,
  selectionHoldX: 10,
  selectionHoldY: 10,
  selectionAnchor: {node: anchorNode, start: 1, end: 4},
  eventPoint: function() { return {x: 50, y: 50}; },
  caretAtPoint: function() { return {startContainer: endpointNode, startOffset: 8}; },
  selectionAncestor: function() { return "same-content"; },
  scheduleDragSelectionInspection: function() { moveCalls.push("schedule"); },
  inspectSelection: function(source) { moveCalls.push("inspect:" + source); },
  document: { createRange: function() { return {
    setStart: function() {}, setEnd: function() {}
  }; } },
  window: { getSelection: function() { return {
    removeAllRanges: function() { moveCalls.push("remove"); },
    addRange: function() { moveCalls.push("add"); }
  }; } },
  Math: Math,
  Date: Date
};
vm.createContext(moveContext);
vm.runInContext(moveMatch[1], moveContext);
moveContext.moveCustomSelection({preventDefault: function() { moveCalls.push("prevent"); }});
assert.deepEqual(moveCalls, ["prevent", "remove", "add", "schedule"],
  "every move updates the native Range immediately without synchronous inspection");
assert.doesNotMatch(moveMatch[1], /inspectSelection\("DRAG"\)/);

// Native triple-click ranges may include the paragraph boundary; Potion
// normalizes that one-block selection. Double-click uses logical offsets so a
// sentence can cross inline formatting nodes without losing exact offsets.
const multiTapMatch = frontend.match(
  /\/\* MULTI_TAP_SELECTION_LOGIC_BEGIN \*\/([\s\S]*?)\/\* MULTI_TAP_SELECTION_LOGIC_END \*\//);
assert.ok(multiTapMatch, "missing multi-tap selection logic boundary");
const multiTapContext = {};
vm.createContext(multiTapContext);
vm.runInContext(multiTapMatch[1], multiTapContext);
assert.deepEqual(
  JSON.parse(JSON.stringify(multiTapContext.sentenceBounds(
    "First sentence. Second bold and linked sentence! Third?", 25))),
  {start: 16, end: 48},
  "double-click selects the complete containing sentence");
assert.deepEqual(
  JSON.parse(JSON.stringify(multiTapContext.sentenceBounds(
    "سلام عرض می‌کنم؟ جمله دوم.", 5))),
  {start: 0, end: 16},
  "sentence selection recognizes Persian question punctuation");
assert.match(frontend, /function logicalBoundary\(content, wanted\)/);
assert.match(frontend, /function setLogicalSelection\(content, start, end\)/);
assert.match(frontend, /function normalizeWholeBlockRange\(selection, range\)/);
assert.match(frontend,
  /if \(!startContent \|\| startContent !== endContent\) \{[\s\S]*normalizeWholeBlockRange/);
assert.match(frontend,
  /detail >= 3[\s\S]*selectMultiTap\(event, true\)[\s\S]*detail === 2[\s\S]*selectMultiTap\(event, false\)/);
assert.match(frontend, /id\("page-content"\)\.ondblclick = function\(event\)/);

const transientPositionMatch = frontend.match(
  /(function rememberCurrentReadingPosition\(\) \{[\s\S]*?\n    \})\n\n    \/\* READING_POSITION_MAINTENANCE_BEGIN/);
assert.ok(transientPositionMatch, "missing transient reading-position updater");
const transientTimers = [];
let transientReads = 0;
let transientViewportRestores = 0;
const transientRoot = {clientWidth: 600};
const transientContext = {
  positionRestoring: false,
  currentPageId: "page",
  readingBlocks: [{}],
  transientReadingPosition: null,
  transientPositionTimer: null,
  readingViewportWidth: 600,
  TRANSIENT_POSITION_UPDATE_MS: 100,
  id: function(name) {
    return name === "page-content" ? transientRoot : {className: ""};
  },
  scheduleViewportReadingRestore: function() {
    transientViewportRestores++;
  },
  currentReadingPosition: function() {
    transientReads++;
    return {blockIndex: transientReads, blockFraction: 0};
  },
  window: {setTimeout: function(callback, delay) {
    const timer = {callback, delay};
    transientTimers.push(timer);
    return timer;
  }}
};
vm.createContext(transientContext);
vm.runInContext(transientPositionMatch[1], transientContext);
transientContext.scheduleTransientReadingPositionUpdate();
transientContext.scheduleTransientReadingPositionUpdate();
assert.equal(transientReads, 1,
  "the first scroll remembers its logical position immediately");
assert.equal(transientTimers.length, 1,
  "rapid scroll events share one in-memory update timer");
assert.equal(transientTimers[0].delay, 100);
transientTimers[0].callback();
assert.equal(transientReads, 2,
  "the throttle also captures the latest position at its trailing edge");
transientRoot.clientWidth = 800;
transientContext.scheduleTransientReadingPositionUpdate();
assert.equal(transientReads, 2,
  "a reflow-induced scroll cannot overwrite the pre-rotation anchor");
assert.equal(transientViewportRestores, 1,
  "a changed viewport width starts semantic restoration instead");
assert.match(frontend,
  /id\("page-content"\)\.onscroll = function\(\)[\s\S]*scheduleTransientReadingPositionUpdate\(\)/);

// Reading-position restoration remains anchored while asynchronous layout
// above the saved block settles. The first correction reveals the page; later
// image/math completions merely reapply the same logical block + fraction.
const readingRestoreMatch = frontend.match(
  /\/\* READING_POSITION_MAINTENANCE_BEGIN \*\/([\s\S]*?)\/\* READING_POSITION_MAINTENANCE_END \*\//);
assert.ok(readingRestoreMatch, "missing reading-position maintenance boundary");

function readingRestoreFixture() {
  const timers = [];
  const root = {
    scrollTop: 0,
    scrollHeight: 2000,
    clientHeight: 600,
    clientWidth: 600,
    style: {visibility: "hidden"}
  };
  const block = {
    offsetTop: 300,
    offsetHeight: 200,
    offsetParent: root
  };
  const context = {
    readingBlocks: [block],
    positionRestoreTimer: null,
    positionRestoring: false,
    positionRestoreAnchor: null,
    transientReadingPosition: null,
    transientPositionTimer: null,
    viewportRestoreTimer: null,
    readingViewportWidth: 600,
    POSITION_RESTORE_QUIET_MS: 400,
    currentPageId: "page",
    mathRepairTimer: null,
    imageNodes: [],
    window: {
      setTimeout: function(callback, delay) {
        const timer = {callback, delay, cancelled: false};
        timers.push(timer);
        return timer;
      },
      clearTimeout: function(timer) {
        if (timer) timer.cancelled = true;
      }
    },
    id: function(name) {
      if (name === "page-content") return root;
      if (name === "reader-view") return {className: ""};
      throw new Error("unexpected id " + name);
    },
    mathTop: function(node) { return node.offsetTop || 0; },
    updateScroll: function() {},
    scheduleMathRepair: function() {},
    scheduleImageLoad: function() {},
    currentReadingPosition: function() {
      return {blockIndex: 0, blockFraction: 32768};
    },
    Math,
    parseInt,
    isFinite
  };
  vm.createContext(context);
  vm.runInContext(readingRestoreMatch[1], context);
  return {context, timers, root, block};
}

const stableRestore = readingRestoreFixture();
assert.equal(stableRestore.context.beginReadingRestore(
  {blockIndex: 0, blockFraction: 32768}, true), true);
assert.equal(stableRestore.root.scrollTop, 400,
  "initial restore applies the saved block fraction");
assert.equal(stableRestore.root.style.visibility, "",
  "the page is revealed immediately after the initial correction");
assert.equal(stableRestore.context.positionRestoring, true,
  "transient corrected scroll positions remain protected from persistence");
assert.equal(stableRestore.timers[0].delay, 400);

stableRestore.root.scrollTop = 120;
const relevantImage = {
  offsetTop: 100,
  offsetHeight: 20,
  offsetParent: stableRestore.root,
  complete: false,
  _potionImageLoading: true,
  _potionImageRequested: true,
  _potionImageAttempts: 1,
  getAttribute: function() { return "image"; }
};
stableRestore.context.imageNodes.push(relevantImage);
stableRestore.context.maintainReadingRestore(relevantImage, false);
assert.equal(stableRestore.root.scrollTop, 400,
  "a relevant image completion reapplies the active anchor");
assert.equal(stableRestore.timers.filter(function(timer) {
  return !timer.cancelled;
}).length, 1, "layout changes share one debounced quiet timer");

let activeTimer = stableRestore.timers[stableRestore.timers.length - 1];
activeTimer.callback();
assert.equal(stableRestore.context.positionRestoring, true,
  "restoration stays active while relevant layout remains pending");
relevantImage._potionImageLoading = false;
relevantImage.complete = true;
activeTimer = stableRestore.timers[stableRestore.timers.length - 1];
activeTimer.callback();
assert.equal(stableRestore.context.positionRestoring, false,
  "restoration ends after the page has been quiet");
assert.equal(stableRestore.context.positionRestoreAnchor, null);

const irrelevantRestore = readingRestoreFixture();
irrelevantRestore.context.beginReadingRestore(
  {blockIndex: 0, blockFraction: 32768}, false);
irrelevantRestore.root.scrollTop = 123;
irrelevantRestore.context.maintainReadingRestore({
  offsetTop: 900,
  offsetHeight: 20,
  offsetParent: irrelevantRestore.root
}, false);
assert.equal(irrelevantRestore.root.scrollTop, 123,
  "layout changes below the restore anchor do not cause correction");

const rotatedRestore = readingRestoreFixture();
rotatedRestore.context.transientReadingPosition = {
  blockIndex: 0,
  blockFraction: 32768
};
rotatedRestore.context.currentReadingPosition = function() {
  return {blockIndex: 0, blockFraction: 0};
};
rotatedRestore.context.scheduleViewportReadingRestore();
rotatedRestore.block.offsetTop = 500;
rotatedRestore.timers[0].callback();
assert.equal(rotatedRestore.root.scrollTop, 600,
  "a captured logical viewport anchor is reapplied after rotation layout");
assert.equal(rotatedRestore.context.positionRestoring, true,
  "rotation correction also remains anchored through asynchronous relayout");

assert.match(frontend,
  /function imageFinished\(image, loaded\)[\s\S]*maintainReadingRestore\(image, false\)/);
assert.match(frontend,
  /function repairMathNearViewport\(\)[\s\S]*maintainReadingRestore\(null, true\)/);
assert.match(frontend,
  /function saveCurrentReadingPosition\(done\)[\s\S]*if \(positionRestoring \|\|/);
assert.match(frontend,
  /window\.onorientationchange = function\(\) \{\s*scheduleViewportReadingRestore\(\)/);
assert.match(frontend,
  /window\.onresize = function\(\) \{[\s\S]*scheduleViewportReadingRestore\(\)/);

console.log("Potion frontend native-math contract tests passed");
