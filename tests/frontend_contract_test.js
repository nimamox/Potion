"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");

const frontend = fs.readFileSync(process.argv[2], "utf8");
const index = fs.readFileSync(process.argv[3], "utf8");
const javascriptAsset = process.argv[4];
const katexCss = fs.readFileSync(process.argv[5], "utf8");
const fontDirectory = process.argv[6];
const appCss = fs.readFileSync(process.argv[7], "utf8");

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

assert.match(frontend, /function repairKindleScripts\(root\)/);
assert.doesNotMatch(frontend, /function repairKindleFractions\(root\)/);
assert.doesNotMatch(frontend, /function repairKindleLimits\(root\)/);
assert.match(frontend, /function repairKindleOperatorBaselines\(root\)/);
assert.match(frontend, /getElementsByClassName\("op-limits"\)/);
assert.match(frontend, /getElementsByClassName\("vlist-t2"\)/);
assert.match(frontend, /table\.style\.verticalAlign = "-" \+ depth \+ "em"/);
assert.match(frontend, /function repairKindleFractionBaselines\(root\)/);
assert.match(frontend, /getElementsByClassName\("mfrac"\)/);
assert.match(frontend, /Like op-limits, a KaTeX fraction stores its depth/);
assert.match(frontend, /function isInsideMathStructure\(node, className\)/);
assert.match(frontend, /repairKindleScripts\(root\)[\s\S]*?isInsideMathStructure\(node, "mfrac"\)/);
assert.match(frontend, /repairKindleOperatorBaselines\(root\)[\s\S]*?isInsideMathStructure\(nodes\[i\]\.parentNode, "mfrac"\)/);
assert.match(frontend, /function repairKindleNestedVlistBaselines\(root, ownerClass\)/);
assert.match(frontend, /repairKindleNestedVlistBaselines\(root, "sqrt"\)/);
assert.match(frontend, /repairKindleNestedVlistBaselines\(root, "mtable"\)/);
assert.doesNotMatch(frontend, /repairKindleFractionClearance/);
assert.match(frontend, /function repairKindleMath\(root\)[\s\S]*?repairKindleScripts\(root\);[\s\S]*?repairKindleOperatorBaselines\(root\);[\s\S]*?repairKindleFractionBaselines\(root\);[\s\S]*?repairKindleNestedVlistBaselines\(root, "sqrt"\);[\s\S]*?repairKindleNestedVlistBaselines\(root, "mtable"\);[\s\S]*?\n\s*}/);
assert.doesNotMatch(appCss, /potion-(?:fraction|frac-|op-)/);
assert.match(appCss, /\.katex \.mfrac \.frac-line\s*{\s*border-bottom-width:\s*2px;/);
assert.doesNotMatch(appCss, /\.mfrac \.frac-line[\s\S]*?!important/);
assert.match(frontend, /function repairMathNearViewport\(\)/);
assert.match(frontend, /root\.scrollTop \+ root\.clientHeight \* 2\.5/);
assert.match(frontend, /repaired >= 16/);
assert.match(frontend, /function scheduleMathRepair\(delay\)/);

console.log("Potion frontend native-math contract tests passed");
