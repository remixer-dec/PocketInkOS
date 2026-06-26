const canHydrate =
  typeof window !== "undefined" &&
  typeof document !== "undefined" &&
  typeof DOMParser !== "undefined";
const pageCache = new Map();
const parser = canHydrate ? new DOMParser() : null;
const docsPages = new Set(["/index.html", "/concepts.html", "/developers.html"]);
const hasStaticShell =
  canHydrate && Boolean(document.querySelector("[data-static-docs]"));
const viewerApps = new WeakMap();
let viewerRuntimePromise = null;
let activeTheme = "paper";

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
  currentShell.className = nextShell.className;
  currentFrame.replaceWith(nextFrame.cloneNode(true));
  applyTheme(activeTheme, false);
}

function settleNavigation(url) {
  const target = url.hash ? document.querySelector(url.hash) : null;
  if (target) {
    target.scrollIntoView({ block: "start" });
    target.focus?.({ preventScroll: true });
    return;
  }

  window.scrollTo({ top: 0, left: 0 });
  document.querySelector("#main")?.focus({ preventScroll: true });
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

  window.addEventListener("popstate", () => {
    navigate(new URL(window.location.href), true).catch(() => {
      window.location.reload();
    });
  });
}
