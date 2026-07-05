const canHydrate =
  typeof window !== "undefined" &&
  typeof document !== "undefined" &&
  typeof DOMParser !== "undefined";
const pageCache = new Map();
const parser = canHydrate ? new DOMParser() : null;
const docsPages = new Set(["/index.html", "/concepts.html", "/developers.html", "/reference.html"]);
const hasStaticShell =
  canHydrate && Boolean(document.querySelector("[data-static-docs]"));
const viewerApps = new WeakMap();
let viewerRuntimePromise = null;
let activeTheme = "paper";
let sectionIndexState = null;
let apiLookupState = null;
let apiLookupBubble = null;

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
  const root = document.querySelector(".section-index");
  if (!root) return null;

  const scrollRoot = root.querySelector(".register-screen");
  const entries = [...root.querySelectorAll('a[href^="#"], summary[data-section-target^="#"]')]
    .map((link) => {
      const href = link.getAttribute("href") || link.dataset.sectionTarget;
      const id = href ? decodeURIComponent(href.slice(1)) : "";
      const target = id ? document.getElementById(id) : null;
      return target ? { link, target, href } : null;
    })
    .filter(Boolean);

  if (!entries.length) return null;

  return {
    root,
    scrollRoot,
    entries,
    activeHref: "",
    ticking: false,
  };
}

function openSectionIndexAncestors(link) {
  let disclosure = link?.closest("details");
  while (disclosure) {
    disclosure.open = true;
    disclosure = disclosure.parentElement?.closest("details") || null;
  }
}

function revealSectionIndexLink(link) {
  if (!sectionIndexState || !link) return;

  const container = sectionIndexState.scrollRoot;
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

  for (const { link, href: entryHref } of sectionIndexState.entries) {
    const active = entryHref === href;
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
  let active = sectionIndexState.entries[0];

  for (const entry of sectionIndexState.entries) {
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

function syncSectionIndexFromScroll() {
  if (!sectionIndexState) return;
  sectionIndexState.ticking = false;
  setSectionIndexActive(findActiveSectionHref(), true);
}

function queueSectionIndexSync() {
  if (!sectionIndexState || sectionIndexState.ticking) return;
  sectionIndexState.ticking = true;
  window.requestAnimationFrame(syncSectionIndexFromScroll);
}

function setupSectionIndex() {
  sectionIndexState = collectSectionIndexState();
  if (!sectionIndexState) return;

  const currentHash = window.location.hash;
  const matchingEntry = sectionIndexState.entries.find(
    (entry) => entry.href === currentHash,
  );
  if (matchingEntry) {
    setSectionIndexActive(matchingEntry.href, false);
    revealSectionIndexLink(matchingEntry.link);
    return;
  }

  setSectionIndexActive(findActiveSectionHref(), true);
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

  const title = document.createElement("strong");
  title.textContent = `${entry.kind} ${entry.name}`;
  bubble.appendChild(title);

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
  currentShell.className = nextShell.className;
  currentFrame.replaceWith(nextFrame.cloneNode(true));
  applyTheme(activeTheme, false);
}

function settleNavigation(url) {
  const target = url.hash ? document.querySelector(url.hash) : null;
  if (target) {
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

  document.addEventListener("click", (event) => {
    const button = event.target.closest("[data-command-tab-value]");
    if (!button) return;

    event.preventDefault();
    activateCommandTab(button);
  });

  document.addEventListener("click", (event) => {
    const link = event.target.closest('.section-index a[href^="#"]');
    if (!link) return;
    window.requestAnimationFrame(() => {
      setupSectionIndex();
    });
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
    if (event.target.closest(".api-signature code")) {
      return;
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
