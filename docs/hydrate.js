const canHydrate =
  typeof window !== "undefined" &&
  typeof document !== "undefined" &&
  typeof DOMParser !== "undefined";
const pageCache = new Map();
const parser = canHydrate ? new DOMParser() : null;
const docsPages = new Set([
  "/index.html",
  "/concepts.html",
  "/developers.html",
  "/pocketinkjs.html",
  "/reference.html",
]);
const hasStaticShell =
  canHydrate && Boolean(document.querySelector("[data-static-docs]"));
const viewerApps = new WeakMap();
let viewerRuntimePromise = null;
let activeTheme = "paper";
let sectionIndexState = null;
let sectionIndexScrollLockHref = "";
let sectionIndexScrollLockUntil = 0;
const sectionIndexOpenTargets = new Set();
let apiLookupState = null;
let apiLookupBubble = null;
let referenceModeState = null;
let clickedApiSymbolHref = "";

function docsUrl(href) {
  if (!hasStaticShell) return null;
  const url = new URL(href, window.location.href);
  if (url.origin !== window.location.origin) return null;
  if (!url.pathname.endsWith(".html")) return null;
  const pagePath = `/${url.pathname.split("/").pop()}`;
  if (!docsPages.has(pagePath)) return null;
  return url;
}

async function loadPage(url) {
  const key = url.pathname;
  if (!pageCache.has(key)) {
    pageCache.set(
      key,
      fetch(url.href, { headers: { Accept: "text/html" } })
        .then((response) => {
          if (!response.ok) throw new Error(`HTTP ${response.status}`);
          return response.text();
        })
        .then((html) => parser.parseFromString(html, "text/html")),
    );
  }
  return pageCache.get(key);
}

function syncHead(nextDoc) {
  document.title = nextDoc.title;

  for (const selector of [
    'meta[name="description"]',
    'meta[property="og:title"]',
    'meta[property="og:description"]',
    'meta[property="og:url"]',
    'meta[name="twitter:title"]',
    'meta[name="twitter:description"]',
    'link[rel="canonical"]',
    'script[type="application/ld+json"]',
  ]) {
    const current = document.head.querySelector(selector);
    const next = nextDoc.head.querySelector(selector);
    if (current && next) current.replaceWith(next.cloneNode(true));
  }
}

function storedTheme() {
  try {
    return localStorage.getItem("docs-theme") === "ink" ? "ink" : "paper";
  } catch {
    return "paper";
  }
}

function saveTheme(theme) {
  try {
    localStorage.setItem("docs-theme", theme);
  } catch {
    // Storage can be unavailable in private or restricted contexts.
  }
}

function storedReferenceMode() {
  try {
    return localStorage.getItem("docs-reference-mode") === "all" ? "all" : "one";
  } catch {
    return "one";
  }
}

function saveReferenceMode(mode) {
  try {
    localStorage.setItem("docs-reference-mode", mode === "one" ? "one" : "all");
  } catch {
    // Storage can be unavailable in private or restricted contexts.
  }
}

function applyTheme(theme, persist = true) {
  activeTheme = theme === "ink" ? "ink" : "paper";
  const inactiveTheme = activeTheme === "ink" ? "paper" : "ink";
  const shell = document.querySelector("[data-static-docs]");

  document.documentElement.dataset.theme = activeTheme;
  document.documentElement.classList.remove("paper-theme", "ink-theme");
  document.documentElement.classList.add(`${activeTheme}-theme`);

  if (shell) {
    shell.classList.remove(`${inactiveTheme}-theme`);
    shell.classList.add(`${activeTheme}-theme`);
  }

  for (const button of document.querySelectorAll("[data-theme-option]")) {
    const selected = button.dataset.themeOption === activeTheme;
    button.classList.toggle("active", selected);
    button.setAttribute("aria-pressed", selected ? "true" : "false");
  }

  for (const card of document.querySelectorAll("[data-pocket-3d-viewer]")) {
    card.dataset.sceneBackground = activeTheme === "ink" ? "#050505" : "#fffffb";
  }

  if (persist) saveTheme(activeTheme);
}

