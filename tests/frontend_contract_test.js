"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");

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
