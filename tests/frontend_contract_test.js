"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");

const frontend = fs.readFileSync(process.argv[2], "utf8");
const index = fs.readFileSync(process.argv[3], "utf8");
const javascriptAsset = process.argv[4];

assert.doesNotMatch(frontend, /window\.katex|katex\.render|loadKatex|katexState|data-expr/);
assert.match(index, /vendor\/katex\/katex\.min\.css\?v=0\.16\.25-native/);
assert.equal(fs.existsSync(javascriptAsset), false);
assert.match(frontend, /function repairKindleScripts\(root\)/);
assert.match(frontend, /function repairKindleFractions\(root\)/);
assert.doesNotMatch(frontend, /function repairKindleLimits\(root\)/);
assert.match(frontend, /function repairMathNearViewport\(\)/);
assert.match(frontend, /root\.scrollTop\+root\.clientHeight\*2\.5/);
assert.match(frontend, /repaired>=16/);
assert.match(frontend, /function scheduleMathRepair\(delay\)/);

console.log("Potion frontend native-math contract tests passed");
