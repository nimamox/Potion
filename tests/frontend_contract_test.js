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

assert.doesNotMatch(frontend, /window\.katex|katex\.render|loadKatex|katexState|data-expr/);
assert.match(index, /vendor\/katex\/katex\.min\.css\?v=0\.16\.25-native/);
assert.equal(fs.existsSync(javascriptAsset), false);
assert.ok(index.indexOf("vendor/katex/katex.min.css") < index.indexOf("app.css"),
  "application compatibility CSS must load after KaTeX CSS");
assert.doesNotMatch(index, /class="eyebrow"/);
assert.match(index, /<div class="section-heading">\s*<h2>Notion Pages<\/h2>\s*<div class="search-row">/);
assert.match(index, /id="sort-opened"[^>]*aria-pressed="true">Opened<\/button><button id="sort-edited"[^>]*>Edited<\/button>/);
assert.match(index, /id="page-pin"[^>]*aria-label="Pin page"[^>]*>&#9734;<\/button>\s*<button id="pages-home"/);
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
assert.match(frontend, /\/api\/pages\/" \+ encodeURIComponent\(currentPageId\) \+ "\/format/);
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
assert.match(frontend, /source === "NATIVE" && selectionHoldTimer !== null/);
assert.match(frontend, /selectionDragActive/);
assert.match(frontend, /point\.x < rect\.left/);
assert.doesNotMatch(frontend, /selectionDebug|\/api\/debug\/selection/);
assert.doesNotMatch(index, /selection-debug/);

console.log("Potion frontend native-math contract tests passed");