function activateCommandTab(button) {
  const article = button.closest(".command-list article");
  const tabs = button.closest(".command-tabs");
  const value = article?.querySelector(".command-value");
  if (!article || !tabs || !value || !button.dataset.commandTabValue) return;

  for (const tab of tabs.querySelectorAll("[data-command-tab-value]")) {
    const selected = tab === button;
    tab.classList.toggle("active", selected);
    tab.setAttribute("aria-selected", selected ? "true" : "false");
  }

  value.textContent = button.dataset.commandTabValue;
}

function collectSectionIndexState() {
  const roots = [...document.querySelectorAll(".section-index")];
  if (!roots.length) return null;

  const entries = roots
    .flatMap((root) => {
      const scrollRoot = root.querySelector(".register-screen") || root;
      return [...root.querySelectorAll('a[href^="#"], summary[data-section-target^="#"]')].map((link) => {
        const href = link.getAttribute("href") || link.dataset.sectionTarget;
        const id = href ? decodeURIComponent(href.slice(1)) : "";
        const target = id ? document.getElementById(id) : null;
        const targetGroup = target?.closest?.("[data-api-group]");
        const targetSection = target?.closest?.(".api-doc-section");
        return target
          ? {
              link,
              target,
              href,
              parentHref: link.dataset.indexParent || (targetGroup ? `#${targetGroup.id}` : ""),
              sectionHref: targetSection ? `#${targetSection.id}` : "",
              scrollRoot,
            }
          : null;
      });
    })
    .filter(Boolean);

  if (!entries.length) return null;
  entries.sort((a, b) => {
    if (a.target === b.target) return 0;
    return a.target.compareDocumentPosition(b.target) & Node.DOCUMENT_POSITION_FOLLOWING ? -1 : 1;
  });

  return {
    roots,
    entries,
    activeHref: "",
    ticking: false,
  };
}

function isReferenceSectionEntry(entry) {
  return (
    entry?.target?.classList?.contains("api-doc-section") ||
    entry?.link?.matches?.("summary[data-section-target]")
  );
}

function openSectionIndexAncestors(link) {
  let disclosure = link?.closest("details");
  while (disclosure) {
    disclosure.open = true;
    const target = disclosure.querySelector(":scope > summary[data-section-target]")?.dataset.sectionTarget;
    if (target) sectionIndexOpenTargets.add(target);
    disclosure = disclosure.parentElement?.closest("details") || null;
  }
}

function restoreSectionIndexDisclosureState() {
  for (const disclosure of document.querySelectorAll(".section-index-disclosure")) {
    const target = disclosure.querySelector(":scope > summary[data-section-target]")?.dataset.sectionTarget;
    if (target && sectionIndexOpenTargets.has(target)) {
      disclosure.open = true;
    }
  }
}

function revealSectionIndexLink(link) {
  if (!sectionIndexState || !link) return;

  const entry = sectionIndexState.entries.find((item) => item.link === link);
  const container = entry?.scrollRoot;
  if (!container) return;

  const containerRect = container.getBoundingClientRect();
  const linkRect = link.getBoundingClientRect();
  const topDelta = linkRect.top - containerRect.top;
  const bottomDelta = linkRect.bottom - containerRect.bottom;
  const padding = 8;

  if (topDelta < padding) {
    container.scrollTop += topDelta - padding;
    return;
  }

  if (bottomDelta > -padding) {
    container.scrollTop += bottomDelta + padding;
  }
}

