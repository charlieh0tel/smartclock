// Injected by tools/check-pages.sh before a page's </body>: reports,
// after load, the page's overflow and each element allowed to scroll
// sideways whose content is wider than it, in a <pre id="check-pages">
// the script reads back, "ok" for none.
addEventListener("load", () => setTimeout(() => {
  const bad = [];
  const root = document.documentElement;
  if (root.scrollWidth > innerWidth + 1) {
    bad.push("page " + root.scrollWidth + ">" + innerWidth);
  }
  for (const e of document.querySelectorAll("*")) {
    const o = getComputedStyle(e).overflowX;
    if ((o === "auto" || o === "scroll") && e.scrollWidth > e.clientWidth + 1) {
      bad.push((e.id || e.className || e.tagName) + " " + e.scrollWidth + ">" + e.clientWidth);
    }
  }
  const p = document.createElement("pre");
  p.id = "check-pages";
  p.textContent = bad.length ? bad.join("; ") : "ok";
  document.body.appendChild(p);
}, 300));
