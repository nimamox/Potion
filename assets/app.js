(function() {
    "use strict";
    var API = "http://127.0.0.1:8766",
        busy = false,
        currentPageId = "",
        currentPagePinned = false,
        pageSortMode = "opened",
        pageList = [],
        pageListLoaded = false,
        pageSortSaveBusy = false,
        pageHistory = [];
    var irisTimer = null,
        irisFrame = 0,
        irisRotation = 0;
    var mathNodes = [],
        mathRepairTimer = null,
        imageNodes = [],
        imageLoadTimer = null,
        imageLoads = 0,
        imageGeneration = 0,
        nightStyledElements = [];
    var aboutLogoTimer = null,
        aboutLogoReady = false,
        aboutLogoPending = false;
    var readingBlocks = [],
        positionSaveTimer = null,
        positionRestoreTimer = null,
        positionRestoring = false,
        positionRestoreAnchor = null,
        transientReadingPosition = null,
        transientPositionTimer = null,
        viewportRestoreTimer = null,
        readingViewportWidth = 0;
    var POSITION_RESTORE_QUIET_MS = 400,
        TRANSIENT_POSITION_UPDATE_MS = 100;
    var selectionTimer = null,
        selectionState = null,
        selectionMenuActive = false,
        formatSaveBusy = false,
        selectionHoldTimer = null,
        selectionHoldActive = false,
        selectionHoldX = 0,
        selectionHoldY = 0,
        selectionHoldStartedAt = 0,
        selectionDragActive = false,
        selectionDragInspectTimer = null,
        selectionAnchor = null,
        selectionSuppressClick = false;
    var SELECTION_DRAG_INSPECT_MS = 80;
    var fontScales = [0.7, 0.8, 0.9, 1, 1.1, 1.25, 1.4, 1.6],
        fontScale = 1,
        pageFont = "Bookerly",
        night = false,
        nightPageMode = "standard",
        pageButtonMode = "normal",
        rotationMode = "auto",
        brandLogoDaySrc = null;
    var fonts = {
        "Amazon Ember": '"Amazon Ember",Arial,sans-serif',
        "Baskerville": "Baskerville,Georgia,serif",
        "Bookerly": "Bookerly,Georgia,serif",
        "Caecilia": '"Caecilia Regular",Georgia,serif',
        "Caecilia Condensed": 'condensed,"Caecilia Regular",Georgia,serif',
        "Futura": "Futura,Arial,sans-serif",
        "Helvetica": '"Helvetica Neue LT",Helvetica,Arial,sans-serif',
        "OpenDyslexic": "OpenDyslexic,Arial,sans-serif",
        "Palatino": "Palatino,Georgia,serif"
    };

    function id(name) {
        return document.getElementById(name);
    }

    /* ROTATION_LOGIC_BEGIN */
    function currentKindleOrientation() {
        var angle = typeof window.orientation === "number" ? window.orientation : null;
        if (angle === 180 || angle === -180) return "portraitDown";
        if (angle === 90) return "landscapeLeft";
        if (angle === -90 || angle === 270) return "landscapeRight";
        if (angle === 0) return "portraitUp";
        return window.innerWidth > window.innerHeight ? "landscape" : "portrait";
    }

    function updateRotationButton() {
        var button = id("rotation"), action;
        if (!button) return;
        action = rotationMode === "auto" ? "Rotation locked" : "Auto rotation";
        button.innerHTML = rotationMode === "auto" ? "⌽" : "⟳";
        button.title = action;
        button.setAttribute("aria-label", action);
        button.setAttribute("aria-pressed", rotationMode === "locked" ? "true" : "false");
    }

    function applyRotationMode() {
        var device;
        if (!window.kindle || !window.kindle.device) return false;
        device = window.kindle.device;
        if (typeof device.setOrientation !== "function") return false;
        try {
            device.setOrientation(
                rotationMode === "auto" ? "auto" : currentKindleOrientation()
            );
            return true;
        } catch (ignore) {
            return false;
        }
    }

    function chooseRotationMode(mode, persist) {
        var previous = rotationMode;
        rotationMode = mode === "locked" ? "locked" : "auto";
        updateRotationButton();
        applyRotationMode();
        if (!persist) return;
        request(
            "POST",
            "/api/settings",
            "key=rotationMode&value=" + encodeURIComponent(rotationMode),
            function(error) {
                if (!error) return;
                rotationMode = previous;
                updateRotationButton();
                applyRotationMode();
                warning(error);
            }
        );
    }
    /* ROTATION_LOGIC_END */

    function requestedPageId() {
        var source = (window.location.search || "") + "&" + (window.location.hash || ""),
            match = source.match(/[?&#]page=([0-9a-fA-F-]+)/),
            value;
        if (!match) return "";
        value = match[1].replace(/-/g, "");
        return /^[0-9a-fA-F]{32}$/.test(value) ? value : "";
    }

    function kindleMathLayout() {
        return !!window.kindle || /[?&]mesquite=1(?:&|$)/.test(window.location.search || "");
    }

    function scaleIndex() {
        var i;
        for (i = 0; i < fontScales.length; ++i)
            if (fontScales[i] === fontScale) return i;
        return 3;
    }

    function hide(element) {
        if (element.className.indexOf("hidden") < 0) element.className += " hidden";
    }

    function show(element) {
        element.className = element.className.replace(/(^|\s)hidden(?=\s|$)/g, "");
    }

    function clear(element) {
        while (element.firstChild) element.removeChild(element.firstChild);
    }

    function drawIrisFrame() {
        var canvas = id("busy-iris"),
            context, radii = [5, 8, 12, 16, 16, 12, 8, 5],
            center = 25, outer = 22, shoulder = 16,
            sector = Math.PI / 3,
            aperture = radii[irisFrame],
            angle, i, j, point,
            background = night ? "#000" : "#fff",
            bladeA = night ? "#eee" : "#111",
            bladeB = night ? "#aaa" : "#666";
        if (!canvas || !canvas.getContext) return;
        context = canvas.getContext("2d");
        context.fillStyle = background;
        context.fillRect(0, 0, 50, 50);
        context.lineWidth = 1;
        context.strokeStyle = background;
        for (i = 0; i < 6; ++i) {
            angle = irisRotation + irisFrame * .035 + i * sector;
            point = [
                [outer, angle - .12],
                [outer, angle + sector * .72],
                [shoulder, angle + sector * .96],
                [aperture, angle + sector * .62],
                [aperture, angle + sector * .08]
            ];
            context.beginPath();
            for (j = 0; j < point.length; ++j) {
                if (j === 0)
                    context.moveTo(center + Math.cos(point[j][1]) * point[j][0],
                        center + Math.sin(point[j][1]) * point[j][0]);
                else
                    context.lineTo(center + Math.cos(point[j][1]) * point[j][0],
                        center + Math.sin(point[j][1]) * point[j][0]);
            }
            context.closePath();
            context.fillStyle = i % 2 ? bladeB : bladeA;
            context.fill();
            context.stroke();
        }
        context.beginPath();
        for (i = 0; i < 6; ++i) {
            angle = irisRotation + irisFrame * .035 + i * sector + sector * .35;
            if (i === 0)
                context.moveTo(center + Math.cos(angle) * aperture,
                    center + Math.sin(angle) * aperture);
            else
                context.lineTo(center + Math.cos(angle) * aperture,
                    center + Math.sin(angle) * aperture);
        }
        context.closePath();
        context.fillStyle = background;
        context.fill();
        context.strokeStyle = bladeA;
        context.stroke();
        context.beginPath();
        context.arc(center, center, outer, 0, Math.PI * 2, false);
        context.stroke();
    }

    function advanceIris() {
        irisTimer = null;
        if (!busy) return;
        drawIrisFrame();
        irisFrame += 1;
        if (irisFrame >= 8) {
            irisFrame = 0;
            irisRotation += Math.PI / 18;
        }
        irisTimer = window.setTimeout(advanceIris, 275);
    }

    function setBusy(value) {
        var canvas = id("busy-iris");
        value = !!value;
        busy = value;
        if (value) {
            if (canvas) canvas.className = "busy-iris active";
            if (irisTimer === null) {
                irisFrame = 0;
                advanceIris();
            }
        } else {
            if (irisTimer !== null) {
                window.clearTimeout(irisTimer);
                irisTimer = null;
            }
            if (canvas) canvas.className = "busy-iris";
        }
    }

    function stopAboutLogoAnimation() {
        if (aboutLogoTimer !== null) {
            window.clearTimeout(aboutLogoTimer);
            aboutLogoTimer = null;
        }
    }

    function drawAboutLogoClean() {
        var image = id("about-logo"),
            canvas = id("about-logo-static"),
            context;
        if (!aboutLogoReady || !canvas || !canvas.getContext) return;
        context = canvas.getContext("2d");
        context.fillStyle = "#fff";
        context.fillRect(0, 0, canvas.width, canvas.height);
        context.drawImage(image, 0, 0, canvas.width, canvas.height);
    }

    function prepareAboutLogo() {
        var image = id("about-logo"),
            canvas = id("about-logo-static");
        if (aboutLogoReady || !image || !canvas || !canvas.getContext) return;
        try {
            aboutLogoReady = true;
            drawAboutLogoClean();
            show(canvas);
            hide(image);
        } catch (ignored) {
            aboutLogoReady = false;
            show(image);
            hide(canvas);
            return;
        }
        if (aboutLogoPending && id("about-dialog").className.indexOf("hidden") < 0) {
            aboutLogoPending = false;
            playAboutLogoAnimation();
        }
    }

    function loadAboutLogo() {
        var image = id("about-logo"),
            source;
        if (aboutLogoReady || image.getAttribute("src")) return;
        source = image.getAttribute("data-src");
        if (!source) return;
        image.setAttribute("src", source);
        if (image.complete) window.setTimeout(prepareAboutLogo, 0);
    }

    function playAboutLogoAnimation() {
        var canvas = id("about-logo-static"),
            context, levels = [1, .92, .8, .64, .47, .31, .18, .08, 0],
            frame = 0,
            cell = 12;
        if (!aboutLogoReady) {
            aboutLogoPending = true;
            return;
        }
        aboutLogoPending = false;
        context = canvas.getContext("2d");
        stopAboutLogoAnimation();

        function tick() {
            var amount = levels[frame],
                x, y;
            drawAboutLogoClean();
            if (amount > 0) {
                for (y = 0; y < canvas.height; y += cell) {
                    for (x = 0; x < canvas.width; x += cell) {
                        if (Math.random() < amount) {
                            context.fillStyle = Math.random() < .5 ? "#000" : "#fff";
                            context.fillRect(x, y, cell, cell);
                        }
                    }
                }
            }
            frame += 1;
            if (frame < levels.length) aboutLogoTimer = window.setTimeout(tick, 125);
            else {
                aboutLogoTimer = null;
                drawAboutLogoClean();
            }
        }
        tick();
    }

    function openAbout() {
        hideSettingsTooltip();
        show(id("about-dialog"));
        loadAboutLogo();
        playAboutLogoAnimation();
    }

    function closeAbout() {
        aboutLogoPending = false;
        stopAboutLogoAnimation();
        if (aboutLogoReady) drawAboutLogoClean();
        hide(id("about-dialog"));
    }

    function request(method, path, body, done) {
        var xhr = new XMLHttpRequest();
        xhr.open(method, API + path, true);
        if (method === "POST") xhr.setRequestHeader("Content-Type", "application/x-www-form-urlencoded");
        xhr.onreadystatechange = function() {
            if (xhr.readyState !== 4) return;
            var result = null;
            try {
                result = JSON.parse(xhr.responseText || "{}");
            } catch (ignored) {}
            if (xhr.status >= 200 && xhr.status < 300) done(null, result, xhr);
            else done(result && result.message || "Potion request failed.", result, xhr);
        };
        xhr.onerror = function() {
            done("Potion engine is not responding.");
        };
        xhr.send(body || null);
    }

    function hasClass(element, name) {
        return element && element.nodeType === 1 &&
            (" " + element.className + " ").indexOf(" " + name + " ") >= 0;
    }

    function selectionAncestor(node, name) {
        if (node && node.nodeType !== 1) node = node.parentNode;
        while (node && node !== id("page-content")) {
            if (hasClass(node, name)) return node;
            node = node.parentNode;
        }
        return null;
    }

    function clearSelectionMenu(clearNative) {
        var selection;
        if (selectionTimer !== null) {
            window.clearTimeout(selectionTimer);
            selectionTimer = null;
        }
        hide(id("selection-menu"));
        selectionState = null;
        selectionMenuActive = false;
        clearSelectionHoldTimer();
        clearDragSelectionInspection();
        selectionHoldActive = false;
        selectionHoldStartedAt = 0;
        selectionDragActive = false;
        selectionAnchor = null;
        selectionSuppressClick = false;
        if (clearNative && window.getSelection) {
            selection = window.getSelection();
            if (selection && selection.removeAllRanges) selection.removeAllRanges();
        }
    }

    function editableIndex(block) {
        var blocks = id("page-content").getElementsByClassName("potion-editable"),
            i;
        for (i = 0; i < blocks.length; ++i)
            if (blocks[i] === block) return i;
        return -1;
    }

    function positionSelectionMenu(rect) {
        var menu = id("selection-menu"),
            width = 430,
            height = 62,
            viewportWidth = document.documentElement.clientWidth || document.body.clientWidth,
            viewportHeight = document.documentElement.clientHeight || document.body.clientHeight,
            left = Math.max(8, Math.floor((viewportWidth - width) / 2)),
            top = 188;
        if (rect && typeof rect.top === "number") {
            if (rect.top > 180 + height + 12) top = rect.top - height - 10;
            else top = rect.bottom + 10;
        }
        top = Math.max(94, Math.min(viewportHeight - height - 8, Math.floor(top)));
        menu.style.left = left + "px";
        menu.style.top = top + "px";
    }

    function nodeHasSelectionFormat(node, content, format) {
        var tag, classes;
        node = node.parentNode;
        while (node && node !== content) {
            if (node.nodeType === 1) {
                tag = node.tagName.toLowerCase();
                classes = " " + node.className + " ";
                if (format === "bold" && (tag === "strong" || tag === "b")) return true;
                if (format === "underline" && tag === "u") return true;
                if (format === "highlight" &&
                    classes.indexOf(" notion-color-yellow-bg ") >= 0) return true;
            }
            node = node.parentNode;
        }
        return false;
    }

    function logicalNodeText(node) {
        var out = "", child;
        if (node.nodeType === 3) return node.nodeValue;
        if (node.nodeType !== 1 && node.nodeType !== 11) return out;
        if (node.nodeType === 1) {
            if (node.tagName.toLowerCase() === "br") return "\n";
            if (node.getAttribute("data-potion-atomic") !== null) return "\ufffc";
        }
        child = node.firstChild;
        while (child) {
            out += logicalNodeText(child);
            child = child.nextSibling;
        }
        return out;
    }

    /* MULTI_TAP_SELECTION_LOGIC_BEGIN */
    function sentenceBounds(text, offset) {
        var start, end, length = text.length;
        if (!length) return {start: 0, end: 0};
        offset = Math.max(0, Math.min(offset, length - 1));
        start = offset;
        while (start > 0 && !/[.!?\u061f\n]/.test(text.charAt(start - 1))) --start;
        while (start < length && /\s/.test(text.charAt(start))) ++start;
        end = Math.max(start, offset);
        while (end < length && !/[.!?\u061f\n]/.test(text.charAt(end))) ++end;
        if (end < length && text.charAt(end) !== "\n") ++end;
        while (end < length && /[\"'\u2019\u201d)\]]/.test(text.charAt(end))) ++end;
        while (end > start && /\s/.test(text.charAt(end - 1))) --end;
        return {start: start, end: end};
    }
    /* MULTI_TAP_SELECTION_LOGIC_END */

    function childOffset(node) {
        var offset = 0;
        while (node.previousSibling) {
            node = node.previousSibling;
            ++offset;
        }
        return offset;
    }

    function logicalBoundary(content, wanted) {
        var at = 0, result = null;
        function visit(node) {
            var leaf = null, next, child, offset;
            if (result) return;
            if (node.nodeType === 3) leaf = node.nodeValue;
            else if (node.nodeType === 1 && node.tagName.toLowerCase() === "br") leaf = "\n";
            else if (node.nodeType === 1 && node.getAttribute("data-potion-atomic") !== null)
                leaf = "\ufffc";
            if (leaf !== null) {
                next = at + leaf.length;
                if (wanted <= next) {
                    if (node.nodeType === 3) {
                        result = {node: node, offset: Math.max(0, wanted - at)};
                    } else {
                        offset = childOffset(node);
                        result = {node: node.parentNode, offset: offset + (wanted > at ? 1 : 0)};
                    }
                }
                at = next;
                return;
            }
            child = node.firstChild;
            while (child && !result) {
                visit(child);
                child = child.nextSibling;
            }
        }
        visit(content);
        return result || {node: content, offset: content.childNodes.length};
    }

    function logicalRange(content, start, end) {
        var first = logicalBoundary(content, start),
            last = logicalBoundary(content, end),
            range = document.createRange();
        try {
            range.setStart(first.node, first.offset);
            range.setEnd(last.node, last.offset);
            return range;
        } catch (ignored) {}
        return null;
    }

    function setLogicalSelection(content, start, end) {
        var range = logicalRange(content, start, end), selection = window.getSelection();
        if (!range) return false;
        selection.removeAllRanges();
        selection.addRange(range);
        return true;
    }

    function selectMultiTap(event, wholeBlock) {
        var point = eventPoint(event), caret, content, text, before, offset, bounds;
        if (!point || busy || !currentPageId) return false;
        caret = caretAtPoint(point.x, point.y);
        content = caret && selectionAncestor(caret.startContainer, "potion-editable-content");
        if (!content)
            content = selectionAncestor(event && event.target, "potion-editable-content");
        if (!content) return false;
        text = logicalNodeText(content);
        if (!text) return false;
        if (wholeBlock) bounds = {start: 0, end: text.length};
        else {
            if (!caret) return false;
            before = document.createRange();
            before.selectNodeContents(content);
            try { before.setEnd(caret.startContainer, caret.startOffset); }
            catch (ignored) { return false; }
            offset = logicalNodeText(before.cloneContents()).length;
            bounds = sentenceBounds(text, offset);
        }
        if (bounds.end <= bounds.start ||
            !setLogicalSelection(content, bounds.start, bounds.end)) return false;
        inspectSelection(wholeBlock ? "TRIPLE" : "DOUBLE");
        return true;
    }

    function normalizeWholeBlockRange(selection, range) {
        var block = selectionAncestor(range.startContainer, "potion-editable") ||
                selectionAncestor(range.endContainer, "potion-editable"),
            contents, content, selectedText, blockText, normalized;
        if (!block) return null;
        contents = block.getElementsByClassName("potion-editable-content");
        if (!contents.length) return null;
        content = contents[0];
        selectedText = String(selection.toString ? selection.toString() : "");
        blockText = logicalNodeText(content);
        normalized = function(value) {
            return value.replace(/^\s+|\s+$/g, "").replace(/\r\n/g, "\n");
        };
        if (!blockText || normalized(selectedText) !== normalized(blockText)) return null;
        range = document.createRange();
        range.selectNodeContents(content);
        selection.removeAllRanges();
        selection.addRange(range);
        return range;
    }

    function selectionFormatState(content, start, end) {
        var at = 0, any = false,
            state = {highlight: true, bold: true, underline: true};
        function visit(node) {
            var next, child, leaf = null;
            if (node.nodeType === 3) leaf = node.nodeValue;
            else if (node.nodeType === 1 && node.tagName.toLowerCase() === "br") leaf = "\n";
            else if (node.nodeType === 1 && node.getAttribute("data-potion-atomic") !== null)
                leaf = "\ufffc";
            if (leaf !== null) {
                next = at + leaf.length;
                if (at < end && next > start) {
                    any = true;
                    if (!nodeHasSelectionFormat(node, content, "highlight")) state.highlight = false;
                    if (!nodeHasSelectionFormat(node, content, "bold")) state.bold = false;
                    if (!nodeHasSelectionFormat(node, content, "underline")) state.underline = false;
                }
                at = next;
                return;
            }
            child = node.firstChild;
            while (child) {
                visit(child);
                child = child.nextSibling;
            }
        }
        visit(content);
        if (!any) state.highlight = state.bold = state.underline = false;
        return state;
    }

    function updateSelectionButtonStates(state) {
        var buttons = id("selection-menu").getElementsByTagName("button"),
            i, format, active;
        for (i = 0; i < buttons.length; ++i) {
            format = buttons[i].getAttribute("data-format");
            active = format !== "clear" && !!state[format];
            buttons[i].className = buttons[i].className.replace(/\s*selection-active/g, "") +
                (active ? " selection-active" : "");
            if (format !== "clear") buttons[i].setAttribute("aria-pressed", active ? "true" : "false");
        }
    }

    function inspectSelection(source) {
        var selection, range, startContent, endContent, block, before, through,
            index, text, blockText, start, end, rect = null;
        selectionTimer = null;
        if (source === "NATIVE" && selectionHoldTimer !== null) return;
        if (selectionMenuActive || busy || !currentPageId ||
            id("reader-view").className.indexOf("hidden") >= 0 ||
            !window.getSelection || !document.createRange) return;
        selection = window.getSelection();
        if (!selection || selection.rangeCount < 1 || selection.isCollapsed) {
            clearSelectionMenu(false);
            return;
        }
        range = selection.getRangeAt(0);
        startContent = selectionAncestor(range.startContainer, "potion-editable-content");
        endContent = selectionAncestor(range.endContainer, "potion-editable-content");
        if (!startContent || startContent !== endContent) {
            range = normalizeWholeBlockRange(selection, range);
            if (!range) {
                clearSelectionMenu(false);
                return;
            }
            startContent = selectionAncestor(range.startContainer, "potion-editable-content");
            endContent = selectionAncestor(range.endContainer, "potion-editable-content");
        }
        block = selectionAncestor(startContent, "potion-editable");
        index = editableIndex(block);
        before = document.createRange();
        before.selectNodeContents(startContent);
        before.setEnd(range.startContainer, range.startOffset);
        through = document.createRange();
        through.selectNodeContents(startContent);
        through.setEnd(range.endContainer, range.endOffset);
        blockText = logicalNodeText(startContent);
        start = logicalNodeText(before.cloneContents()).length;
        end = logicalNodeText(through.cloneContents()).length;
        text = blockText.substring(start, end);
        if (!block || index < 0 || !text || text.length > 4000) {
            clearSelectionMenu(false);
            return;
        }
        if (range.getBoundingClientRect) {
            try { rect = range.getBoundingClientRect(); } catch (ignored) {}
        }
        selectionState = {
            range: range.cloneRange(),
            content: startContent,
            editableIndex: index,
            blockText: blockText,
            selectedText: text,
            start: start,
            end: end
        };
        selectionState.formats = selectionFormatState(
            startContent, selectionState.start, selectionState.end);
        updateSelectionButtonStates(selectionState.formats);
        setSelectionButtonsDisabled(formatSaveBusy);
        positionSelectionMenu(rect);
        show(id("selection-menu"));
    }

    function scheduleSelectionInspection() {
        if (selectionMenuActive) return;
        if (selectionTimer !== null) window.clearTimeout(selectionTimer);
        selectionTimer = window.setTimeout(function() { inspectSelection("NATIVE"); }, 140);
    }

    /* DRAG_SELECTION_THROTTLE_BEGIN */
    function clearDragSelectionInspection() {
        if (selectionDragInspectTimer !== null) {
            window.clearTimeout(selectionDragInspectTimer);
            selectionDragInspectTimer = null;
        }
    }

    function scheduleDragSelectionInspection() {
        if (selectionDragInspectTimer !== null) return;
        selectionDragInspectTimer = window.setTimeout(function() {
            selectionDragInspectTimer = null;
            if (selectionHoldActive) inspectSelection("DRAG");
        }, SELECTION_DRAG_INSPECT_MS);
    }
    /* DRAG_SELECTION_THROTTLE_END */

    function eventPoint(event) {
        var touch = event && event.touches && event.touches.length ?
                event.touches[0] :
                event && event.changedTouches && event.changedTouches.length ?
                    event.changedTouches[0] : event;
        if (!touch) return null;
        return {
            x: typeof touch.clientX === "number" ? touch.clientX : touch.pageX,
            y: typeof touch.clientY === "number" ? touch.clientY : touch.pageY
        };
    }

    function clearSelectionHoldTimer() {
        if (selectionHoldTimer !== null) {
            window.clearTimeout(selectionHoldTimer);
            selectionHoldTimer = null;
        }
    }

    function wordCharacter(character) {
        return !!character && !/[\s.,;:!?()[\]{}"'\/\\|<>]/.test(character);
    }

    function caretAtPoint(x, y) {
        if (!document.caretRangeFromPoint) return null;
        try { return document.caretRangeFromPoint(x, y); } catch (ignored) {}
        return null;
    }

    function selectWordAtPoint(x, y) {
        var caret = caretAtPoint(x, y),
            node, text, start, end, range, selection;
        if (!caret || !caret.startContainer ||
            !selectionAncestor(caret.startContainer, "potion-editable-content")) return false;
        node = caret.startContainer;
        text = node.nodeValue || "";
        start = Math.min(caret.startOffset, text.length);
        if (start === text.length && start) --start;
        if (!wordCharacter(text.charAt(start))) return false;
        end = start + 1;
        while (start > 0 && wordCharacter(text.charAt(start - 1))) --start;
        while (end < text.length && wordCharacter(text.charAt(end))) ++end;
        range = document.createRange();
        range.setStart(node, start);
        range.setEnd(node, end);
        selection = window.getSelection();
        selection.removeAllRanges();
        selection.addRange(range);
        selectionAnchor = {node: node, start: start, end: end};
        selectionHoldActive = true;
        selectionDragActive = false;
        selectionSuppressClick = true;
        inspectSelection("SELECT");
        return true;
    }

    function beginCustomSelection() {
        if (selectionHoldTimer !== null) window.clearTimeout(selectionHoldTimer);
        selectionHoldTimer = null;
        selectWordAtPoint(selectionHoldX, selectionHoldY);
    }

    function startSelectionHold(event) {
        var point, caret, content;
        point = eventPoint(event);
        if (!point) return;
        content = selectionAncestor(event && event.target, "potion-editable-content");
        if (!content) {
            caret = caretAtPoint(point.x, point.y);
            content = caret && selectionAncestor(caret.startContainer, "potion-editable-content");
        }
        if (busy || !content) return;
        clearSelectionHoldTimer();
        selectionHoldActive = false;
        selectionDragActive = false;
        selectionAnchor = null;
        selectionHoldX = point.x;
        selectionHoldY = point.y;
        selectionHoldStartedAt = new Date().getTime();
        selectionHoldTimer = window.setTimeout(beginCustomSelection, 700);
    }

    function moveCustomSelection(event) {
        var point = eventPoint(event), elapsed, caret, content, before, range, selection;
        if (!point) return;
        if (!selectionHoldActive) {
            if (selectionHoldTimer === null) return;
            elapsed = new Date().getTime() - selectionHoldStartedAt;
            if (elapsed >= 700) {
                beginCustomSelection();
                if (!selectionHoldActive) return;
            } else if (Math.abs(point.x - selectionHoldX) > 28 ||
                       Math.abs(point.y - selectionHoldY) > 28) {
                clearSelectionHoldTimer();
                selectionHoldStartedAt = 0;
                return;
            } else return;
        }
        if (event.preventDefault) event.preventDefault();
        if (!selectionDragActive) {
            if (Math.abs(point.x - selectionHoldX) <= 28 &&
                Math.abs(point.y - selectionHoldY) <= 28) return;
            selectionDragActive = true;
        }
        caret = caretAtPoint(point.x, point.y);
        content = caret && selectionAncestor(caret.startContainer, "potion-editable-content");
        if (!caret || !content || content !== selectionAncestor(selectionAnchor.node, "potion-editable-content")) return;
        if (caret.startContainer === selectionAnchor.node)
            before = caret.startOffset < selectionAnchor.start;
        else
            before = !!(selectionAnchor.node.compareDocumentPosition(caret.startContainer) & 2);
        range = document.createRange();
        if (before) {
            range.setStart(caret.startContainer, caret.startOffset);
            range.setEnd(selectionAnchor.node, selectionAnchor.end);
        } else {
            range.setStart(selectionAnchor.node, selectionAnchor.start);
            range.setEnd(caret.startContainer, caret.startOffset);
        }
        selection = window.getSelection();
        selection.removeAllRanges();
        selection.addRange(range);
        scheduleDragSelectionInspection();
    }

    function finishSelectionHold(event) {
        var elapsed = selectionHoldStartedAt ?
                new Date().getTime() - selectionHoldStartedAt : 0,
            held = selectionHoldTimer !== null && elapsed >= 700;
        clearSelectionHoldTimer();
        clearDragSelectionInspection();
        if (held) beginCustomSelection();
        if (selectionHoldActive) {
            moveCustomSelection(event);
            clearDragSelectionInspection();
            selectionHoldActive = false;
            selectionDragActive = false;
            inspectSelection("SELECT");
            window.setTimeout(function() { selectionSuppressClick = false; }, 800);
        }
        selectionHoldStartedAt = 0;
    }

    function setSelectionButtonsDisabled(value) {
        var buttons = id("selection-menu").getElementsByTagName("button"), i;
        for (i = 0; i < buttons.length; ++i) buttons[i].disabled = !!value;
    }

    function unwrapElement(element) {
        var parent = element.parentNode;
        while (element.firstChild) parent.insertBefore(element.firstChild, element);
        parent.removeChild(element);
    }

    function cleanSelectionFragment(root, format) {
        var elements = [], node;
        function collect(parent) {
            var child = parent.firstChild;
            while (child) {
                if (child.nodeType === 1) {
                    collect(child);
                    elements.push(child);
                }
                child = child.nextSibling;
            }
        }
        collect(root);
        while (elements.length) {
            node = elements.shift();
            if (!node.parentNode) continue;
            var tag = node.tagName.toLowerCase(), classes = " " + node.className + " ", remove = false;
            if (format === "bold") remove = tag === "strong" || tag === "b";
            else if (format === "underline") remove = tag === "u";
            else if (format === "highlight") remove = classes.indexOf(" notion-color ") >= 0;
            else if (format === "clear")
                remove = tag === "strong" || tag === "b" || tag === "em" || tag === "i" ||
                    tag === "u" || tag === "s" || tag === "strike" || tag === "del" ||
                    classes.indexOf(" notion-color ") >= 0;
            if (remove) unwrapElement(node);
        }
    }

    function applySelectionLocally(state, format, enabled) {
        var wrapper, fragment;
        if (!state || !state.range) return false;
        try {
            fragment = state.range.extractContents();
            cleanSelectionFragment(fragment, format);
            if (format !== "clear" && enabled) {
                wrapper = document.createElement(format === "bold" ? "strong" : format === "underline" ? "u" : "span");
                if (format === "highlight") wrapper.className = "notion-color notion-color-yellow-bg";
                wrapper.appendChild(fragment);
                state.range.insertNode(wrapper);
            } else state.range.insertNode(fragment);
            if (state.content.normalize) state.content.normalize();
            return true;
        } catch (ignored) {}
        return false;
    }

    /* OPTIMISTIC_FORMATTING_BEGIN */
    function selectionContentIsCurrent(pageId, content) {
        var node = content, root = id("page-content");
        if (pageId !== currentPageId) return false;
        while (node) {
            if (node === root) return true;
            node = node.parentNode;
        }
        return false;
    }

    function refreshAfterLocalFormatting() {
        collectReadingBlocks();
        applyNightPageAppearance();
        updateScroll();
    }

    function formatSelection(format) {
        var state = selectionState, pageId, previousHtml, enabled, body;
        if (!state || busy || formatSaveBusy) return;
        pageId = currentPageId;
        previousHtml = state.content.innerHTML;
        enabled = format !== "clear" && !state.formats[format];
        body = "editableIndex=" + state.editableIndex +
            "&start=" + state.start +
            "&end=" + state.end +
            "&format=" + encodeURIComponent(format) +
            "&blockText=" + encodeURIComponent(state.blockText) +
            "&selectedText=" + encodeURIComponent(state.selectedText);
        formatSaveBusy = true;
        warning("");
        if (!applySelectionLocally(state, format, enabled)) {
            state.content.innerHTML = previousHtml;
            formatSaveBusy = false;
            warning("Formatting could not be applied.");
            return;
        }
        clearSelectionMenu(true);
        refreshAfterLocalFormatting();
        request(
            "POST",
            "/api/pages/" + encodeURIComponent(pageId) + "/format",
            body,
            function(error, result) {
                var authoritativeEnabled;
                formatSaveBusy = false;
                setSelectionButtonsDisabled(false);
                if (error) {
                    if (selectionContentIsCurrent(pageId, state.content)) {
                        state.content.innerHTML = previousHtml;
                        refreshAfterLocalFormatting();
                    }
                    warning("Formatting could not be saved and was reverted. " + error);
                    return;
                }
                authoritativeEnabled = !!(result && result.enabled);
                if (authoritativeEnabled !== enabled &&
                    selectionContentIsCurrent(pageId, state.content)) {
                    state.content.innerHTML = previousHtml;
                    state.range = logicalRange(state.content, state.start, state.end);
                    applySelectionLocally(state, format, authoritativeEnabled);
                    refreshAfterLocalFormatting();
                }
            }
        );
    }
    /* OPTIMISTIC_FORMATTING_END */

    function warning(message) {
        id("warning").innerHTML = "";
        id("warning").appendChild(document.createTextNode(message || ""));
        if (message) show(id("warning"));
        else hide(id("warning"));
    }

    function activeScroll() {
        if (id("reader-view").className.indexOf("hidden") < 0) return id("page-content");
        if (id("pages-view").className.indexOf("hidden") < 0) return id("pages");
        return null;
    }

    function updateScroll() {
        var target = activeScroll(),
            max;
        if (!target) {
            hide(id("scroll-up"));
            hide(id("scroll-down"));
            return;
        }
        max = Math.max(0, target.scrollHeight - target.clientHeight);
        if (target.scrollTop > 6) show(id("scroll-up"));
        else hide(id("scroll-up"));
        if (target.scrollTop < max - 6) show(id("scroll-down"));
        else hide(id("scroll-down"));
    }

    function pageScroll(direction) {
        var target = activeScroll(),
            distance, max;
        if (!target) return;
        distance = Math.floor(target.clientHeight * .8);
        max = Math.max(0, target.scrollHeight - target.clientHeight);
        target.scrollTop = Math.max(0, Math.min(max, target.scrollTop + direction * distance));
        updateScroll();
        scheduleTransientReadingPositionUpdate();
        scheduleReadingPositionSave();
    }

    function readyForInput() {
        return !busy &&
            id("selection-menu").className.indexOf("hidden") >= 0 &&
            id("connect-view").className.indexOf("hidden") >= 0 &&
            id("settings-dialog").className.indexOf("hidden") >= 0 &&
            id("logout-dialog").className.indexOf("hidden") >= 0 &&
            id("about-dialog").className.indexOf("hidden") >= 0 &&
            !!activeScroll();
    }

    var pageButtonDownCode = 0,
        pageButtonDownAt = 0;

    function handlePageButtonAction(action) {
        var down;
        if (!readyForInput()) return;
        down = (pageButtonMode === "normal" && action === "forward") ||
            (pageButtonMode === "reversed" && action === "backward");
        pageScroll(down ? 1 : -1);
    }

    function pageButtonKeyDown(event) {
        var code, now;
        event = event || window.event;
        code = event.keyCode || event.which;
        if (code !== 33 && code !== 34) return;
        if (event.preventDefault) event.preventDefault();
        event.returnValue = false;
        if (event.stopPropagation) event.stopPropagation();
        event.cancelBubble = true;
        now = new Date().getTime();
        if (pageButtonDownCode === code && now - pageButtonDownAt < 1000)
            return false;
        pageButtonDownCode = code;
        pageButtonDownAt = now;
        handlePageButtonAction(code === 34 ? "forward" : "backward");
        return false;
    }

    function pageButtonKeyUp(event) {
        var code;
        event = event || window.event;
        code = event.keyCode || event.which;
        if (code === pageButtonDownCode) pageButtonDownCode = 0;
    }

    document.addEventListener("keydown", pageButtonKeyDown, false);
    document.addEventListener("keyup", pageButtonKeyUp, false);
    if (window.location.protocol === "http:")
        window.potionSimulatorPageButton = handlePageButtonAction;

    function saveSetting(key, value) {
        request(
            "POST",
            "/api/settings",
            "key=" + encodeURIComponent(key) + "&value=" + encodeURIComponent(value),
            function(error) {
                if (error) warning(error);
            }
        );
    }

    function applyAppearance(persist) {
        clearSelectionMenu(true);
        restoreNightPalette(id("page-content"));
        id("page-content").style.fontFamily = fonts[pageFont];
        id("page-content").style.fontSize = Math.round(30 * fontScale) + "px";

        if (night) {
            if (document.documentElement.className.indexOf("night-mode") < 0)
                document.documentElement.className += " night-mode";
            id("night").innerHTML = "&#9788;";
        } else {
            document.documentElement.className =
                document.documentElement.className.replace(/(^|\s)night-mode(?=\s|$)/g, "");
            id("night").innerHTML = "&#9789;";
        }

        var brandLogo = id("brand-logo"), brandNightSrc;
        if (brandLogo) {
            if (!brandLogoDaySrc) brandLogoDaySrc = brandLogo.getAttribute("src");
            brandNightSrc = brandLogo.getAttribute("data-night-src");
            if (brandNightSrc)
                brandLogo.src = night ? brandNightSrc : brandLogoDaySrc;
        }

        id("page-font").value = pageFont;

        var buttons = id("font-sizes").getElementsByTagName("button"),
            i;
        for (i = 0; i < buttons.length; ++i) {
            buttons[i].className =
                parseFloat(buttons[i].getAttribute("data-scale")) === fontScale ? "selected" : "";
            buttons[i].style.fontFamily = fonts[pageFont];
        }

        if (persist) {
            saveSetting("fontScale", String(fontScale));
            saveSetting("cardFont", pageFont);
            saveSetting("nightMode", night ? "1" : "0");
        }

        applyNightPageAppearance();
        scheduleMathRepair();
        scheduleImageLoad();
    }

    function parsedColor(value) {
        var match, hex, alpha;
        value = String(value || "").replace(/^\s+|\s+$/g, "").toLowerCase();
        if (!value || value === "transparent") return null;

        match = value.match(/^rgba?\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)(?:\s*,\s*([\d.]+))?\s*\)$/);
        if (match) {
            alpha = typeof match[4] === "undefined" ? 1 : parseFloat(match[4]);
            if (alpha === 0) return null;
            return [
                parseInt(match[1], 10),
                parseInt(match[2], 10),
                parseInt(match[3], 10)
            ];
        }

        match = value.match(/^#([0-9a-f]{3}|[0-9a-f]{6})$/);
        if (!match) return null;

        hex = match[1];
        if (hex.length === 3)
            hex =
                hex.charAt(0) + hex.charAt(0) +
                hex.charAt(1) + hex.charAt(1) +
                hex.charAt(2) + hex.charAt(2);

        return [
            parseInt(hex.substring(0, 2), 16),
            parseInt(hex.substring(2, 4), 16),
            parseInt(hex.substring(4, 6), 16)
        ];
    }

    function colorText(rgb) {
        return "rgb(" +
            Math.round(rgb[0]) + "," +
            Math.round(rgb[1]) + "," +
            Math.round(rgb[2]) + ")";
    }

    function nightFriendlyColor(value, background) {
        var rgb = parsedColor(value),
            luminance, factor;
        if (!rgb) return null;

        luminance = .299 * rgb[0] + .587 * rgb[1] + .114 * rgb[2];

        if (background) {
            if (luminance <= 55) return null;
            factor = 55 / luminance;
            return colorText([
                rgb[0] * factor,
                rgb[1] * factor,
                rgb[2] * factor
            ]);
        }

        if (luminance >= 190) return null;

        factor = (190 - luminance) / (255 - luminance);
        return colorText([
            rgb[0] + (255 - rgb[0]) * factor,
            rgb[1] + (255 - rgb[1]) * factor,
            rgb[2] + (255 - rgb[2]) * factor
        ]);
    }

    function saveNightStyle(element) {
        if (element._potionNightStyleSaved) return;
        element._potionNightStyleSaved = true;
        element._potionNightOriginalStyle = element.getAttribute("style");
        nightStyledElements.push(element);
    }

    function restoreNightPalette(root) {
        var elements = nightStyledElements,
            i, element;
        nightStyledElements = [];

        for (i = 0; i < elements.length; ++i) {
            element = elements[i];
            if (!element._potionNightStyleSaved) continue;

            if (element._potionNightOriginalStyle === null)
                element.removeAttribute("style");
            else
                element.setAttribute("style", element._potionNightOriginalStyle);

            try {
                delete element._potionNightStyleSaved;
                delete element._potionNightOriginalStyle;
            } catch (ignored) {
                element._potionNightStyleSaved = false;
                element._potionNightOriginalStyle = null;
            }
        }
    }

    function setNightStyle(element, property, value) {
        if (!value) return;

        saveNightStyle(element);

        if (element.style.setProperty)
            element.style.setProperty(property, value, "important");
        else
            element.style[property === "background-color" ? "backgroundColor" : property] = value;
    }

    function applyNightPalette(root) {
        var elements = [root],
            descendants = root.getElementsByTagName("*"),
            i, element, computed, parentComputed, foreground, background;

        for (i = 0; i < descendants.length; ++i)
            elements.push(descendants[i]);

        for (i = 0; i < elements.length; ++i) {
            element = elements[i];

            if (element.tagName === "IMG" || element.tagName === "CANVAS")
                continue;

            computed = window.getComputedStyle ?
                window.getComputedStyle(element, null) :
                element.currentStyle;

            if (!computed) continue;

            parentComputed =
                element.parentNode &&
                element.parentNode.nodeType === 1 &&
                window.getComputedStyle ?
                    window.getComputedStyle(element.parentNode, null) :
                    null;

            if (!parentComputed || computed.color !== parentComputed.color) {
                foreground = nightFriendlyColor(computed.color, false);
                setNightStyle(element, "color", foreground);
            }

            background = nightFriendlyColor(computed.backgroundColor, true);
            setNightStyle(element, "background-color", background);
        }
    }

    function restoreNightImage(image) {
        var original;
        if (!image._potionNightImageInverted) return;

        original = image._potionNightOriginalSource;
        image._potionNightImageInverted = false;
        image._potionNightOriginalSource = null;

        if (original)
            image.setAttribute("src", original);
    }

    function invertNightImage(image) {
        var width, height, scale, canvas, context, pixels, data, i, source;

        if (!night ||
            nightPageMode !== "palette-images" ||
            image._potionNightImageInverted ||
            image._potionNightImageBusy)
            return;

        width = image.naturalWidth || image.width;
        height = image.naturalHeight || image.height;
        source = image.getAttribute("src") || "";

        if (!width || !height || !source) return;

        scale = Math.min(1, 1600 / Math.max(width, height));
        width = Math.max(1, Math.round(width * scale));
        height = Math.max(1, Math.round(height * scale));
        image._potionNightImageBusy = true;

        try {
            canvas = document.createElement("canvas");
            canvas.width = width;
            canvas.height = height;

            context = canvas.getContext("2d");
            context.drawImage(image, 0, 0, width, height);

            pixels = context.getImageData(0, 0, width, height);
            data = pixels.data;

            for (i = 0; i < data.length; i += 4) {
                data[i] = 255 - data[i];
                data[i + 1] = 255 - data[i + 1];
                data[i + 2] = 255 - data[i + 2];
            }

            context.putImageData(pixels, 0, 0);

            image._potionNightOriginalSource = source;
            image._potionNightImageInverted = true;
            image.setAttribute("src", canvas.toDataURL("image/png"));
        } catch (ignored) {
            image._potionNightImageInverted = false;
            image._potionNightOriginalSource = null;
        }

        image._potionNightImageBusy = false;
        updateScroll();
    }

    function applyNightPageAppearance() {
        var root = id("page-content"),
            images, i;

        if (!root) return;

        restoreNightPalette(root);

        if (night && nightPageMode !== "standard")
            applyNightPalette(root);

        images = root.getElementsByTagName("img");

        for (i = 0; i < images.length; ++i) {
            if (night && nightPageMode === "palette-images")
                invertNightImage(images[i]);
            else
                restoreNightImage(images[i]);
        }
    }

    function selectedRadio(name, value) {
        var choices = document.getElementsByName(name),
            i;
        for (i = 0; i < choices.length; ++i)
            choices[i].checked = choices[i].value === value;
    }

    var settingsTooltipSource = null;

    function hideSettingsTooltip() {
        hide(id("settings-tooltip"));
        settingsTooltipSource = null;
    }

    function showSettingsTooltip(button) {
        var tooltip = id("settings-tooltip"),
            text = button.getAttribute("data-help") || "",
            bounds, left, top;

        if (settingsTooltipSource === button &&
            tooltip.className.indexOf("hidden") < 0) {
            hideSettingsTooltip();
            return;
        }

        clear(tooltip);
        tooltip.appendChild(document.createTextNode(text));
        show(tooltip);

        bounds = button.getBoundingClientRect();

        left = Math.max(
            20,
            Math.min(
                window.innerWidth - tooltip.offsetWidth - 20,
                bounds.right - tooltip.offsetWidth
            )
        );

        top = bounds.bottom + 8;

        if (top + tooltip.offsetHeight > window.innerHeight - 20)
            top = Math.max(20, bounds.top - tooltip.offsetHeight - 8);

        tooltip.style.left = left + "px";
        tooltip.style.top = top + "px";
        settingsTooltipSource = button;
    }

    function connectView() {
        setBusy(false);
        hide(id("pages-view"));
        hide(id("reader-view"));
        show(id("connect-view"));
        hide(id("settings"));
        id("status").innerHTML = "Connect to Notion";
        updateScroll();
    }

    function resetMathRepair() {
        if (mathRepairTimer !== null) {
            window.clearTimeout(mathRepairTimer);
            mathRepairTimer = null;
        }
        mathNodes = [];
    }

    function collectMath() {
        var nodes = id("page-content").getElementsByClassName("math"),
            i;

        resetMathRepair();

        for (i = 0; i < nodes.length; ++i)
            mathNodes.push(nodes[i]);
    }

    function mathTop(node, root) {
        var top = 0;

        while (node && node !== root) {
            top += node.offsetTop || 0;
            node = node.offsetParent;
        }

        return top;
    }

    function clearReadingPositionState() {
        if (positionSaveTimer !== null) {
            window.clearTimeout(positionSaveTimer);
            positionSaveTimer = null;
        }

        if (positionRestoreTimer !== null) {
            window.clearTimeout(positionRestoreTimer);
            positionRestoreTimer = null;
        }

        if (viewportRestoreTimer !== null) {
            window.clearTimeout(viewportRestoreTimer);
            viewportRestoreTimer = null;
        }

        if (transientPositionTimer !== null) {
            window.clearTimeout(transientPositionTimer);
            transientPositionTimer = null;
        }

        readingBlocks = [];
        positionRestoring = false;
        positionRestoreAnchor = null;
        transientReadingPosition = null;
        readingViewportWidth = 0;
        id("page-content").style.visibility = "";
    }

    function collectReadingBlocks() {
        var nodes = id("page-content").getElementsByClassName("potion-block"),
            i;

        readingBlocks = [];

        for (i = 0; i < nodes.length; ++i)
            readingBlocks.push(nodes[i]);

        readingViewportWidth = id("page-content").clientWidth;
    }

    function currentReadingPosition() {
        var root = id("page-content"),
            visible = [],
            i, low, high, middle, selected, top, height, fraction;

        for (i = 0; i < readingBlocks.length; ++i)
            if (readingBlocks[i].offsetParent !== null)
                visible.push({
                    block: readingBlocks[i],
                    index: i,
                    top: mathTop(readingBlocks[i], root)
                });

        if (!visible.length) return null;

        low = 0;
        high = visible.length - 1;
        selected = 0;

        while (low <= high) {
            middle = Math.floor((low + high) / 2);

            if (visible[middle].top <= root.scrollTop) {
                selected = middle;
                low = middle + 1;
            } else {
                high = middle - 1;
            }
        }

        top = visible[selected].top;
        height = Math.max(1, visible[selected].block.offsetHeight || 1);
        fraction = Math.max(
            0,
            Math.min(1, (root.scrollTop - top) / height)
        );

        return {
            blockIndex: visible[selected].index,
            blockFraction: Math.round(fraction * 65535)
        };
    }

    function saveCurrentReadingPosition(done) {
        var pageId = currentPageId,
            position;

        if (positionSaveTimer !== null) {
            window.clearTimeout(positionSaveTimer);
            positionSaveTimer = null;
        }

        done = done || function() {};

        if (positionRestoring ||
            !pageId ||
            id("reader-view").className.indexOf("hidden") >= 0 ||
            !readingBlocks.length) {
            done();
            return;
        }

        position = currentReadingPosition();

        if (!position) {
            done();
            return;
        }

        transientReadingPosition = position;

        request(
            "POST",
            "/api/pages/" + encodeURIComponent(pageId) + "/position",
            "blockIndex=" + position.blockIndex +
                "&blockFraction=" + position.blockFraction,
            function(error) {
                if (error) warning(error);
                done();
            }
        );
    }

    function scheduleReadingPositionSave() {
        if (positionRestoring ||
            !currentPageId ||
            id("reader-view").className.indexOf("hidden") >= 0)
            return;

        if (positionSaveTimer !== null)
            window.clearTimeout(positionSaveTimer);

        positionSaveTimer = window.setTimeout(function() {
            positionSaveTimer = null;
            saveCurrentReadingPosition();
        }, 5000);
    }

    function rememberCurrentReadingPosition() {
        var position;
        if (positionRestoring ||
            !currentPageId ||
            id("reader-view").className.indexOf("hidden") >= 0 ||
            !readingBlocks.length)
            return;
        position = currentReadingPosition();
        if (position) transientReadingPosition = position;
    }

    function scheduleTransientReadingPositionUpdate() {
        if (positionRestoring) return;
        if (readingViewportWidth &&
            id("page-content").clientWidth !== readingViewportWidth) {
            scheduleViewportReadingRestore();
            return;
        }
        if (transientPositionTimer === null) {
            rememberCurrentReadingPosition();
            transientPositionTimer = window.setTimeout(function() {
                transientPositionTimer = null;
                rememberCurrentReadingPosition();
            }, TRANSIENT_POSITION_UPDATE_MS);
        }
    }

    /* READING_POSITION_MAINTENANCE_BEGIN */
    function readingRestoreAffects(node) {
        var root = id("page-content"), anchor = positionRestoreAnchor,
            anchorBottom;
        if (!anchor || !node || node.offsetParent === null) return false;
        anchorBottom = mathTop(anchor.block, root) +
            Math.max(1, anchor.block.offsetHeight || 1);
        return mathTop(node, root) <= anchorBottom;
    }

    function applyReadingRestoreAnchor() {
        var root = id("page-content"), anchor = positionRestoreAnchor,
            top, height, maximum, index, block;
        if (!anchor || !readingBlocks.length) return;
        block = anchor.block;
        index = anchor.blockIndex;
        while (index > 0 && (!block || block.offsetParent === null))
            block = readingBlocks[--index];
        if (!block || block.offsetParent === null) return;
        anchor.block = block;
        top = mathTop(block, root);
        height = Math.max(1, block.offsetHeight || 1);
        maximum = Math.max(0, root.scrollHeight - root.clientHeight);
        root.scrollTop = Math.max(
            0,
            Math.min(maximum, Math.round(top + height * anchor.fraction))
        );
        updateScroll();
    }

    function readingRestoreHasPendingLayout() {
        var i, image;
        if (!positionRestoreAnchor) return false;
        if (mathRepairTimer !== null) return true;
        for (i = 0; i < imageNodes.length; ++i) {
            image = imageNodes[i];
            if (!readingRestoreAffects(image)) continue;
            if (image._potionImageLoading ||
                (image._potionImageRequested && !image.complete) ||
                (!image._potionImageRequested &&
                    image._potionImageAttempts < 2 &&
                    image.getAttribute("data-src")))
                return true;
        }
        return false;
    }

    function finishReadingRestore() {
        var root = id("page-content");
        if (positionRestoreTimer !== null) {
            window.clearTimeout(positionRestoreTimer);
            positionRestoreTimer = null;
        }
        applyReadingRestoreAnchor();
        positionRestoring = false;
        positionRestoreAnchor = null;
        root.style.visibility = "";
        transientReadingPosition = currentReadingPosition();
    }

    function armReadingRestoreQuietPeriod() {
        if (!positionRestoreAnchor) return;
        if (positionRestoreTimer !== null)
            window.clearTimeout(positionRestoreTimer);
        positionRestoreTimer = window.setTimeout(function() {
            positionRestoreTimer = null;
            if (!positionRestoreAnchor) return;
            if (readingRestoreHasPendingLayout()) {
                armReadingRestoreQuietPeriod();
                return;
            }
            finishReadingRestore();
        }, POSITION_RESTORE_QUIET_MS);
    }

    function maintainReadingRestore(node, force) {
        if (!positionRestoreAnchor || (!force && !readingRestoreAffects(node)))
            return;
        applyReadingRestoreAnchor();
        armReadingRestoreQuietPeriod();
    }

    function beginReadingRestore(position, initiallyHidden) {
        var root = id("page-content"),
            index, fraction, block;

        if (!position || !readingBlocks.length) {
            root.scrollTop = 0;
            root.style.visibility = "";
            positionRestoring = false;
            positionRestoreAnchor = null;
            transientReadingPosition = currentReadingPosition();
            return false;
        }

        index = parseInt(position.blockIndex, 10);
        if (!isFinite(index) || index < 0)
            index = 0;

        index = Math.min(index, readingBlocks.length - 1);

        fraction = parseInt(position.blockFraction, 10);
        if (!isFinite(fraction) || fraction < 0)
            fraction = 0;

        fraction = Math.min(fraction, 65535) / 65535;
        block = readingBlocks[index];

        while (index > 0 && block.offsetParent === null)
            block = readingBlocks[--index];

        if (positionRestoreTimer !== null) {
            window.clearTimeout(positionRestoreTimer);
            positionRestoreTimer = null;
        }
        positionRestoreAnchor = {
            blockIndex: index,
            blockFraction: Math.round(fraction * 65535),
            fraction: fraction,
            block: block
        };
        positionRestoring = true;
        if (initiallyHidden) root.style.visibility = "hidden";
        applyReadingRestoreAnchor();
        root.style.visibility = "";
        scheduleMathRepair(0);
        scheduleImageLoad(0);
        armReadingRestoreQuietPeriod();
        return true;
    }

    function restoreReadingPosition(position) {
        beginReadingRestore(position, true);
    }

    function scheduleViewportReadingRestore() {
        if (!currentPageId ||
            id("reader-view").className.indexOf("hidden") >= 0 ||
            !readingBlocks.length) {
            updateScroll();
            return;
        }
        if (transientPositionTimer !== null) {
            window.clearTimeout(transientPositionTimer);
            transientPositionTimer = null;
        }
        if (viewportRestoreTimer !== null)
            window.clearTimeout(viewportRestoreTimer);
        viewportRestoreTimer = window.setTimeout(function() {
            var position;
            viewportRestoreTimer = null;
            updateScroll();
            readingViewportWidth = id("page-content").clientWidth;
            if (positionRestoreAnchor) {
                maintainReadingRestore(null, true);
                return;
            }
            position = transientReadingPosition || currentReadingPosition();
            if (position) beginReadingRestore(position, false);
        }, 0);
    }
    /* READING_POSITION_MAINTENANCE_END */

    /*
     * Mesquite math compatibility
     *
     * KaTeX relies heavily on inline-table vertical lists. Mesquite has two
     * relevant layout problems:
     *
     *  1. Ordinary msupsub scripts are not positioned correctly.
     *  2. For two-row .vlist-t2 structures, Mesquite does not include the
     *     final depth row when determining the inline-table baseline.
     *
     * Ordinary scripts outside fractions are reconstructed below. Native
     * scripts inside fractions remain intact because changing their dimensions
     * after KaTeX has laid out the fraction can invalidate the surrounding
     * fraction geometry.
     *
     * All intact .vlist-t2 structures are handled generically afterwards.
     */

    function directSpans(node) {
        var result = [],
            children = node ? node.childNodes : [],
            i;

        for (i = 0; i < children.length; ++i)
            if (children[i].nodeType === 1 &&
                children[i].tagName.toLowerCase() === "span")
                result.push(children[i]);

        return result;
    }

    function hasClass(node, className) {
        return !!node &&
            (" " + String(node.className || "") + " ")
                .indexOf(" " + className + " ") >= 0;
    }

    function directSpansWithClass(node, className) {
        var spans = directSpans(node),
            result = [],
            i;

        for (i = 0; i < spans.length; ++i)
            if (hasClass(spans[i], className))
                result.push(spans[i]);

        return result;
    }

    function positionedContents(vlist) {
        var result = [],
            children = directSpans(vlist),
            i, parts;

        for (i = 0; i < children.length; ++i) {
            parts = directSpans(children[i]);

            if (parts.length > 1)
                result.push({
                    position: children[i],
                    content: parts[parts.length - 1],
                    top: parseFloat(children[i].style.top || "0")
                });
        }

        return result;
    }

    function isInsideMathStructure(node, className) {
        while (node) {
            if (hasClass(node, className))
                return true;
            node = node.parentNode;
        }

        return false;
    }

    function repairKindleScripts(root) {
        var live = root.getElementsByClassName("msupsub"),
            nodes = [],
            i, node, vlists, positions, isSub,
            sup, sub, wrapper, width;

        /*
         * Snapshot the live collection before modifying any of its members.
         */
        for (i = 0; i < live.length; ++i)
            nodes.push(live[i]);

        for (i = 0; i < nodes.length; ++i) {
            node = nodes[i];

            /*
             * Do not reconstruct scripts inside fractions.
             *
             * The surrounding KaTeX fraction has already reserved dimensions
             * for the native script box. Rebuilding it afterwards changes the
             * box geometry without re-running KaTeX's fraction layout.
             *
             * The generic vlist baseline repair handles these native nested
             * scripts safely.
             */
            if (isInsideMathStructure(node, "mfrac"))
                continue;

            vlists = node.getElementsByClassName("vlist");
            if (!vlists.length)
                continue;

            positions = positionedContents(vlists[0]);

            if (!positions.length || positions.length > 2)
                continue;

            isSub = node.getElementsByClassName("vlist-t2").length > 0;

            sub = positions.length === 2 || isSub ?
                positions[0] :
                null;

            sup = positions.length === 2 ?
                positions[1] :
                (!isSub ? positions[0] : null);

            clear(node);
            node.className += " potion-script";

            if (sub && sup) {
                node.className += " potion-script-both";

                wrapper = document.createElement("span");
                wrapper.className = "potion-script-sup";
                wrapper.appendChild(sup.content);
                node.appendChild(wrapper);
                sup = wrapper;

                wrapper = document.createElement("span");
                wrapper.className = "potion-script-sub";
                wrapper.appendChild(sub.content);
                node.appendChild(wrapper);
                sub = wrapper;

                width = Math.max(sup.offsetWidth, sub.offsetWidth);
                node.style.width = width + "px";

                sup.style.left =
                    Math.max(0, (width - sup.offsetWidth) / 2) + "px";

                sub.style.left =
                    Math.max(0, (width - sub.offsetWidth) / 2) + "px";
            } else if (sub) {
                node.className += " potion-script-sub-only";
                node.appendChild(sub.content);
            } else if (sup) {
                node.className += " potion-script-sup-only";
                node.appendChild(sup.content);
            }
        }
    }

    /*
     * KaTeX represents a vertical stack extending below the baseline as:
     *
     *   .vlist-t.vlist-t2
     *       .vlist-r       visible stack
     *       .vlist-r       encoded depth below the baseline
     *
     * Mesquite ignores the second row when deriving the baseline of the
     * inline-table. KaTeX has already calculated the correct depth, so restore
     * that baseline explicitly rather than reconstructing each kind of math
     * object separately.
     *
     * This one repair covers fractions, nested scripts, operator limits,
     * radicals, matrices and other KaTeX constructs that use vlist-t2.
     */
    function repairKindleVlistBaselines(root) {
        var live = root.getElementsByClassName("vlist-t2"),
            tables = [],
            i, rows, cells, depth;

        /*
         * Snapshot the live collection before changing styles.
         */
        for (i = 0; i < live.length; ++i)
            tables.push(live[i]);

        for (i = 0; i < tables.length; ++i) {
            /*
             * Only inspect direct rows and cells. Descendant searches would
             * incorrectly mix nested vertical lists with this table's own
             * depth row.
             */
            rows = directSpansWithClass(tables[i], "vlist-r");

            if (rows.length < 2)
                continue;

            cells = directSpansWithClass(
                rows[rows.length - 1],
                "vlist"
            );

            if (!cells.length)
                continue;

            depth = parseFloat(cells[0].style.height || "");

            if (!isFinite(depth) || depth <= 0)
                continue;

            tables[i].style.verticalAlign = "-" + depth + "em";
        }
    }

    function repairKindleMath(root) {
        if (!kindleMathLayout())
            return;

        /*
         * Reconstruct only ordinary scripts for which Mesquite cannot
         * reproduce KaTeX's positioning correctly.
         */
        repairKindleScripts(root);

        /*
         * Then repair every remaining native two-row KaTeX vertical list.
         * This replaces the previous fraction/operator/radical/matrix
         * special-case baseline fixes.
         */
        repairKindleVlistBaselines(root);
    }

    function repairMathNearViewport() {
        var root = id("page-content"),
            limit = root.scrollTop + root.clientHeight * 2.5,
            i, node, repaired = 0, restoreChanged = false;

        mathRepairTimer = null;

        if (id("reader-view").className.indexOf("hidden") >= 0)
            return;

        for (i = 0; i < mathNodes.length; ++i) {
            node = mathNodes[i];

            if (node._potionMathRepaired ||
                node.offsetParent === null)
                continue;

            if (mathTop(node, root) > limit)
                break;

            node._potionMathRepaired = true;
            repairKindleMath(node);
            if (readingRestoreAffects(node)) restoreChanged = true;
            repaired++;

            if (repaired >= 16) {
                scheduleMathRepair(20);
                break;
            }
        }

        updateScroll();
        if (restoreChanged)
            maintainReadingRestore(null, true);
        else if (positionRestoreAnchor)
            armReadingRestoreQuietPeriod();
    }

    function scheduleMathRepair(delay) {
        if (mathRepairTimer !== null || !mathNodes.length)
            return;

        mathRepairTimer = window.setTimeout(
            repairMathNearViewport,
            typeof delay === "number" ? delay : 40
        );
    }

    function resetImageLoading() {
        if (imageLoadTimer !== null) {
            window.clearTimeout(imageLoadTimer);
            imageLoadTimer = null;
        }

        imageNodes = [];
        imageLoads = 0;
        imageGeneration++;
    }

    function scheduleImageLoad(delay) {
        if (imageLoadTimer !== null || !imageNodes.length)
            return;

        imageLoadTimer = window.setTimeout(
            loadImagesNearViewport,
            typeof delay === "number" ? delay : 40
        );
    }

    function imageFinished(image, loaded) {
        if (image._potionImageGeneration !== imageGeneration)
            return;

        if (image._potionImageLoading) {
            image._potionImageLoading = false;
            imageLoads = Math.max(0, imageLoads - 1);
        }

        if (loaded) {
            invertNightImage(image);
            updateScroll();
            scheduleMathRepair();
        } else {
            image._potionImageRequested = false;
        }

        maintainReadingRestore(image, false);

        scheduleImageLoad(loaded ? 20 : 500);
    }

    function loadImagesNearViewport() {
        var root = id("page-content"),
            limit = root.scrollTop + root.clientHeight * 2.5,
            i, image, source;

        imageLoadTimer = null;

        if (id("reader-view").className.indexOf("hidden") >= 0)
            return;

        for (i = 0; i < imageNodes.length && imageLoads < 2; ++i) {
            image = imageNodes[i];

            if (image._potionImageRequested ||
                image._potionImageAttempts >= 2 ||
                image.offsetParent === null)
                continue;

            if (mathTop(image, root) > limit)
                break;

            source = image.getAttribute("data-src");

            if (!source)
                continue;

            image._potionImageRequested = true;
            image._potionImageLoading = true;
            image._potionImageAttempts++;
            image._potionImageGeneration = imageGeneration;
            imageLoads++;

            if (readingRestoreAffects(image))
                armReadingRestoreQuietPeriod();

            image.setAttribute(
                "src",
                source +
                    (image._potionImageAttempts > 1 ?
                        "?potion_retry=" + image._potionImageAttempts :
                        "")
            );
        }
    }

    function prepareImages() {
        var images = id("page-content").getElementsByTagName("img"),
            i, source;

        resetImageLoading();

        for (i = 0; i < images.length; ++i) {
            imageNodes.push(images[i]);

            source = images[i].getAttribute("src");

            if (source && !images[i].getAttribute("data-src"))
                images[i].setAttribute("data-src", source);

            images[i]._potionImageAttempts = source ? 1 : 0;
            images[i]._potionImageRequested = !!source;
            images[i]._potionImageGeneration = imageGeneration;

            images[i].onload = function() {
                imageFinished(this, true);
            };

            images[i].onerror = function() {
                imageFinished(this, false);
            };

            images[i].onclick = function() {
                this.className =
                    this.className === "expanded" ? "" : "expanded";
                updateScroll();
                maintainReadingRestore(this, false);
                scheduleMathRepair();
                scheduleImageLoad();
            };

            if (source && images[i].complete) {
                if (images[i].naturalWidth)
                    invertNightImage(images[i]);
                else
                    images[i]._potionImageRequested = false;
            }
        }

        scheduleImageLoad(0);
    }

    function prepareToggles() {
        var buttons =
                id("page-content").getElementsByClassName("toggle-summary"),
            i;

        for (i = 0; i < buttons.length; ++i)
            buttons[i].onclick = function() {
                var content = this.nextSibling,
                    expanded =
                        this.getAttribute("aria-expanded") === "true",
                    arrows =
                        this.getElementsByClassName("toggle-arrow");

                this.setAttribute(
                    "aria-expanded",
                    expanded ? "false" : "true"
                );

                if (expanded)
                    hide(content);
                else
                    show(content);

                if (arrows.length)
                    arrows[0].innerHTML =
                        expanded ? "&#9656;" : "&#9662;";

                updateScroll();
                maintainReadingRestore(this, false);
                scheduleMathRepair();
                scheduleImageLoad();
            };
    }

    function preparePageLinks() {
        var links =
                id("page-content").getElementsByClassName("child-page"),
            inlineLinks =
                id("page-content").getElementsByClassName("notion-page-link"),
            i,
            openLinkedPage = function() {
                openPage(
                    this.getAttribute("data-page-id"),
                    "child"
                );
                return false;
            };

        for (i = 0; i < links.length; ++i)
            if (links[i].getAttribute("data-page-id"))
                links[i].onclick = openLinkedPage;
        for (i = 0; i < inlineLinks.length; ++i)
            if (inlineLinks[i].getAttribute("data-page-id"))
                inlineLinks[i].onclick = openLinkedPage;
    }

    function setPinButton(button, pinned) {
        if (!button) return;
        button.innerHTML = pinned ? "&#9733;" : "&#9734;";
        button.setAttribute("aria-pressed", pinned ? "true" : "false");
        button.setAttribute("aria-label", pinned ? "Unpin page" : "Pin page");
    }

    function setSortButtons() {
        var opened = id("sort-opened"),
            edited = id("sort-edited"),
            openedActive = pageSortMode === "opened";
        opened.className = openedActive ? "active" : "";
        edited.className = openedActive ? "" : "active";
        opened.setAttribute("aria-pressed", openedActive ? "true" : "false");
        edited.setAttribute("aria-pressed", openedActive ? "false" : "true");
    }

    /* PAGE_NAVIGATION_LOGIC_BEGIN */
    function pageIdKey(pageId) {
        return (pageId || "").replace(/-/g, "").toLowerCase();
    }

    function cachedPage(pageId) {
        var wanted = pageIdKey(pageId), i;
        for (i = 0; i < pageList.length; ++i)
            if (pageIdKey(pageList[i].id) === wanted) return pageList[i];
        return null;
    }

    function updatePageMetadata(pageId, key, value) {
        var page = cachedPage(pageId);
        if (page) page[key] = value;
        return page;
    }

    function comparePageTitles(left, right) {
        var a = left.title || "", b = right.title || "";
        if (a < b) return -1;
        if (a > b) return 1;
        return 0;
    }

    function comparePageEdited(left, right) {
        var a = left.edited || "", b = right.edited || "";
        if (a > b) return -1;
        if (a < b) return 1;
        return comparePageTitles(left, right);
    }

    function comparePages(left, right) {
        var leftOpened, rightOpened;
        if ((left.pinned === true) !== (right.pinned === true))
            return left.pinned === true ? -1 : 1;
        if (pageSortMode === "opened") {
            leftOpened = Number(left.opened) || 0;
            rightOpened = Number(right.opened) || 0;
            if (leftOpened !== rightOpened) return rightOpened - leftOpened;
        }
        return comparePageEdited(left, right);
    }

    function sortPages() {
        pageList.sort(comparePages);
    }
    /* PAGE_NAVIGATION_LOGIC_END */

    function setPagePinned(pageId, pinned, button) {
        if (button) button.disabled = true;
        request(
            "POST",
            "/api/pages/" + encodeURIComponent(pageId) + "/pin",
            "pinned=" + (pinned ? "1" : "0"),
            function(error, result) {
                if (error) {
                    if (button) button.disabled = false;
                    warning(error);
                    return;
                }
                pinned = result && result.pinned === true;
                updatePageMetadata(pageId, "pinned", pinned);
                if (pageIdKey(currentPageId) === pageIdKey(pageId)) {
                    currentPagePinned = pinned;
                    setPinButton(id("page-pin"), pinned);
                }
                if (button) button.disabled = false;
                renderSortedPages(false);
            }
        );
    }

    function pageDate(timestamp) {
        var date, month, day;
        if (!timestamp) return "Not opened yet";
        date = new Date(timestamp * 1000);
        month = date.getMonth() + 1;
        day = date.getDate();
        return date.getFullYear() + "-" +
            (month < 10 ? "0" : "") + month + "-" +
            (day < 10 ? "0" : "") + day;
    }

    function renderPageList(resetScroll) {
        var list = id("pages"), previousScroll = list.scrollTop, i;
        clear(list);

        for (i = 0; i < pageList.length; ++i) {
            (function(page) {
                var row = document.createElement("div"),
                    button = document.createElement("button"),
                    pin = document.createElement("button"),
                    icon = document.createElement("span"),
                    name = document.createElement("span"),
                    arrow = document.createElement("span"),
                    small = document.createElement("small");

                row.className = "page-row";
                row.setAttribute("data-page-id", page.id);
                button.className = "page-open";
                button.setAttribute("type", "button");
                pin.className = "page-pin";
                pin.setAttribute("type", "button");
                setPinButton(pin, page.pinned === true);

                icon.className = "page-icon";
                icon.appendChild(document.createTextNode("\u2637"));

                name.className = "page-name";
                name.appendChild(document.createTextNode(page.title));
                small.appendChild(document.createTextNode(
                    pageSortMode === "opened" ?
                        (page.opened ?
                            "Opened " + pageDate(page.opened) :
                            "Not opened yet") :
                        (page.edited ?
                            "Edited " + page.edited.substring(0, 10) :
                            "Not edited yet")
                ));
                name.appendChild(small);

                arrow.className = "page-arrow";
                arrow.innerHTML = "&rsaquo;";
                button.appendChild(icon);
                button.appendChild(name);
                button.appendChild(arrow);

                button.onclick = function() {
                    openPage(page.id);
                };
                pin.onclick = function() {
                    setPagePinned(
                        page.id,
                        pin.getAttribute("aria-pressed") !== "true",
                        pin
                    );
                };

                row.appendChild(button);
                row.appendChild(pin);
                list.appendChild(row);
            }(pageList[i]));
        }

        if (!pageList.length) {
            var empty = document.createElement("p");
            empty.appendChild(document.createTextNode(
                "No accessible pages found. " +
                "Share pages with your Notion connection, then search again."
            ));
            list.appendChild(empty);
        }

        list.scrollTop = resetScroll ? 0 : previousScroll;
    }

    function renderSortedPages(resetScroll) {
        sortPages();
        renderPageList(resetScroll);
        updateScroll();
    }

    function choosePageSort(mode) {
        var previous;
        if (busy || pageSortSaveBusy || mode === pageSortMode ||
            (mode !== "opened" && mode !== "edited")) return;
        previous = pageSortMode;
        pageSortMode = mode;
        setSortButtons();
        renderSortedPages(false);
        pageSortSaveBusy = true;
        id("sort-opened").disabled = true;
        id("sort-edited").disabled = true;
        request(
            "POST",
            "/api/settings",
            "key=pageSortMode&value=" + encodeURIComponent(mode),
            function(error) {
                pageSortSaveBusy = false;
                id("sort-opened").disabled = false;
                id("sort-edited").disabled = false;
                if (error) {
                    pageSortMode = previous;
                    setSortButtons();
                    renderSortedPages(false);
                    warning(error);
                }
            }
        );
    }

    function showPages(positionSaved) {
        if (busy) return;

        if (!positionSaved &&
            currentPageId &&
            id("reader-view").className.indexOf("hidden") < 0) {
            saveCurrentReadingPosition(function() {
                showPages(true);
            });
            return;
        }

        clearSelectionMenu(true);
        resetMathRepair();
        resetImageLoading();
        clearReadingPositionState();

        pageHistory = [];
        currentPageId = "";
        currentPagePinned = false;

        if (!pageListLoaded) {
            loadPages();
            return;
        }

        renderSortedPages(false);

        hide(id("reader-view"));
        show(id("pages-view"));

        id("status").innerHTML = "Pages";

        updateScroll();
    }

    function loadPages() {
        setBusy(true);
        warning("");
        id("status").innerHTML = "Loading Notion pages...";

        hide(id("connect-view"));
        hide(id("reader-view"));
        show(id("pages-view"));
        show(id("settings"));

        request(
            "GET",
            "/api/pages?query=" +
                encodeURIComponent(id("search").value || ""),
            null,
            function(error, result) {
                if (error) {
                    setBusy(false);
                    warning(error);
                    id("status").innerHTML = "Notion unavailable";
                    return;
                }

                pageSortMode = result.sortMode === "edited" ? "edited" : "opened";
                pageList = result.pages || [];
                pageListLoaded = true;
                setSortButtons();
                renderSortedPages(true);

                id("status").innerHTML =
                    pageList.length +
                    " accessible page" +
                    (pageList.length === 1 ? "" : "s");

                setBusy(false);
            }
        );
    }

    function openPage(pageId, navigation, positionSaved) {
        if (busy) return;

        if (!positionSaved &&
            currentPageId &&
            id("reader-view").className.indexOf("hidden") < 0) {
            saveCurrentReadingPosition(function() {
                openPage(pageId, navigation, true);
            });
            return;
        }

        var previous = currentPageId;

        clearSelectionMenu(true);

        setBusy(true);
        warning("");
        show(id("settings"));
        id("status").innerHTML = "Loading page...";

        request(
            "GET",
            "/api/pages/" + encodeURIComponent(pageId),
            null,
            function(error, page) {
                if (error) {
                    setBusy(false);
                    warning(error);
                    return;
                }

                if (navigation === "child" && previous)
                    pageHistory.push(previous);
                else if (navigation === "back")
                    pageHistory.pop();
                else
                    pageHistory = [];

                currentPageId = page.id || pageId;
                currentPagePinned = page.pinned === true;
                updatePageMetadata(
                    currentPageId,
                    "opened",
                    Math.floor(new Date().getTime() / 1000)
                );
                updatePageMetadata(currentPageId, "pinned", currentPagePinned);
                setPinButton(id("page-pin"), currentPagePinned);

                restoreNightPalette(id("page-content"));
                resetMathRepair();
                resetImageLoading();
                clearReadingPositionState();

                hide(id("pages-view"));
                show(id("reader-view"));

                id("page-title").innerHTML = "";
                id("page-title").appendChild(
                    document.createTextNode(
                        "\u2637 " + page.title
                    )
                );

                id("page-content").innerHTML = page.html;
                id("page-content").scrollTop = 0;

                if (page.position)
                    id("page-content").style.visibility = "hidden";

                if (page.truncated)
                    warning(
                        "This very large page was truncated by Notion."
                    );

                id("status").innerHTML = "";

                updateScroll();

                window.setTimeout(function() {
                    try {
                        collectMath();
                        prepareImages();
                        prepareToggles();
                        preparePageLinks();
                        collectReadingBlocks();
                        applyNightPageAppearance();
                        scheduleMathRepair(0);
                        restoreReadingPosition(page.position);
                    } finally {
                        setBusy(false);
                        updateScroll();
                    }
                }, 0);
            }
        );
    }

    function start() {
        request("GET", "/api/settings", null, function(error, settings) {
            if (!error && settings) {
                fontScale = settings.fontScale || 1;

                pageFont =
                    fonts[settings.cardFont] ?
                        settings.cardFont :
                        "Bookerly";

                night = settings.nightMode === true;
                nightPageMode = settings.nightPageMode;

                if (nightPageMode !== "palette" &&
                    nightPageMode !== "palette-images")
                    nightPageMode = "standard";

                pageButtonMode =
                    settings.pageButtonMode === "reversed" ?
                        "reversed" :
                        "normal";

                pageSortMode =
                    settings.pageSortMode === "edited" ?
                        "edited" :
                        "opened";

                rotationMode =
                    settings.rotationMode === "locked" ?
                        "locked" :
                        "auto";
            }

            chooseRotationMode(rotationMode, false);
            applyAppearance(false);
            setSortButtons();

            request(
                "GET",
                "/api/status",
                null,
                function(statusError, status) {
                    var pageId;

                    if (statusError) {
                        warning(statusError);
                        return;
                    }

                    pageId =
                        status.startPageId ||
                        requestedPageId();

                    if (status.authenticated && pageId)
                        openPage(pageId, "debug");
                    else if (status.authenticated)
                        loadPages();
                    else
                        connectView();

                    if (status.tokenImportMessage)
                        warning(
                            "Token file: " +
                            status.tokenImportMessage
                        );
                }
            );
        });
    }

    id("connect").onclick = function() {
        var token = id("token").value;

        if (!token) {
            warning("Enter a Notion access token.");
            return;
        }

        setBusy(true);
        id("status").innerHTML =
            "Validating Notion token...";

        request(
            "POST",
            "/api/auth/token",
            "token=" + encodeURIComponent(token),
            function(error) {
                id("token").value = "";

                if (error) {
                    setBusy(false);
                    warning(error);
                    connectView();
                } else {
                    loadPages();
                }
            }
        );
    };

    id("search-button").onclick = loadPages;

    id("sort-opened").onclick = function() {
        choosePageSort("opened");
    };

    id("sort-edited").onclick = function() {
        choosePageSort("edited");
    };

    id("search").onkeydown = function(event) {
        event = event || window.event;
        if (event.keyCode === 13)
            loadPages();
    };

    id("back").onclick = function() {
        if (pageHistory.length) {
            openPage(
                pageHistory[pageHistory.length - 1],
                "back"
            );
            return;
        }

        showPages();
    };

    id("pages-home").onclick = showPages;

    id("page-pin").onclick = function() {
        if (currentPageId)
            setPagePinned(currentPageId, !currentPagePinned, this);
    };

    id("scroll-up").onclick = function() {
        pageScroll(-1);
    };

    id("scroll-down").onclick = function() {
        pageScroll(1);
    };

    id("pages").onscroll = updateScroll;

    id("page-content").onscroll = function() {
        if (!selectionHoldActive) clearSelectionMenu(true);
        updateScroll();
        scheduleMathRepair();
        scheduleImageLoad();
        scheduleTransientReadingPositionUpdate();
        scheduleReadingPositionSave();
    };

    id("font-plus").onclick = function() {
        var i = Math.min(
            fontScales.length - 1,
            scaleIndex() + 1
        );

        fontScale = fontScales[i];
        applyAppearance(true);
    };

    id("font-minus").onclick = function() {
        var i = Math.max(
            0,
            scaleIndex() - 1
        );

        fontScale = fontScales[i];
        applyAppearance(true);
    };

    id("night").onclick = function() {
        night = !night;
        applyAppearance(true);
    };

    id("rotation").onclick = function() {
        chooseRotationMode(rotationMode === "auto" ? "locked" : "auto", true);
    };

    id("refresh").onclick = function() {
        request(
            "POST",
            "/api/refresh",
            "",
            function(error) {
                if (error) warning(error);
            }
        );
    };

    id("settings").onclick = function() {
        id("page-font").value = pageFont;
        selectedRadio("page-buttons", pageButtonMode);
        selectedRadio("night-page-mode", nightPageMode);
        hideSettingsTooltip();
        show(id("settings-dialog"));
    };

    id("settings-about").onclick = openAbout;

    id("settings-done").onclick = function() {
        hideSettingsTooltip();
        hide(id("settings-dialog"));
    };

    id("page-font").onchange = function() {
        if (fonts[this.value]) {
            pageFont = this.value;
            applyAppearance(true);
        }
    };

    (function() {
        var buttons =
                id("font-sizes").getElementsByTagName("button"),
            i;

        for (i = 0; i < buttons.length; ++i)
            buttons[i].onclick = function() {
                fontScale =
                    parseFloat(
                        this.getAttribute("data-scale")
                    );
                applyAppearance(true);
            };
    }());

    (function() {
        var choices =
                document.getElementsByName("night-page-mode"),
            i;

        for (i = 0; i < choices.length; ++i)
            choices[i].onclick = function() {
                clearSelectionMenu(true);
                nightPageMode = this.value;
                saveSetting(
                    "nightPageMode",
                    nightPageMode
                );
                applyNightPageAppearance();
            };
    }());

    (function() {
        var choices =
                document.getElementsByName("page-buttons"),
            i;

        for (i = 0; i < choices.length; ++i)
            choices[i].onclick = function() {
                pageButtonMode = this.value;
                saveSetting(
                    "pageButtonMode",
                    pageButtonMode
                );
            };
    }());

    (function() {
        var buttons =
                document.getElementsByClassName("setting-help"),
            i;

        for (i = 0; i < buttons.length; ++i)
            buttons[i].onclick = function(event) {
                event = event || window.event;

                if (event.stopPropagation)
                    event.stopPropagation();
                else
                    event.cancelBubble = true;

                showSettingsTooltip(this);
            };

        id("settings-tooltip").onclick =
            hideSettingsTooltip;

        id("settings-dialog").onscroll =
            hideSettingsTooltip;

        document.onclick =
            hideSettingsTooltip;
    }());

    id("logout").onclick = function() {
        hideSettingsTooltip();
        hide(id("settings-dialog"));
        show(id("logout-dialog"));
    };

    id("logout-cancel").onclick = function() {
        hide(id("logout-dialog"));
        show(id("settings-dialog"));
    };

    id("logout-confirm").onclick = function() {
        request(
            "POST",
            "/api/auth/logout",
            "",
            function(error) {
                hide(id("logout-dialog"));

                if (error) {
                    warning(error);
                } else {
                    clearReadingPositionState();
                    currentPageId = "";
                    currentPagePinned = false;
                    pageList = [];
                    pageListLoaded = false;
                    connectView();
                }
            }
        );
    };

    id("about").onclick = openAbout;
    id("about-done").onclick = closeAbout;

    id("close").onclick = function() {
        saveCurrentReadingPosition(function() {
            request(
                "POST",
                "/api/quit",
                "",
                function() {
                    if (window.kindle &&
                        window.kindle.appmgr &&
                        window.kindle.appmgr.back)
                        window.kindle.appmgr.back();
                    else
                        window.close();
                }
            );
        });
    };

    id("about-logo").onload = prepareAboutLogo;
    document.onmousemove = moveCustomSelection;
    document.ontouchmove = moveCustomSelection;
    document.onmouseup = function(event) {
        finishSelectionHold(event || window.event);
        scheduleSelectionInspection();
    };
    document.ontouchend = function(event) {
        finishSelectionHold(event || window.event);
        scheduleSelectionInspection();
    };
    document.onselectionchange = scheduleSelectionInspection;
    (function() {
        var menu = id("selection-menu"),
            buttons = menu.getElementsByTagName("button"),
            i;
        menu.onmousedown = menu.ontouchstart = function(event) {
            selectionMenuActive = true;
            event = event || window.event;
            if (event.stopPropagation) event.stopPropagation();
            else event.cancelBubble = true;
        };
        for (i = 0; i < buttons.length; ++i)
            buttons[i].onclick = function() {
                formatSelection(this.getAttribute("data-format"));
                return false;
            };
    }());
    id("page-content").onmousedown = startSelectionHold;
    id("page-content").ontouchstart = startSelectionHold;
    id("page-content").onclick = function(event) {
        var point, rect, detail;
        event = event || window.event;
        detail = typeof event.detail === "number" ? event.detail : 0;
        if (detail >= 3) {
            selectMultiTap(event, true);
            return false;
        }
        if (detail === 2) {
            selectMultiTap(event, false);
            return false;
        }
        if (selectionSuppressClick) {
            selectionSuppressClick = false;
            return false;
        }
        if (selectionState) {
            point = eventPoint(event || window.event);
            try { rect = selectionState.range.getBoundingClientRect(); } catch (ignored) {}
            if (!point || !rect || point.x < rect.left || point.x > rect.right ||
                point.y < rect.top || point.y > rect.bottom) {
                clearSelectionMenu(true);
                return false;
            }
        }
    };
    id("page-content").ondblclick = function(event) {
        selectMultiTap(event || window.event, false);
        return false;
    };
    window.onorientationchange = function() {
        scheduleViewportReadingRestore();
    };
    window.onresize = function() {
        updateScroll();
        scheduleViewportReadingRestore();
    };

    start();
}());