function setSectionIndexActive(href, syncHash = false) {
  if (!sectionIndexState) return;
  if (sectionIndexState.activeHref === href && !syncHash) return;

  sectionIndexState.activeHref = href || "";
  if (clickedApiSymbolHref && clickedApiSymbolHref !== href) {
    setClickedApiSymbol("");
  }
  const activeTarget = href ? document.getElementById(decodeURIComponent(href.slice(1))) : null;
  const activeGroup = activeTarget?.closest?.("[data-api-group]");
  const activeChildGroup = activeTarget?.querySelector?.("[data-api-group]");
  const activeGroupHref = activeGroup
    ? `#${activeGroup.id}`
    : activeChildGroup
      ? `#${activeChildGroup.id}`
      : "";

  if (activeGroupHref && referenceModeState) {
    setReferenceSymbolIndexActive(activeGroupHref);
  }

  for (const { link, href: entryHref } of sectionIndexState.entries) {
    const active =
      entryHref === href ||
      (activeGroupHref && entryHref === activeGroupHref);
    link.classList.toggle("active", active);
    if (active) {
      openSectionIndexAncestors(link);
      link.setAttribute("aria-current", "location");
      revealSectionIndexLink(link);
    } else {
      link.removeAttribute("aria-current");
    }
  }

  if (!syncHash || !href) return;

  const url = new URL(window.location.href);
  if (url.hash === href) return;
  url.hash = href;
  window.history.replaceState(null, "", url.href);
}

function findActiveSectionHref() {
  if (!sectionIndexState || !sectionIndexState.entries.length) return "";

  const threshold = Math.min(window.innerHeight * 0.24, 160);
  const visibleEntries = sectionIndexState.entries.filter(
    (entry) =>
      !entry.target.closest("[hidden]") &&
      (referenceModeState?.mode !== "one" || !isReferenceSectionEntry(entry)),
  );
  let active = visibleEntries[0];

  for (const entry of visibleEntries) {
    const rect = entry.target.getBoundingClientRect();
    if (rect.top <= threshold) {
      active = entry;
      continue;
    }
    if (rect.bottom > threshold) {
      break;
    }
  }

  return active?.href || "";
}

function setClickedApiSymbol(href) {
  const nextHref = href || "";
  if (clickedApiSymbolHref === nextHref) return;

  if (clickedApiSymbolHref) {
    targetForHash(clickedApiSymbolHref)?.classList.remove("symbol-click-highlight");
  }

  clickedApiSymbolHref = nextHref;
  targetForHash(clickedApiSymbolHref)?.classList.add("symbol-click-highlight");
}

function syncSectionIndexFromScroll() {
  if (!sectionIndexState) return;
  sectionIndexState.ticking = false;
  if (Date.now() < sectionIndexScrollLockUntil) {
    if (sectionIndexScrollLockHref) {
      setSectionIndexActive(sectionIndexScrollLockHref, false);
    }
    return;
  }

  sectionIndexScrollLockHref = "";
  setSectionIndexActive(findActiveSectionHref(), true);
}

function queueSectionIndexSync() {
  if (!sectionIndexState || sectionIndexState.ticking) return;
  sectionIndexState.ticking = true;
  window.requestAnimationFrame(syncSectionIndexFromScroll);
}

function lockSectionIndexScrollSync(href) {
  sectionIndexScrollLockHref = href || "";
  sectionIndexScrollLockUntil = Date.now() + 4000;
  window.setTimeout(() => {
    if (Date.now() >= sectionIndexScrollLockUntil) {
      sectionIndexScrollLockHref = "";
      queueSectionIndexSync();
    }
  }, 4050);
}

function targetForHash(href) {
  if (!href?.startsWith("#")) return null;
  return document.getElementById(decodeURIComponent(href.slice(1)));
}

function jumpToHashTarget(href, replaceHistory = false) {
  const target = targetForHash(href);
  if (!target) return false;

  const previousScrollBehavior = document.documentElement.style.scrollBehavior;
  document.documentElement.style.scrollBehavior = "auto";
  target.scrollIntoView({ block: "start" });
  document.documentElement.style.scrollBehavior = previousScrollBehavior;

  const url = new URL(window.location.href);
  url.hash = target.id;
  if (replaceHistory) {
    window.history.replaceState(null, "", url.href);
  } else {
    window.history.pushState(null, "", url.href);
  }
  setSectionIndexActive(`#${target.id}`, false);
  return true;
}

function setupSectionIndex() {
  sectionIndexState = collectSectionIndexState();
  if (!sectionIndexState) return;
  restoreSectionIndexDisclosureState();

  const currentHash = window.location.hash;
  const matchingEntry = sectionIndexState.entries.find(
    (entry) => entry.href === currentHash,
  );
  if (matchingEntry) {
    setSectionIndexActive(matchingEntry.href, false);
    revealSectionIndexLink(matchingEntry.link);
    return;
  }

  if (currentHash) {
    setSectionIndexActive(findActiveSectionHref(), true);
  }
}

