"use strict";

const assert = require("node:assert/strict");
const crypto = require("node:crypto");
const fs = require("node:fs");
const path = require("node:path");
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
const notionClient = fs.readFileSync(process.argv[18], "utf8");
const vendorDirectory = path.dirname(path.dirname(bionicFontDirectory));
const emojiFontDirectory = path.join(vendorDirectory, "noto-emoji", "fonts");
const emojiFontPath = path.join(emojiFontDirectory, "NotoEmoji-Regular.ttf");
const emojiFont = fs.readFileSync(emojiFontPath);

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
  /id="appearance"[^>]*class="[^"]*reader-only-action-hidden[^"]*"[^>]*aria-pressed="false"[^>]*>Aa<\/button>/);
assert.doesNotMatch(index, /id="font-plus"|id="font-minus"/);
assert.match(appCss,
  /\.header-actions \.reader-only-action-hidden\s*\{[\s\S]*visibility:\s*hidden/);
assert.match(index,
  /id="appearance-font-previous"[^>]*>&#10162;<\/button>[\s\S]*id="appearance-font-next"[^>]*>&#10162;<\/button>[\s\S]*id="appearance-close"[^>]*>&#10005;<\/button>/);
assert.match(index, /id="appearance-font-dots"/);
assert.match(index,
  /id="appearance-word-spacing"[\s\S]*data-value="normal">Normal[\s\S]*data-value="plus">\+[\s\S]*data-value="plusplus">\+\+[\s\S]*data-value="plusplusplus">\+\+\+/);
assert.match(index,
  /id="appearance-line-spacing"[\s\S]*data-value="normal">Normal[\s\S]*data-value="plus">\+[\s\S]*data-value="plusplus">\+\+[\s\S]*data-value="plusplusplus">\+\+\+/);
assert.match(index,
  /id="appearance-bionic-reading"[\s\S]*data-value="off">Off[\s\S]*data-value="on">On/);
const appearanceSizes = index.match(/id="appearance-font-sizes"[\s\S]*?<\/div>/)[0];
assert.equal((appearanceSizes.match(/data-scale=/g) || []).length, 8);
const settingsMarkup = index.match(/id="settings-dialog"[\s\S]*?id="logout-dialog"/)[0];
assert.doesNotMatch(settingsMarkup, /Page font|font-sizes|Bionic Reading|<h3>Reading<\/h3>/);
assert.match(appCss, /\.header-actions\s*{[\s\S]*width:\s*365px;[\s\S]*font-size:\s*0;/);
assert.match(appCss,
  /\.header-actions button,[\s\S]*?\.header-actions \.header-button\.icon-button\s*{[\s\S]*width:\s*69px;[\s\S]*height:\s*69px;/);
assert.match(appCss,
  /\.appearance-symbol,[\s\S]*\.appearance-font-dots\s*\{[\s\S]*font-family:\s*"Code2000"/);
assert.match(appCss,
  /\.appearance-font-previous\s*\{[\s\S]*-webkit-transform:\s*rotate\(180deg\)/);
assert.doesNotMatch(appCss, /\.appearance-sheet[^\{]*\{[^}]*animation|\.appearance-sheet[^\{]*\{[^}]*transition/);
assert.match(appCss,
  /\.appearance-sheet\s*\{[\s\S]*top:\s*0;[\s\S]*bottom:\s*0;[\s\S]*background:\s*transparent/);
assert.match(appCss,
  /\.appearance-sheet-inner\s*\{[\s\S]*width:\s*96%;[\s\S]*height:\s*590px/);
const codeSizeRow = index.match(
  /<div class="appearance-setting-row">\s*<strong>CODE SIZE<\/strong>([\s\S]*?)<\/div>\s*<\/div>/);
assert.ok(codeSizeRow, "CODE SIZE row in the appearance sheet");
assert.match(codeSizeRow[1],
  /id="appearance-code-size"[\s\S]*id="appearance-code-smaller"[^>]*>Smaller<\/button>[\s\S]*id="appearance-code-larger"[^>]*>Larger<\/button>/);
assert.doesNotMatch(codeSizeRow[1], /aria-pressed|data-value/,
  "code size buttons are actions, not a persistent selection");
assert.ok(
  index.indexOf('id="appearance-line-spacing"') <
    index.indexOf('id="appearance-code-size"') &&
    index.indexOf('id="appearance-code-size"') <
    index.indexOf('id="appearance-bionic-reading"'),
  "CODE SIZE sits between LINE SPACING and BIONIC READING");
assert.match(runKindle, /'supportedOrientation','UDLR'/);
assert.match(runKindle, /LD_PRELOAD=.*libmesquite-whisper-touch\.so \/usr\/bin\/mesquite/);
assert.match(runKindle,
  /FONTCONFIG_FILE=\$POTION_ROOT\/etc\/fontconfig-potion\.conf/);
assert.match(potionFontconfig, /<include ignore_missing="no">\/etc\/fonts\/fonts\.conf<\/include>/);
assert.match(potionFontconfig,
  /<dir>\/mnt\/us\/potion\/share\/potion\/vendor\/fast-font\/fonts<\/dir>/);
assert.match(potionFontconfig,
  /<dir>\/mnt\/us\/potion\/share\/potion\/vendor\/noto-emoji\/fonts<\/dir>/);
assert.doesNotMatch(potionFontconfig, /\/usr\/share\/fonts|\/etc\/fonts\/conf\.d/);
assert.deepEqual(fs.readdirSync(bionicFontDirectory).sort(), [
  "PotionFastSans-Bold.otf",
  "PotionFastSans-BoldItalic.otf",
  "PotionFastSans-Italic.otf",
  "PotionFastSans-Regular.otf"
]);
assert.deepEqual(fs.readdirSync(emojiFontDirectory).sort(), ["NotoEmoji-Regular.ttf"]);
assert.equal(
  crypto.createHash("sha256").update(emojiFont).digest("hex"),
  "415dc6290378574135b64c808dc640c1df7531973290c4970c51fdeb849cb0c5"
);
const emojiTables = new Set();
for (let i = 0, count = emojiFont.readUInt16BE(4); i < count; ++i)
  emojiTables.add(emojiFont.toString("ascii", 12 + i * 16, 16 + i * 16));
assert.ok(emojiTables.has("glyf"), "Noto Emoji must use monochrome TrueType outlines");
for (const table of ["CBDT", "CBLC", "COLR", "CPAL", "SVG ", "fvar"])
  assert.equal(emojiTables.has(table), false,
    `Noto Emoji must not contain ${table}`);
assert.match(whisperTouch, /win_mgr_utils_new_application_name/);
assert.match(whisperTouch, /win_mgr_utils_add_is_wisper_touch_supported/);
assert.match(whisperTouch, /win_mgr_utils_new_name\(0, "application"\)/);
assert.match(whisperTouch, /win_mgr_utils_add_is_wisper_touch_supported\(name, 1\)/);
assert.doesNotMatch(frontend, /\/api\/input|\/api\/simulator\/input|pollInput|waitForInput/);
assert.doesNotMatch(httpServer, /\/api\/input|\/api\/simulator\/input|gpiokey|\/dev\/input|input_event/);
assert.match(httpServer, /select\(highest \+ 1, &set, nullptr, nullptr, nullptr\)/);
assert.match(httpServer,
  /retrieve_registered_image\([\s\S]*?retrieve_image\(url, result, download_error,[\s\S]*?CachePolicy::proxied_image/);
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
assert.match(simulatorHostFonts,
  /font-family:\s*"Noto Emoji";[\s\S]*url\("\/vendor\/noto-emoji\/fonts\/NotoEmoji-Regular\.ttf"\) format\("truetype"\)/);
assert.doesNotMatch(appCss, /@font-face[\s\S]*Potion Fast Sans/,
  "Kindle application CSS must keep using process-local Fontconfig, not web-font loading");
assert.doesNotMatch(appCss, /@font-face[\s\S]*Noto Emoji/,
  "Kindle application CSS must use process-local Fontconfig for emoji");
for (const fontName of [
  "Amazon Ember", "Baskerville", "Bookerly", "Caecilia", "Caecilia Condensed",
  "Futura", "Helvetica", "OpenDyslexic", "Palatino"
]) {
  const escapedName = fontName.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
  assert.match(frontend,
    new RegExp(`"${escapedName}"\\s*:\\s*[^\\n]+"Noto Emoji"[^\\n]+(?:serif|sans-serif)`),
    `${fontName} must retain its existing faces ahead of Noto Emoji fallback`);
}
assert.match(appCss,
  /#page-content\.bionic-reading \.potion-editable-content,[\s\S]*font-family:\s*"Potion Fast Sans",\s*"Noto Emoji",\s*sans-serif/);
assert.match(appCss, /\.emoji-variation-selector\s*\{[\s\S]*display:\s*none/);
assert.match(appCss,
  /\.page-content table\s*\{[^}]*font-size:\s*0\.733333em;/,
  "table text must scale with the selected reader font size");
assert.doesNotMatch(appCss,
  /\.page-content table\s*\{[^}]*font-size:\s*[0-9.]+px;/,
  "reader tables must not override font scaling with a fixed pixel size");
assert.match(httpServer, /\.ttf[\s\S]*font\/ttf/);
assert.doesNotMatch(runKindle, /host-fonts\.css/,
  "the Kindle launch path must not acquire simulator font declarations");

const imageFitLogic = frontend.match(
  /\/\* IMAGE_FIT_LOGIC_BEGIN \*\/([\s\S]*?)\/\* IMAGE_FIT_LOGIC_END \*\//);
assert.ok(imageFitLogic, "missing Kindle image aspect-ratio fitting boundary");
const imageFitContext = {
  IMAGE_COMPACT_MAX_HEIGHT: 430,
  imageNodes: [],
  id: function() { return {clientWidth: 1000}; },
  isFinite,
  Math
};
vm.createContext(imageFitContext);
vm.runInContext(imageFitLogic[1], imageFitContext);
const tallImage = {
  className: "",
  naturalWidth: 1200,
  naturalHeight: 675,
  parentNode: {clientWidth: 1000},
  style: {}
};
imageFitContext.fitCompactImage(tallImage);
assert.equal(tallImage.style.width, "764px");
assert.equal(tallImage.style.height, "430px");
const wideImage = {
  className: "",
  naturalWidth: 1200,
  naturalHeight: 470,
  parentNode: {clientWidth: 1000},
  style: {}
};
imageFitContext.fitCompactImage(wideImage);
assert.equal(wideImage.style.width, "1000px");
assert.equal(wideImage.style.height, "auto",
  "width-limited images let Mesquite preserve their intrinsic ratio");
const nativeImage = {
  className: "",
  naturalWidth: 552,
  naturalHeight: 413,
  parentNode: {clientWidth: 1000},
  style: {}
};
imageFitContext.fitCompactImage(nativeImage);
assert.equal(nativeImage.style.width, "553px",
  "intrinsic-size images get an imperceptible scale to trigger Mesquite rasterization");
assert.equal(nativeImage.style.height, "auto",
  "images already within both limits avoid Mesquite's two-axis RGBA path");
wideImage.className = "expanded";
wideImage.style.width = "";
wideImage.style.height = "";
imageFitContext.fitCompactImage(wideImage);
assert.equal(wideImage.style.width, "",
  "expanded images remain controlled by their full-width CSS");
assert.match(frontend,
  /function imageFinished\(image, loaded\)[\s\S]*if \(loaded\) \{[\s\S]*?fitCompactImage\(image\)/);
assert.match(frontend,
  /function commitLoadedImagePaint\(image\)[\s\S]*visibility = "hidden"[\s\S]*offsetHeight[\s\S]*setTimeout[\s\S]*visibility = ""/,
  "loaded images need a delayed, layout-stable Mesquite paint commit");
assert.match(frontend,
  /function imageFinished\(image, loaded\)[\s\S]*if \(loaded\) \{[\s\S]*?commitLoadedImagePaint\(image\)/);
assert.match(frontend,
  /this\.className = "";\s*fitCompactImage\(this\)/);
assert.match(appCss,
  /\.page-content img\s*\{[^}]*display:\s*block;[^}]*width:\s*auto;[^}]*height:\s*auto;[^}]*max-width:\s*100%;[^}]*max-height:\s*430px;/);
assert.match(appCss,
  /\.page-content ol\s*\{[^}]*padding-left:\s*1\.7em;/,
  "numbered-list markers need a Kindle-safe inset without moving page text");

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
  /setSelectedButtons\([\s\S]*"appearance-bionic-reading",[\s\S]*bionicReading \? "on" : "off"/);

assert.match(frontend, /wordSpacing = settings\.wordSpacing === "plus"/);
assert.match(frontend, /lineSpacing = settings\.lineSpacing === "plus"/);
assert.match(frontend, /settings\.wordSpacing === "plusplusplus"/);
assert.match(frontend, /settings\.lineSpacing === "plusplusplus"/);
assert.match(frontend, /saveSetting\("wordSpacing", wordSpacing\)/);
assert.match(frontend, /saveSetting\("lineSpacing", lineSpacing\)/);
assert.match(frontend,
  /function changeReaderAppearance\(kind, value\)[\s\S]*currentReadingPosition\(\)[\s\S]*applyAppearance\(false\)[\s\S]*beginReadingRestore\(position, false\)/,
  "appearance reflow must preserve Potion's logical reading anchor");
assert.match(frontend,
  /fontNames = \[[\s\S]*"Amazon Ember"[\s\S]*"Palatino"[\s\S]*\]/);
assert.match(frontend,
  /\(fontIndex\(\) \+ fontNames\.length - 1\) % fontNames\.length/);
assert.match(frontend,
  /\(fontIndex\(\) \+ 1\) % fontNames\.length/);
assert.match(frontend,
  /id\("appearance-sheet"\)\.onclick = function\(event\)[\s\S]*\(event\.target \|\| event\.srcElement\) === this[\s\S]*closeAppearanceSheet\(\)/,
  "only a tap on the full-screen sheet backdrop should dismiss the panel");
assert.match(frontend,
  /function connectView\(\)[\s\S]*setAppearanceButtonVisible\(false\)[\s\S]*hide\(id\("reader-view"\)\)/);
assert.match(frontend,
  /function showPages\(positionSaved\)[\s\S]*setAppearanceButtonVisible\(false\)[\s\S]*show\(id\("pages-view"\)\)/);
assert.match(frontend,
  /function loadPages\(\)[\s\S]*setAppearanceButtonVisible\(false\)[\s\S]*show\(id\("pages-view"\)\)/);
assert.match(frontend,
  /show\(id\("reader-view"\)\);\s*setAppearanceButtonVisible\(true\);/,
  "Aa must become visible only after a reader page opens");

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
assert.match(appCss,
  /#page-content\.word-spacing-plus \.potion-editable-content,[\s\S]*word-spacing:\s*\.08em/);
assert.match(appCss,
  /#page-content\.line-spacing-plusplus \.potion-editable-content,[\s\S]*line-height:\s*1\.8/);
assert.match(appCss,
  /#page-content\.word-spacing-plusplusplus \.potion-editable-content,[\s\S]*word-spacing:\s*\.24em/);
assert.match(appCss,
  /#page-content\.line-spacing-plusplusplus \.potion-editable-content,[\s\S]*line-height:\s*2/);
assert.doesNotMatch(appCss,
  /#page-content\.(?:word|line)-spacing-(?:plus|plusplus)\s+(?:h[1-6]|table|th|td|\.toggle-summary|\.child-page)/);
assert.match(appCss,
  /\.page-content pre\s*\{[^}]*font-family:\s*monospace;[^}]*font-size:\s*18px;[^}]*line-height:\s*1\.4/,
  "the pre rule declares explicit font properties CODE SIZE can override");
assert.doesNotMatch(appCss, /font:\s*18px\/1\.4 monospace/,
  "the pre rule must not use the font shorthand, which would hide size overrides");
for (let px = 14; px <= 30; px += 2)
  assert.ok(
    new RegExp(
      "#page-content\\.code-size-" + px +
        " pre\\s*\\{[^}]*font-size:\\s*" + px + "px;"
    ).test(appCss),
    "code-size-" + px + " must size pre blocks to " + px + "px");
assert.match(appCss,
  /\.appearance-code-size-segments button,[\s\S]*?\.appearance-code-size-segments button:first-child \{ width:\s*50%; \}/,
  "the two code-size buttons share the row equally");
assert.match(appCss,
  /\.appearance-code-size-segments button:disabled\s*\{[^}]*color:\s*#888/,
  "clamped code-size buttons need visible disabled contrast");
assert.match(frontend,
  /kind === "code"\) \{\s*codeSize = parseInt\(value, 10\);\s*settingKey = "codeSize";\s*value = String\(codeSize\)/,
  "code size changes persist through changeReaderAppearance");
assert.match(frontend,
  /typeof settings\.codeSize === "number" &&[\s\S]*?CODE_SIZE_MIN[\s\S]*?CODE_SIZE_MAX[\s\S]*?:\s*18;/,
  "untrusted code size values fall back to 18px");
assert.match(frontend,
  /applyBionicClass\(\);\s*applySpacingClasses\(root\);\s*applyCodeSizeClass\(root\)/,
  "applyAppearance installs the code-size class alongside spacing");
assert.match(frontend,
  /id\("appearance-code-smaller"\)\.onclick = function\(\) \{\s*adjustCodeSize\(-CODE_SIZE_STEP\)/);
assert.match(frontend,
  /id\("appearance-code-larger"\)\.onclick = function\(\) \{\s*adjustCodeSize\(CODE_SIZE_STEP\)/);
assert.match(frontend,
  /id\("appearance-code-smaller"\)\.disabled = codeSize <= CODE_SIZE_MIN;\s*id\("appearance-code-larger"\)\.disabled = codeSize >= CODE_SIZE_MAX/,
  "code-size buttons disable at the grid limits");
assert.doesNotMatch(frontend, /setSelectedButtons\(\s*"appearance-code-size"/,
  "code size is an action, not a selected segment");

const CODE_SIZE_LOGIC_TESTS = (() => {
  const codeSizeLogic = frontend.match(
    /\/\* CODE_SIZE_LOGIC_BEGIN \*\/([\s\S]*?)\/\* CODE_SIZE_LOGIC_END \*\//);
  assert.ok(codeSizeLogic, "missing code-size logic test boundary");
  const codeSizeCalls = [];
  const codeSizeContext = {
    codeSize: 18,
    CODE_SIZE_MIN: 14,
    CODE_SIZE_MAX: 30,
    CODE_SIZE_STEP: 2,
    changeReaderAppearance: function(kind, value) {
      codeSizeCalls.push(kind + ":" + value);
      codeSizeContext.codeSize = parseInt(value, 10);
    }
  };
  vm.createContext(codeSizeContext);
  vm.runInContext(codeSizeLogic[1], codeSizeContext);
  codeSizeContext.adjustCodeSize(2);
  assert.deepEqual(codeSizeCalls, ["code:20"],
    "Larger steps the code size through the appearance pipeline");
  codeSizeContext.codeSize = 30;
  codeSizeContext.adjustCodeSize(2);
  assert.deepEqual(codeSizeCalls, ["code:20"],
    "code size clamps at its maximum without a no-op apply");
  codeSizeContext.codeSize = 14;
  codeSizeContext.adjustCodeSize(-2);
  assert.deepEqual(codeSizeCalls, ["code:20"],
    "code size clamps at its minimum without a no-op apply");
  return true;
})();
assert.equal(CODE_SIZE_LOGIC_TESTS, true);
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
  /function loadPages\(\) \{([\s\S]*?)\n    \}\n\n    \/\* EQUATION_ENRICHMENT_BEGIN/)[1];
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

const equationEnrichmentLogic = frontend.match(
  /\/\* EQUATION_ENRICHMENT_BEGIN \*\/([\s\S]*?)\/\* EQUATION_ENRICHMENT_END \*\//);
assert.ok(equationEnrichmentLogic, "missing asynchronous equation enrichment");
assert.doesNotMatch(equationEnrichmentLogic[1],
  /innerHTML|outerHTML|replaceChild|removeChild/,
  "equation enrichment must not replace page or block HTML");
assert.match(equationEnrichmentLogic[1],
  /range\.extractContents\(\)[\s\S]*document\.createElement\("span"\)/,
  "enrichment wraps only the exact logical color range");
{
  function mathNode(atomic, expression, className) {
    return {
      className: className || "math",
      getAttribute: function(name) {
        if (name === "data-potion-atomic" && atomic) return "1";
        if (name === "data-potion-expression" && atomic) return expression;
        return null;
      }
    };
  }
  const display = mathNode(false, "display");
  const first = mathNode(true, "x^2");
  const second = mathNode(true, "y = \\sin(x)", "math compatibility-class");
  const content = {logicalText: "we have \ufffc to show"};
  const editable = {
    getElementsByClassName: function(name) {
      return name === "potion-editable-content" ? [content] : [];
    }
  };
  const root = {
    getElementsByClassName: function(name) {
      if (name === "math") return [display, first, second];
      if (name === "potion-editable") return [editable];
      return [];
    }
  };
  const reader = {className: ""};
  const context = {
    currentPageId: "page-one",
    night: false,
    nightPageMode: "standard",
    id: function(name) {
      return name === "page-content" ? root : reader;
    },
    collectReadingBlocks: function() {},
    scheduleMathRepair: function() {},
    updateScroll: function() {},
    applyNightPageAppearance: function() {},
    logicalNodeText: function(node) { return node.logicalText; },
    encodeURIComponent,
    request: function() {}
  };
  vm.createContext(context);
  vm.runInContext(equationEnrichmentLogic[1], context);
  const appliedColors = [];
  context.applyEnrichedColor = function(target, color) {
    appliedColors.push({target: target, color: color});
  };
  context.applyEquationEnrichment("page-one", {
    blocks: [{
      editableIndex: 0,
      blockText: "we have \ufffc to show",
      colors: [{start: 0, end: 17, color: "yellow_background"}]
    }],
    equations: [
      {color: "default", expression: "x^2"},
      {color: "yellow_background", underline: true, bold: true,
        expression: "y = \\sin(x)"}
    ]
  });
  assert.equal(appliedColors.length, 1);
  assert.equal(appliedColors[0].target, content);
  assert.deepEqual(appliedColors[0].color,
    {start: 0, end: 17, color: "yellow_background"},
    "one merged color range covers text, an atomic equation, and more text");
  assert.equal(display.className, "math",
    "display math is not part of inline-equation enrichment");
  assert.equal(first.className, "math");
  assert.match(second.className, /notion-color-yellow-bg/);
  assert.match(second.className, /potion-equation-underline/);
  assert.match(second.className, /potion-equation-bold/);
  assert.match(second.className, /compatibility-class/,
    "equation enrichment preserves unrelated renderer classes");

  first.className = "math unchanged";
  second.className = "math unchanged";
  context.applyEquationEnrichment("page-one", {equations: [{color: "red"}]});
  assert.equal(first.className, "math unchanged");
  assert.equal(second.className, "math unchanged",
    "an equation-count mismatch must not partially annotate a page");
}
assert.match(httpServer,
  /"\/enrichment"[\s\S]*retrieve_page_enrichment/);
assert.match(httpServer,
  /"blocks"[\s\S]*"editableIndex"[\s\S]*"blockText"[\s\S]*"colors"/);
assert.match(httpServer,
  /"equationEnrichment"[\s\S]*equation_enrichment/);
assert.match(httpServer,
  /html\.find\("data-potion-expression="\)/,
  "inline-equation gating must not depend on class attribute order");
assert.match(frontend,
  /if \(page\.equationEnrichment\)\s*loadEquationEnrichment\(currentPageId, generation\)/);
assert.match(frontend,
  /generation === pageGeneration[\s\S]*pendingPageEnrichment/,
  "stale enrichment responses must not replace the current page result");
assert.match(frontend,
  /!pendingPageEnrichment \|\| positionRestoring \|\| selectionState \|\|\s*selectionHoldTimer !== null/,
  "enrichment waits for reading restoration and pending long presses");
assert.match(frontend,
  /function schedulePageEnrichmentApply\(\)[\s\S]*window\.setTimeout[\s\S]*tryApplyPageEnrichment/,
  "selection callbacks must defer enrichment DOM changes to a later task");
assert.match(frontend,
  /selection = window\.getSelection\(\);\s*if \(selection && selection\.rangeCount && !selection\.isCollapsed\)\s*return;/,
  "enrichment must not rewrite text underneath a native selection");
assert.match(frontend,
  /data-potion-expression[\s\S]*equations\[i\]\.expression/,
  "equation annotations are matched by expression, not count alone");
assert.match(appCss,
  /\.page-content \.math\.potion-equation-underline/);
const retrievePageSource = notionClient.match(
  /bool NotionClient::retrieve_page\([^\{]*\{([\s\S]*?)\n\}\n\nbool NotionClient::retrieve_page_enrichment/);
assert.ok(retrievePageSource, "retrieve_page and enrichment must remain separate");
assert.doesNotMatch(retrievePageSource[1], /\/children|collect\(|retrieve_page_enrichment/,
  "the initial page response must not wait for the recursive block walk");

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

// Removing formatting from the middle of a larger formatted run must split
// the ancestor around the selection. Otherwise the extracted text is inserted
// back inside the same <strong>/<u>/highlight and looks unchanged until reload.
const localFormattingMatch = frontend.match(
  /\/\* LOCAL_FORMATTING_DOM_BEGIN \*\/([\s\S]*?)\/\* LOCAL_FORMATTING_DOM_END \*\//);
assert.ok(localFormattingMatch, "missing local-formatting DOM test boundary");
function testElement(tag, className) {
  const node = {
    nodeType: 1,
    tagName: tag.toUpperCase(),
    className: className || "",
    parentNode: null,
    childNodes: [],
    appendChild: function(child) {
      if (child.parentNode) child.parentNode.removeChild(child);
      this.childNodes.push(child);
      child.parentNode = this;
      return child;
    },
    insertBefore: function(child, reference) {
      let index = this.childNodes.indexOf(reference);
      assert.notEqual(index, -1);
      if (child.parentNode) {
        const oldParent = child.parentNode;
        const oldIndex = oldParent.childNodes.indexOf(child);
        oldParent.childNodes.splice(oldIndex, 1);
        if (oldParent === this && oldIndex < index) --index;
      }
      this.childNodes.splice(index, 0, child);
      child.parentNode = this;
      return child;
    },
    removeChild: function(child) {
      const index = this.childNodes.indexOf(child);
      assert.notEqual(index, -1);
      this.childNodes.splice(index, 1);
      child.parentNode = null;
      return child;
    },
    cloneNode: function() { return testElement(tag, this.className); }
  };
  Object.defineProperty(node, "firstChild", {
    get: function() { return this.childNodes[0] || null; }
  });
  Object.defineProperty(node, "nextSibling", {
    get: function() {
      if (!this.parentNode) return null;
      const index = this.parentNode.childNodes.indexOf(this);
      return this.parentNode.childNodes[index + 1] || null;
    }
  });
  return node;
}
function testText(value) {
  const node = {nodeType: 3, nodeValue: value, parentNode: null};
  Object.defineProperty(node, "nextSibling", {
    get: function() {
      if (!this.parentNode) return null;
      const index = this.parentNode.childNodes.indexOf(this);
      return this.parentNode.childNodes[index + 1] || null;
    }
  });
  return node;
}
function localFormattingContext() {
  const context = {
    unwrapElement: function(element) {
      const parent = element.parentNode;
      while (element.firstChild)
        parent.insertBefore(element.firstChild, element);
      parent.removeChild(element);
    }
  };
  vm.createContext(context);
  vm.runInContext(localFormattingMatch[1], context);
  return context;
}

const splitContext = localFormattingContext();
const splitContent = testElement("div");
const splitStrong = splitContent.appendChild(testElement("strong"));
splitStrong.appendChild(testText("résumé "));
const splitMarker = splitStrong.appendChild(testElement("span"));
splitMarker.appendChild(testText("naïve"));
splitStrong.appendChild(testText(" façade"));
splitContext.liftSelectionFromFormatting(splitMarker, splitContent, "clear");
assert.deepEqual(splitContent.childNodes.map(function(node) {
  return node.nodeType === 3 ? node.nodeValue :
    node.tagName + ":" + node.firstChild.nodeValue;
}), ["STRONG:résumé ", "naïve", "STRONG: façade"],
"clearing the middle word immediately splits its surrounding bold run");

const linkContext = localFormattingContext();
const linkContent = testElement("div");
const linkStrong = linkContent.appendChild(testElement("strong"));
const link = linkStrong.appendChild(testElement("a"));
link.appendChild(testText("before "));
const linkMarker = link.appendChild(testElement("span"));
linkMarker.appendChild(testText("selected"));
link.appendChild(testText(" after"));
linkContext.liftSelectionFromFormatting(linkMarker, linkContent, "clear");
assert.deepEqual(linkContent.childNodes.map(function(node) {
  return node.tagName;
}), ["STRONG", "A", "STRONG"],
"non-formatting link ancestry is preserved around and within the selection");
assert.equal(linkContent.childNodes[1].firstChild.nodeValue, "selected");

// Formatting closes its popup and changes the DOM immediately. Backend writes
// remain serialized. A failure discards and reverses every still-pending
// optimistic action so the visible page cannot remain ahead of Notion.
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
    formatSaveActive: null,
    formatSaveQueue: [],
    id: function(name) { assert.equal(name, "page-content"); return root; },
    applySelectionLocally: function(localState, format, enabled) {
      calls.push("apply:" + format + ":" + enabled);
      localState.content.innerHTML = "optimistic";
      return true;
    },
    clearSelectionMenu: function(clearNative) {
      calls.push("clear:" + clearNative);
      context.selectionState = null;
    },
    collectReadingBlocks: function() { calls.push("blocks"); },
    applyNightPageAppearance: function() { calls.push("night"); },
    updateScroll: function() { calls.push("scroll"); },
    schedulePageEnrichmentApply: function() { calls.push("enrichment"); },
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
optimisticFailure.requests[0].callback("Notion rejected the edit", null);
assert.equal(optimisticFailure.content.innerHTML,
  '<a href="https://example.com"><strong class="notion-color">linked</strong></a>',
  "a failed write restores the exact prior nested markup and link");
assert.ok(optimisticFailure.calls.indexOf(
  "warning:Formatting could not be saved. Pending formatting was reverted. Notion rejected the edit") >= 0);
assert.equal(optimisticFailure.context.formatSaveBusy, false);
assert.equal(optimisticFailure.context.formatSaveQueue.length, 0);

const optimisticQueue = optimisticContext();
optimisticQueue.context.formatSelection("bold");
optimisticQueue.context.selectionState = optimisticQueue.state;
optimisticQueue.context.formatSelection("underline");
assert.equal(optimisticQueue.requests.length, 1,
  "a second popup action is accepted without overlapping the active write");
assert.equal(optimisticQueue.context.formatSaveQueue.length, 1);
assert.equal(optimisticQueue.calls.filter(function(call) {
  return call.indexOf("apply:") === 0;
}).length, 2, "every queued action is visible immediately");
assert.equal(optimisticQueue.calls.filter(function(call) {
  return call === "clear:true";
}).length, 2, "both popup clicks close immediately");
optimisticQueue.requests[0].callback(null, {enabled: true});
assert.equal(optimisticQueue.requests.length, 2,
  "the queued action starts when the prior request completes");
assert.equal(optimisticQueue.calls.filter(function(call) {
  return call.indexOf("apply:") === 0;
}).length, 2, "starting a queued request does not apply its formatting twice");
optimisticQueue.requests[1].callback(null, {enabled: true});
assert.equal(optimisticQueue.context.formatSaveBusy, false);
assert.equal(optimisticQueue.context.formatSaveQueue.length, 0);

const optimisticQueuedFailure = optimisticContext();
const queuedFailureOriginal = optimisticQueuedFailure.content.innerHTML;
optimisticQueuedFailure.context.formatSelection("bold");
optimisticQueuedFailure.context.selectionState = optimisticQueuedFailure.state;
optimisticQueuedFailure.context.formatSelection("underline");
assert.equal(optimisticQueuedFailure.content.innerHTML, "optimistic");
optimisticQueuedFailure.requests[0].callback("Notion rejected the edit", null);
assert.equal(optimisticQueuedFailure.requests.length, 1,
  "no later queued request is sent after the active request fails");
assert.equal(optimisticQueuedFailure.content.innerHTML, queuedFailureOriginal,
  "the failed action and every later optimistic action are reverted in reverse order");
assert.equal(optimisticQueuedFailure.context.formatSaveQueue.length, 0);
assert.equal(optimisticQueuedFailure.context.formatSaveActive, null);
assert.equal(optimisticQueuedFailure.context.formatSaveBusy, false);

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
assert.match(appCss,
  /\.selection-menu\s*\{[^}]*width:\s*430px;[^}]*height:\s*93px;/);
assert.match(appCss, /\.selection-menu button\s*\{[^}]*height:\s*84px;/);
assert.match(frontend,
  /function positionSelectionMenu\(rect\)[\s\S]*width = 430,\s*height = 93,/);
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
  /function finishSelectionHold\(event\)[\s\S]*if \(held\) beginCustomSelection\(\);[\s\S]*if \(selectionHoldActive\) \{[\s\S]*if \(selectionDragActive\) updateCustomSelection\(point\);/);
const finishMatch = frontend.match(
  /(function finishSelectionHold\(event\) \{[\s\S]*?\n    \})\n\n    function setSelectionButtonsDisabled/);
assert.ok(finishMatch, "missing finishSelectionHold");
const finishCalls = [];
const finishContext = {
  selectionHoldStartedAt: Date.now() - 1000,
  selectionHoldTimer: null,
  selectionHoldActive: true,
  selectionDragActive: true,
  selectionSuppressClick: true,
  clearSelectionHoldTimer: function() { finishContext.selectionHoldTimer = null; },
  clearDragSelectionUpdate: function() { finishCalls.push("clear-drag"); },
  eventPoint: function(event) { return {x: event.x, y: event.y}; },
  updateCustomSelection: function(point) {
    finishCalls.push("update:" + point.x + "," + point.y);
  },
  beginCustomSelection: function() { finishCalls.push("begin"); },
  inspectSelection: function(source) { finishCalls.push("inspect:" + source); },
  schedulePageEnrichmentApply: function() {},
  window: { setTimeout: function() {} },
  Date: Date
};
vm.createContext(finishContext);
vm.runInContext(finishMatch[1], finishContext);
assert.equal(finishContext.finishSelectionHold({x: 91, y: 72}), true);
assert.deepEqual(finishCalls,
  ["clear-drag", "update:91,72", "inspect:SELECT"],
  "release applies the exact final drag point before inspecting once");
assert.equal(finishContext.selectionHoldActive, false);
assert.equal(finishContext.selectionDragActive, false);
assert.match(frontend, /document\.onmousemove = moveCustomSelection;/);
assert.match(frontend, /document\.ontouchmove = moveCustomSelection;/);
assert.match(frontend,
  /document\.onmouseup = function\(event\) \{\s*if \(!finishSelectionHold\(event \|\| window\.event\)\)\s*scheduleSelectionInspection\(\);/);
assert.doesNotMatch(frontend, /id\("page-content"\)\.onmousemove/);
assert.doesNotMatch(frontend, /id\("page-content"\)\.onmouseup/);
assert.match(frontend,
  /id\("page-content"\)\.onscroll = function\(\) \{\s*if \(!selectionHoldActive\) clearSelectionMenu\(true\);/);
assert.match(frontend,
  /function applyAppearance\(persist\) \{\s*clearSelectionMenu\(true\);/);
assert.match(frontend, /source === "NATIVE" && selectionHoldTimer !== null/);
assert.match(frontend,
  /function scheduleSelectionInspection\(\) \{\s*if \(selectionMenuActive \|\| selectionHoldActive \|\| selectionDragActive\)/);
assert.match(frontend, /point\.x < rect\.left/);
assert.doesNotMatch(frontend, /selectionDebug|\/api\/debug\/selection/);
assert.doesNotMatch(index, /selection-debug/);

// Drag moves coalesce into one native selection update. The preferred WebKit
// API mutates the Selection without replacing its Range or inspecting menus.
const dragThrottleMatch = frontend.match(
  /\/\* DRAG_SELECTION_UPDATE_BEGIN \*\/([\s\S]*?)\/\* DRAG_SELECTION_UPDATE_END \*\//);
assert.ok(dragThrottleMatch, "missing drag-selection update test boundary");
const dragThrottleCalls = [];
const dragThrottleTimers = [];
const anchorNode = {
  compareDocumentPosition: function() { return 0; }
};
const endpointNode = {};
const nativeSelection = {
  setBaseAndExtent: function(base, baseOffset, focus, focusOffset) {
    dragThrottleCalls.push(
      "extent:" + baseOffset + ":" + focusOffset + ":" + (focus === endpointNode));
  },
  removeAllRanges: function() { dragThrottleCalls.push("remove"); },
  addRange: function() { dragThrottleCalls.push("add"); }
};
const dragThrottleContext = {
  selectionDragUpdateTimer: null,
  selectionDragPoint: null,
  selectionDragLastUpdateAt: 0,
  selectionHoldActive: true,
  selectionDragActive: true,
  selectionAnchor: {node: anchorNode, start: 1, end: 4},
  selectionDragUpdateMs: 50,
  SELECTION_DRAG_UPDATE_NORMAL_MS: 50,
  caretAtPoint: function(pointX) {
    dragThrottleCalls.push("caret:" + pointX);
    return {startContainer: endpointNode, startOffset: 8};
  },
  selectionAncestor: function() { return "same-content"; },
  wordCharacter: function(character) {
    return !!character && !/[\s.,;:!?()[\]{}"'\/\\|<>]/.test(character);
  },
  document: {createRange: function() { throw new Error("unexpected fallback"); }},
  window: {
    getSelection: function() { return nativeSelection; },
    setTimeout: function(callback, delay) {
      dragThrottleCalls.push("timer:" + delay);
      dragThrottleTimers.push({callback: callback, cancelled: false});
      return dragThrottleTimers.length;
    },
    clearTimeout: function(timer) {
      dragThrottleCalls.push("clear:" + timer);
      dragThrottleTimers[timer - 1].cancelled = true;
    }
  },
  Math
};
vm.createContext(dragThrottleContext);
vm.runInContext(dragThrottleMatch[1], dragThrottleContext);
dragThrottleContext.scheduleDragSelectionUpdate({x: 10, y: 20});
dragThrottleContext.scheduleDragSelectionUpdate({x: 30, y: 40});
assert.deepEqual(dragThrottleCalls, ["timer:50"],
  "rapid moves create at most one selection-update timer");
dragThrottleTimers[0].callback();
assert.deepEqual(dragThrottleCalls,
  ["timer:50", "caret:30", "extent:1:8:true"],
  "the timer applies only the newest point through setBaseAndExtent");
assert.equal(dragThrottleCalls.includes("remove"), false,
  "the preferred drag path does not replace the native Range");
const liveLargePageCalls = [];
const originalUpdateCustomSelection = dragThrottleContext.updateCustomSelection;
dragThrottleContext.selectionDragUpdateMs = 75;
dragThrottleContext.selectionDragLastUpdateAt = 0;
dragThrottleContext.updateCustomSelection = function(point) {
  liveLargePageCalls.push(point.x + "," + point.y);
};
dragThrottleContext.scheduleDragSelectionUpdate({x: 70, y: 80});
assert.deepEqual(liveLargePageCalls, ["70,80"],
  "a large page applies its first paced update inside touchmove");
assert.equal(dragThrottleContext.selectionDragUpdateTimer, null,
  "the live large-page update does not wait for a starvable timer");
dragThrottleContext.selectionDragUpdateMs = 50;
dragThrottleContext.selectionDragLastUpdateAt = 0;
dragThrottleContext.updateCustomSelection = originalUpdateCustomSelection;
const wordNode = {
  nodeType: 3,
  nodeValue: "virtualization",
  compareDocumentPosition: function() { return 0; }
};
dragThrottleContext.selectionAnchor = {node: anchorNode, start: 1, end: 4};
dragThrottleContext.caretAtPoint = function() {
  return {startContainer: wordNode, startOffset: 4};
};
dragThrottleContext.updateCustomSelection({x: 1, y: 1});
assert.equal(dragThrottleCalls[dragThrottleCalls.length - 1],
  "extent:1:14:false",
  "forward dragging snaps the focus to the end of its word");
dragThrottleContext.selectionAnchor = {node: wordNode, start: 6, end: 10};
dragThrottleContext.caretAtPoint = function() {
  return {startContainer: wordNode, startOffset: 3};
};
dragThrottleContext.updateCustomSelection({x: 2, y: 2});
assert.equal(dragThrottleCalls[dragThrottleCalls.length - 1],
  "extent:10:0:false",
  "backward dragging snaps the focus to the start of its word");
const extendCalls = [];
dragThrottleContext.selectionAnchor = {node: anchorNode, start: 1, end: 4};
dragThrottleContext.caretAtPoint = function() {
  return {startContainer: anchorNode, startOffset: 0};
};
dragThrottleContext.window.getSelection = function() {
  return {
    collapse: function(node, offset) {
      extendCalls.push("collapse:" + offset);
    },
    extend: function(node, offset) {
      extendCalls.push("extend:" + offset);
    }
  };
};
assert.equal(dragThrottleContext.updateCustomSelection({x: 0, y: 0}), true);
assert.deepEqual(extendCalls, ["collapse:4", "extend:0"],
  "Selection.extend fallback keeps the far edge of the original word anchored");
const silentNoopCalls = [];
const fallbackRange = {
  setStart: function(node, offset) {
    silentNoopCalls.push("start:" + offset + ":" + (node === anchorNode));
  },
  setEnd: function(node, offset) {
    silentNoopCalls.push("end:" + offset + ":" + (node === endpointNode));
  }
};
dragThrottleContext.selectionAnchor = {node: anchorNode, start: 1, end: 4};
dragThrottleContext.caretAtPoint = function() {
  return {startContainer: endpointNode, startOffset: 8};
};
dragThrottleContext.document.createRange = function() {
  silentNoopCalls.push("range");
  return fallbackRange;
};
dragThrottleContext.window.getSelection = function() {
  return {
    rangeCount: 1,
    setBaseAndExtent: function() { silentNoopCalls.push("extent-noop"); },
    getRangeAt: function() {
      return {
        startContainer: anchorNode,
        startOffset: 1,
        endContainer: anchorNode,
        endOffset: 4
      };
    },
    removeAllRanges: function() { silentNoopCalls.push("remove"); },
    addRange: function(range) {
      assert.equal(range, fallbackRange);
      silentNoopCalls.push("add");
    }
  };
};
assert.equal(dragThrottleContext.updateCustomSelection({x: 3, y: 3}), true);
assert.deepEqual(silentNoopCalls,
  ["extent-noop", "range", "start:1:true", "end:8:true", "remove", "add"],
  "a silently ignored setBaseAndExtent falls back to replacing the Range");
dragThrottleContext.scheduleDragSelectionUpdate({x: 50, y: 60});
dragThrottleContext.clearDragSelectionUpdate();
assert.equal(dragThrottleContext.selectionDragUpdateTimer, null);
assert.equal(dragThrottleContext.selectionDragPoint, null);
assert.equal(dragThrottleTimers[1].cancelled, true,
  "release cancels a pending coalesced update before applying its final point");
assert.match(frontend,
  /function clearSelectionMenu\(clearNative\)[\s\S]*clearDragSelectionUpdate\(\)/,
  "clearing selection cancels a pending drag update");
assert.doesNotMatch(dragThrottleMatch[1], /inspectSelection/);
assert.match(frontend,
  /page\.html\.length >= SELECTION_DRAG_LARGE_HTML_BYTES[\s\S]*SELECTION_DRAG_UPDATE_LARGE_MS[\s\S]*SELECTION_DRAG_UPDATE_NORMAL_MS/,
  "very large pages use slower drag updates so Mesquite can paint each range");

const moveMatch = frontend.match(
  /(function moveCustomSelection\(event\) \{[\s\S]*?\n    \})\n\n    function finishSelectionHold/);
assert.ok(moveMatch, "missing moveCustomSelection");
const moveCalls = [];
const moveContext = {
  selectionHoldActive: true,
  selectionDragActive: false,
  selectionHoldTimer: null,
  selectionTimer: 9,
  selectionHoldX: 10,
  selectionHoldY: 10,
  eventPoint: function() { return {x: 50, y: 50}; },
  id: function() { return {name: "menu"}; },
  hide: function() { moveCalls.push("hide"); },
  scheduleDragSelectionUpdate: function(point) {
    moveCalls.push("schedule:" + point.x + "," + point.y);
  },
  window: {clearTimeout: function(timer) { moveCalls.push("clear:" + timer); }},
  Math: Math,
  Date: Date
};
vm.createContext(moveContext);
vm.runInContext(moveMatch[1], moveContext);
moveContext.moveCustomSelection({preventDefault: function() { moveCalls.push("prevent"); }});
assert.deepEqual(moveCalls,
  ["prevent", "clear:9", "hide", "schedule:50,50"],
  "starting a drag hides the menu and schedules only a coalesced selection update");
assert.equal(moveContext.selectionDragActive, true);
assert.equal(moveContext.selectionTimer, null);
assert.doesNotMatch(moveMatch[1], /removeAllRanges|addRange|inspectSelection/);

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
    schedulePageEnrichmentApply: function() {},
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
const imageRetryLogic = frontend.match(
  /\/\* IMAGE_RETRY_LOGIC_BEGIN \*\/([\s\S]*?)\/\* IMAGE_RETRY_LOGIC_END \*\//);
assert.ok(imageRetryLogic, "missing recoverable image retry lifecycle");
{
  let now = 0;
  const timers = [];
  const root = {scrollTop: 0, clientHeight: 400};
  const assignedSources = [];
  const attributes = {"data-src": "http://127.0.0.1:8766/api/images/key"};
  const image = {
    offsetParent: root,
    offsetTop: 100,
    offsetHeight: 100,
    complete: false,
    className: "",
    style: {},
    _potionImageAttempts: 0,
    _potionImageRequested: false,
    _potionImageLoading: false,
    _potionImageNextAttemptAt: 0,
    _potionImageGeneration: 1,
    getAttribute: function(name) { return attributes[name] || ""; },
    setAttribute: function(name, value) {
      attributes[name] = value;
      if (name === "src") assignedSources.push(value);
    }
  };
  function FakeDate() { return {getTime: function() { return now; }}; }
  const context = {
    IMAGE_RETRY_MAX_MS: 300000,
    IMAGE_AUTOMATIC_RETRY_LIMIT: 6,
    imageGeneration: 1,
    imageLoads: 0,
    imageNodes: [image],
    imageLoadTimer: null,
    Date: FakeDate,
    Math,
    isFinite,
    id: function(name) {
      return name === "page-content" ? root : {className: ""};
    },
    mathTop: function(node) { return node.offsetTop; },
    readingRestoreAffects: function() { return false; },
    armReadingRestoreQuietPeriod: function() {},
    maintainReadingRestore: function() {},
    fitCompactImage: function() {},
    invertNightImage: function() {},
    updateScroll: function() {},
    scheduleMathRepair: function() {},
    scheduleImageLoad: function(delay) {
      if (context.imageLoadTimer !== null) return;
      context.imageLoadTimer = context.window.setTimeout(function() {
        context.loadImagesNearViewport();
      }, typeof delay === "number" ? delay : 40);
    },
    resetImageLoading: function() {},
    clear: function() {},
    window: {
      setTimeout: function(callback, delay) {
        const timer = {callback, delay, cancelled: false};
        timers.push(timer);
        return timer;
      },
      clearTimeout: function(timer) { timer.cancelled = true; }
    }
  };
  vm.createContext(context);
  vm.runInContext(imageRetryLogic[1], context);

  context.loadImagesNearViewport();
  assert.equal(assignedSources.length, 1);
  context.loadImagesNearViewport();
  assert.equal(assignedSources.length, 1,
    "an in-flight image is never downloaded concurrently");
  context.imageFinished(image, false);
  assert.equal(image._potionImageRequested, false);
  now = 1000;
  timers[timers.length - 1].callback();
  assert.equal(assignedSources.length, 2,
    "a transient image failure is retried after backoff");
  context.imageFinished(image, false);
  now = 3000;
  timers[timers.length - 1].callback();
  assert.equal(assignedSources.length, 3,
    "an image remains retryable beyond the former two-attempt cutoff");
  context.imageFinished(image, true);
  assert.equal(image._potionImageNextAttemptAt, 0,
    "a later successful load clears retry backoff");
}
assert.match(frontend,
  /function repairMathNearViewport\(\)[\s\S]*maintainReadingRestore\(null, true\)/);
assert.match(frontend,
  /function saveCurrentReadingPosition\(done\)[\s\S]*if \(positionRestoring \|\|/);
assert.match(frontend,
  /window\.onorientationchange = function\(\) \{\s*fitCompactImages\(\);\s*scheduleViewportReadingRestore\(\)/);
assert.match(frontend,
  /window\.onresize = function\(\) \{\s*fitCompactImages\(\);[\s\S]*scheduleViewportReadingRestore\(\)/);

console.log("Potion frontend native-math contract tests passed");
