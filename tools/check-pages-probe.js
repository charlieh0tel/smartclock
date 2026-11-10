// Injected by tools/check-pages.sh before a page's </body>: reports,
// after load, the page's overflow and each element allowed to scroll
// sideways whose content is wider than it, in a <pre id="check-pages">
// the script reads back, "ok" for none.  In a window as narrow as a
// phone's, a box marked data-scroll="x" that scrolls sideways may, and
// anything reaching past the window's edges must be inside one.
const PHONE_WIDTH = 500;
const SLACK = 1;
addEventListener("load", () => setTimeout(() => {
  const bad = [];
  const phone = innerWidth < PHONE_WIDTH;
  const name = e => e.id || (typeof e.className === "string" && e.className) || e.tagName;
  const scrolls = e => ["auto", "scroll"].includes(getComputedStyle(e).overflowX);
  const marked = e => e.matches('[data-scroll="x"]') && scrolls(e);
  const insideMarked = e => {
    for (let a = e.parentElement; a; a = a.parentElement) {
      if (marked(a)) return true;
    }
    return false;
  };
  const root = document.documentElement;
  if (root.scrollWidth > innerWidth + SLACK) {
    bad.push("page " + root.scrollWidth + ">" + innerWidth);
  }
  for (const e of document.querySelectorAll("*")) {
    if (scrolls(e) && e.scrollWidth > e.clientWidth + SLACK && !(phone && marked(e))) {
      bad.push(name(e) + " " + e.scrollWidth + ">" + e.clientWidth);
    }
    if (phone) {
      const r = e.getBoundingClientRect();
      if (r.width && (r.left < -SLACK || r.right > innerWidth + SLACK) && !insideMarked(e)) {
        bad.push(name(e) + " at " + Math.round(r.left) + ".." + Math.round(r.right));
      }
    }
  }
  const p = document.createElement("pre");
  p.id = "check-pages";
  p.textContent = bad.length ? [...new Set(bad)].join("; ") : "ok";
  document.body.appendChild(p);
}, 300));