function groupHrefForTarget(target) {
  const group = target?.closest?.("[data-api-group]");
  if (group?.id) {
    return `#${group.id}`;
  }

  const firstGroup = target?.querySelector?.("[data-api-group]");
  return firstGroup?.id ? `#${firstGroup.id}` : "";
}

function resolveReferenceTargetHref(href) {
  if (!href) return "";
  const target = document.getElementById(decodeURIComponent(href.slice(1)));
  if (!target) return "";
  return groupHrefForTarget(target) || href;
}

function collectReferenceModeState() {
  const switcher = document.querySelector("[data-reference-mode-switch]");
  const groups = [...document.querySelectorAll("[data-api-group]")]
    .filter((group) => group.id)
    .map((group) => ({
      href: `#${group.id}`,
      element: group,
      section: group.closest(".api-doc-section"),
      label:
        group.querySelector(".api-group-header strong")?.textContent?.trim() ||
        group.querySelector(".api-group-header code")?.textContent?.trim() ||
        group.id,
    }));

  if (!switcher || !groups.length) {
    return null;
  }

  const currentHash = window.location.hash;
  const selectedHref =
    resolveReferenceTargetHref(currentHash) ||
    resolveReferenceTargetHref(findActiveSectionHref()) ||
    groups[0].href;

  return {
    switcher,
    pager: document.querySelector("[data-reference-file-pager]"),
    groups,
    mode: "all",
    activeGroupHref: selectedHref,
  };
}

function renderReferencePager() {
  if (!referenceModeState?.pager) return;

  const { groups, activeGroupHref, pager, mode } = referenceModeState;
  const index = groups.findIndex((group) => group.href === activeGroupHref);
  if (mode !== "one" || index < 0) {
    pager.hidden = true;
    pager.replaceChildren();
    return;
  }

  const items = [
    { label: "Previous", group: groups[index - 1], direction: "prev" },
    { label: "Next", group: groups[index + 1], direction: "next" },
  ];

  pager.replaceChildren();
  for (const item of items) {
    const node = document.createElement(item.group ? "a" : "span");
    node.className = `reference-file-card ${item.direction}${item.group ? "" : " disabled"}`;
    if (item.group) {
      node.href = item.group.href;
      node.dataset.referenceFileLink = "true";
    }

    const label = document.createElement("span");
    label.textContent = item.label;
    const name = document.createElement("strong");
    name.textContent = item.group?.label || (item.direction === "prev" ? "Start" : "End");
    node.append(label, name);
    pager.appendChild(node);
  }

  pager.hidden = false;
}

function setReferenceSymbolIndexActive(groupHref) {
  if (!referenceModeState || !groupHref) return;

  referenceModeState.activeGroupHref = groupHref;
  for (const symbolGroup of document.querySelectorAll("[data-symbol-group]")) {
    symbolGroup.hidden = symbolGroup.dataset.symbolGroup !== groupHref;
  }

  renderReferencePager();
}

function syncReferenceModeSwitch(nextDoc) {
  const currentSwitch = document.querySelector("[data-reference-mode-switch]");
  const nextSwitch = nextDoc.querySelector("[data-reference-mode-switch]");

  if (currentSwitch && nextSwitch) {
    currentSwitch.replaceWith(nextSwitch.cloneNode(true));
    return;
  }

  if (currentSwitch) {
    currentSwitch.remove();
    return;
  }

  if (!nextSwitch) return;

  const themeSwitch = document.querySelector(".theme-switch");
  const shell = document.querySelector("[data-static-docs]");
  if (themeSwitch) {
    themeSwitch.insertAdjacentElement("afterend", nextSwitch.cloneNode(true));
    return;
  }

  shell?.prepend(nextSwitch.cloneNode(true));
}

function applyReferenceMode(mode, targetHref = "", persist = true) {
  if (!referenceModeState) return;

  const nextMode = mode === "one" ? "one" : "all";
  const nextGroupHref =
    resolveReferenceTargetHref(targetHref) ||
    referenceModeState.activeGroupHref ||
    referenceModeState.groups[0].href;

  referenceModeState.mode = nextMode;
  referenceModeState.activeGroupHref = nextGroupHref;
  if (persist) saveReferenceMode(nextMode);

  const shell = document.querySelector("[data-static-docs]");
  if (shell) {
    shell.dataset.referenceMode = nextMode;
  }

  for (const button of referenceModeState.switcher.querySelectorAll("[data-reference-mode-option]")) {
    const selected = button.dataset.referenceModeOption === nextMode;
    button.classList.toggle("active", selected);
    button.setAttribute("aria-pressed", selected ? "true" : "false");
  }

  for (const group of referenceModeState.groups) {
    const visible = nextMode === "all" || group.href === nextGroupHref;
    group.element.hidden = !visible;
  }

  const visibleSections = new Set(
    referenceModeState.groups
      .filter((group) => nextMode === "all" || group.href === nextGroupHref)
      .map((group) => group.section)
      .filter(Boolean),
  );
  for (const section of document.querySelectorAll(".api-doc-section")) {
    section.hidden = nextMode === "one" && !visibleSections.has(section);
  }

  setReferenceSymbolIndexActive(nextGroupHref);
}

function setupReferenceMode() {
  referenceModeState = collectReferenceModeState();
  if (!referenceModeState) return;
  applyReferenceMode(storedReferenceMode(), window.location.hash, false);
}

function ensureApiLookupBubble() {
  if (apiLookupBubble) {
    return apiLookupBubble;
  }

  const bubble = document.createElement("div");
  bubble.className = "api-lookup-bubble";
  bubble.hidden = true;
  document.body.appendChild(bubble);
  apiLookupBubble = bubble;
  return bubble;
}

function hideApiLookup() {
  if (!apiLookupBubble) {
    return;
  }

  apiLookupBubble.hidden = true;
  apiLookupBubble.style.visibility = "";
  apiLookupBubble.replaceChildren();
}

function setupApiLookup() {
  apiLookupState = null;
  hideApiLookup();

  const source = document.querySelector("[data-api-symbol-index]");
  if (!source) {
    return;
  }

  try {
    const entries = JSON.parse(source.textContent || "[]").filter(
      (entry) => entry && entry.name,
    );
    if (!entries.length) {
      return;
    }

    const byName = new Map();
    for (const entry of entries) {
      if (!byName.has(entry.name)) {
        byName.set(entry.name, []);
      }
      byName.get(entry.name).push(entry);
    }

    apiLookupState = { byName };
  } catch (error) {
    console.warn("API symbol index parsing failed", error);
  }
}

function resolveApiLookupEntry(name, ownerName = "") {
  if (!apiLookupState) {
    return null;
  }

  const matches = apiLookupState.byName.get(name) || [];
  if (!matches.length) {
    return null;
  }

  if (ownerName) {
    const ownedMatch = matches.find((entry) => entry.owner === ownerName);
    if (ownedMatch) {
      return ownedMatch;
    }

    if (name === ownerName) {
      const topLevelMatch = matches.find((entry) => !entry.owner);
      if (topLevelMatch) {
        return topLevelMatch;
      }
    }
  }

  return matches.find((entry) => !entry.owner) || matches[0];
}

function selectedApiIdentifier(root) {
  const selection = window.getSelection();
  if (!selection || selection.isCollapsed || !selection.rangeCount) {
    return null;
  }

  const text = selection.toString().trim();
  if (!/^[A-Za-z_]\w*$/.test(text)) {
    return null;
  }

  const range = selection.getRangeAt(0);
  const node = range.commonAncestorContainer;
  const container = node?.nodeType === 3 ? node.parentNode : node;
  if (!container || !root.contains(container)) {
    return null;
  }

  return {
    name: text,
    rect: range.getBoundingClientRect(),
  };
}

function showApiLookup(entry, anchorRect) {
  const bubble = ensureApiLookupBubble();
  bubble.replaceChildren();

  const header = document.createElement("div");
  header.className = "api-lookup-header";

  const title = document.createElement("strong");
  title.textContent = `${entry.kind} ${entry.name}`;
  header.appendChild(title);

  const definitionHref = entry.anchor || entry.href || "";
  const controls = document.createElement("div");
  controls.className = "api-lookup-controls";
  if (definitionHref) {
    const definitionLink = document.createElement("a");
    definitionLink.className = "api-lookup-definition-link";
    definitionLink.href = definitionHref;
    definitionLink.dataset.apiLookupDefinitionLink = "true";
    definitionLink.setAttribute("aria-label", `Go to ${entry.name} definition`);
    definitionLink.textContent = "i";
    controls.appendChild(definitionLink);
  }

  const closeButton = document.createElement("button");
  closeButton.className = "api-lookup-close";
  closeButton.type = "button";
  closeButton.dataset.apiLookupClose = "true";
  closeButton.setAttribute("aria-label", "Close definition popup");
  closeButton.textContent = "x";
  controls.appendChild(closeButton);
  header.appendChild(controls);

  bubble.appendChild(header);

  if (entry.owner) {
    const owner = document.createElement("p");
    owner.className = "api-lookup-context";
    owner.textContent = `within ${entry.owner}`;
    bubble.appendChild(owner);
  }

  const sourcePath = document.createElement("code");
  sourcePath.className = "api-lookup-path";
  sourcePath.textContent = entry.path;
  bubble.appendChild(sourcePath);

  if (entry.docs) {
    const docs = document.createElement("p");
    docs.className = "api-lookup-doc";
    docs.textContent = entry.docs;
    bubble.appendChild(docs);
  }

  const signature = document.createElement("pre");
  signature.className = "api-signature api-lookup-signature";
  const code = document.createElement("code");
  code.textContent = entry.definition;
  signature.appendChild(code);
  bubble.appendChild(signature);

  bubble.hidden = false;
  bubble.style.visibility = "hidden";
  bubble.style.left = "12px";
  bubble.style.top = "12px";

  const rect = anchorRect?.width || anchorRect?.height
    ? anchorRect
    : { top: 16, bottom: 16, left: 16 };
  const bubbleRect = bubble.getBoundingClientRect();
  const margin = 12;
  const left = Math.max(
    margin,
    Math.min(rect.left, window.innerWidth - bubbleRect.width - margin),
  );
  let top = rect.bottom + margin;

  if (top + bubbleRect.height > window.innerHeight - margin) {
    top = Math.max(margin, rect.top - bubbleRect.height - margin);
  }

  bubble.style.left = `${Math.round(left)}px`;
  bubble.style.top = `${Math.round(top)}px`;
  bubble.style.visibility = "";
}

async function loadViewerRuntime() {
  if (!viewerRuntimePromise) {
    viewerRuntimePromise = Promise.all([
      import("vue"),
      import("./3d-viewer.js"),
    ]).then(([vue, viewer]) => ({
      createApp: vue.createApp,
      Pocket3DViewer: viewer.Pocket3DViewer,
    }));
  }
  return viewerRuntimePromise;
}

function unmountViewers(root = document) {
  for (const card of root.querySelectorAll("[data-pocket-3d-viewer]")) {
    const app = viewerApps.get(card);
    if (app) {
      app.unmount();
      viewerApps.delete(card);
    }
  }
}

async function hydrateViewers(root = document) {
  const cards = [...root.querySelectorAll("[data-pocket-3d-viewer]")].filter(
    (card) => !viewerApps.has(card),
  );
  if (!cards.length) return;

  const { createApp, Pocket3DViewer } = await loadViewerRuntime();

  for (const card of cards) {
    let mount = card.querySelector(".viewer-mount");
    if (!mount) {
      mount = document.createElement("div");
      mount.className = "viewer-mount";
      card.replaceChildren(mount);
    }

    const app = createApp(Pocket3DViewer, {
      modelUrl: card.dataset.modelUrl || "./WSEPAPER.glb",
      screenText: card.dataset.screenText || "Pocket Ink",
      sceneBackground: activeTheme === "ink" ? "#050505" : "#fffffb",
    });
    app.mount(mount);
    viewerApps.set(card, app);
  }
}

function replacePage(nextDoc) {
  const currentFrame = document.querySelector(".page-frame");
  const nextFrame = nextDoc.querySelector(".page-frame");
  const currentShell = document.querySelector(".site-shell");
  const nextShell = nextDoc.querySelector(".site-shell");

  if (!currentFrame || !nextFrame || !currentShell || !nextShell) {
    throw new Error("Static document shell is missing.");
  }

  syncHead(nextDoc);
  unmountViewers(currentFrame);
  hideApiLookup();
  syncReferenceModeSwitch(nextDoc);
  currentShell.className = nextShell.className;
  currentShell.removeAttribute("data-reference-mode");
  currentFrame.replaceWith(nextFrame.cloneNode(true));
  applyTheme(activeTheme, false);
}

function settleNavigation(url) {
  const target = url.hash ? document.querySelector(url.hash) : null;
  setupReferenceMode();
  if (target) {
    applyReferenceMode(referenceModeState?.mode || "all", url.hash);
    target.scrollIntoView({ block: "start" });
    target.focus?.({ preventScroll: true });
    setupSectionIndex();
    setupApiLookup();
    return;
  }

  window.scrollTo({ top: 0, left: 0 });
  document.querySelector("#main")?.focus({ preventScroll: true });
  setupSectionIndex();
  setupApiLookup();
}

async function navigate(url, replaceHistory = false) {
  const nextDoc = await loadPage(url);
  const apply = () => replacePage(nextDoc);

  if (document.startViewTransition) {
    await document.startViewTransition(apply).finished;
  } else {
    apply();
  }

  if (replaceHistory) {
    window.history.replaceState(null, "", url.href);
  } else {
    window.history.pushState(null, "", url.href);
  }

  await hydrateViewers();
  settleNavigation(url);
}

if (hasStaticShell) {
  applyTheme(storedTheme(), false);
  hydrateViewers().catch((error) => {
    console.warn("3D viewer hydration failed", error);
  });
  setupReferenceMode();
  setupSectionIndex();
  setupApiLookup();

  document.addEventListener("click", (event) => {
    const button = event.target.closest("[data-theme-option]");
    if (!button) return;

    event.preventDefault();
    applyTheme(button.dataset.themeOption);
    unmountViewers();
    hydrateViewers().catch((error) => {
      console.warn("3D viewer hydration failed", error);
    });
  });

  document.addEventListener("toggle", (event) => {
    const disclosure = event.target.closest?.(".section-index-disclosure");
    if (!disclosure || event.target !== disclosure) return;

    const target = disclosure.querySelector(":scope > summary[data-section-target]")?.dataset.sectionTarget;
    if (!target) return;

    if (disclosure.open) {
      sectionIndexOpenTargets.add(target);
      return;
    }

    sectionIndexOpenTargets.delete(target);
  }, true);

  document.addEventListener("click", (event) => {
    const button = event.target.closest("[data-command-tab-value]");
    if (!button) return;

    event.preventDefault();
    activateCommandTab(button);
  });

  document.addEventListener("click", (event) => {
    const button = event.target.closest("[data-reference-mode-option]");
    if (!button || !referenceModeState) return;

    event.preventDefault();
    const activeHref = window.location.hash || findActiveSectionHref();
    applyReferenceMode(button.dataset.referenceModeOption, activeHref);
    setupSectionIndex();

    if (referenceModeState.mode === "one") {
      document
        .querySelector(referenceModeState.activeGroupHref)
        ?.scrollIntoView({ block: "start" });
    }
  });

  document.addEventListener("click", (event) => {
    const link = event.target.closest('.section-index a[href^="#"]');
    if (!link) return;

    const href = link.getAttribute("href");
    if (link.closest(".symbol-index")) {
      setClickedApiSymbol(href);
    }

    if (href) {
      lockSectionIndexScrollSync(href);
      setSectionIndexActive(href, false);
    }

    if (referenceModeState?.mode === "one") {
      const targetHref = resolveReferenceTargetHref(href);
      if (targetHref) {
        event.preventDefault();
        applyReferenceMode("one", href);
        const scrollHref = href === targetHref ? targetHref : href;
        jumpToHashTarget(targetForHash(scrollHref) ? scrollHref : targetHref);
        return;
      }
    }

    if (href && jumpToHashTarget(href)) {
      event.preventDefault();
    }
  });

  document.addEventListener("click", (event) => {
    const link = event.target.closest("[data-reference-file-link]");
    if (!link || !referenceModeState) return;

    event.preventDefault();
    const href = link.getAttribute("href");
    const targetHref = resolveReferenceTargetHref(href);
    lockSectionIndexScrollSync(targetHref || href);
    applyReferenceMode("one", href);
    setupSectionIndex();
    jumpToHashTarget(targetForHash(href) ? href : targetHref);
  });

  document.addEventListener("click", (event) => {
    if (event.defaultPrevented || event.button !== 0) return;
    if (event.metaKey || event.ctrlKey || event.shiftKey || event.altKey) return;

    const link = event.target.closest("a[href]");
    if (!link || link.target || link.hasAttribute("download")) return;

    const url = docsUrl(link.href);
    if (!url) return;

    const current = new URL(window.location.href);
    if (url.pathname === current.pathname && url.hash) return;

    event.preventDefault();
    navigate(url).catch(() => {
      window.location.href = url.href;
    });
  });

  document.addEventListener("mouseover", (event) => {
    const link = event.target.closest("a[href]");
    const url = link ? docsUrl(link.href) : null;
    if (url && url.pathname !== window.location.pathname) {
      loadPage(url).catch(() => {});
    }
  });

  document.addEventListener("dblclick", (event) => {
    const code = event.target.closest(".api-signature code");
    if (!code) return;

    window.requestAnimationFrame(() => {
      const selected = selectedApiIdentifier(code);
      if (!selected) {
        hideApiLookup();
        return;
      }

      const ownerName = code.closest(".api-symbol")?.dataset.apiSymbolName || "";
      const entry = resolveApiLookupEntry(selected.name, ownerName);
      if (!entry) {
        hideApiLookup();
        return;
      }

      showApiLookup(entry, selected.rect);
    });
  });

  document.addEventListener("click", (event) => {
    const closeButton = event.target.closest("[data-api-lookup-close]");
    if (!closeButton) return;

    event.preventDefault();
    hideApiLookup();
  });

  document.addEventListener("click", (event) => {
    const link = event.target.closest("[data-api-lookup-definition-link]");
    if (!link || !apiLookupState) return;

    const href = link.getAttribute("href");
    if (!href) return;

    event.preventDefault();
    hideApiLookup();
    if (referenceModeState?.mode === "one") {
      applyReferenceMode("one", href);
      setupSectionIndex();
    }
    jumpToHashTarget(href);
  });

  document.addEventListener("click", (event) => {
    if (event.target.closest(".api-signature code")) {
      return;
    }
    if (event.target.closest(".api-lookup-bubble")) {
      return;
    }
    if (!event.target.closest(".symbol-index")) {
      setClickedApiSymbol("");
    }
    hideApiLookup();
  });

  window.addEventListener("popstate", () => {
    navigate(new URL(window.location.href), true).catch(() => {
      window.location.reload();
    });
  });

  window.addEventListener("scroll", () => {
    hideApiLookup();
    queueSectionIndexSync();
  }, { passive: true });
  window.addEventListener("resize", () => {
    hideApiLookup();
    queueSectionIndexSync();
  }, { passive: true });
  window.addEventListener("keydown", (event) => {
    if (event.key === "Escape") {
      hideApiLookup();
    }
  });
  window.addEventListener("hashchange", () => {
    if (!sectionIndexState) return;
    hideApiLookup();
    if (referenceModeState) {
      applyReferenceMode(referenceModeState.mode, window.location.hash);
    }
    const currentHash = window.location.hash;
    const matchingEntry = sectionIndexState.entries.find(
      (entry) => entry.href === currentHash,
    );
    if (matchingEntry) {
      setSectionIndexActive(matchingEntry.href, false);
      revealSectionIndexLink(matchingEntry.link);
      return;
    }
    queueSectionIndexSync();
  });
}
