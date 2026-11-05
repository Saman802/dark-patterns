// content.js - DOM capture + highlighting. All detection logic lives in the C++/Wasm engine.
(() => {
  if (window.__darkwebLoaded) return;
  window.__darkwebLoaded = true;
  const api = typeof browser !== 'undefined' ? browser : chrome;

  // flag bits must match enum Flag in cpp/rules.hpp
  const F = { HIDDEN: 1, CHECKED: 2, CHECKBOX: 4, BUTTON: 8, TINY: 16, LOW: 32, STRIKE: 64, RADIO: 128 };
  const MAX_NODES = 6000;
  const SKIP = new Set(['SCRIPT', 'STYLE', 'NOSCRIPT', 'TEMPLATE', 'SVG', 'HEAD', 'META', 'LINK', 'IFRAME', 'CANVAS', 'PATH', 'VIDEO', 'AUDIO']);
  const SHOP_HOSTS = /(amazon|flipkart|myntra|ajio|meesho|nykaa|snapdeal|ebay|walmart|etsy|aliexpress|temu|shein|bigbasket|zepto|swiggy|zomato|makemytrip|booking|bookmyshow|myshopify|shopify|croma|tatacliq|target|bestbuy)\./i;

  let els = [];          // index == node id sent to Wasm
  let lastResult = null;
  let scanning = false, autoScans = 0, timer = null;

  // ---------- helpers ----------
  const clean = (s) => s.replace(/[\u2018\u2019]/g, "'").replace(/[\u201c\u201d]/g, '"').replace(/[\t\r\n\u00a0]+/g, ' ').replace(/\s+/g, ' ').trim().slice(0, 300);
  function ownText(el) {
    let t = '';
    for (const n of el.childNodes) if (n.nodeType === 3) t += n.nodeValue + ' ';
    if (el.tagName === 'INPUT' && ['submit', 'button', 'reset'].includes((el.type || '').toLowerCase())) t += ' ' + (el.value || '');
    return clean(t);
  }
  function parseRGBA(s) {
    const m = s && s.match(/rgba?\(([^)]+)\)/);
    if (!m) return null;
    const p = m[1].split(/[ ,\/]+/).filter(Boolean).map(parseFloat);
    return { r: p[0], g: p[1], b: p[2], a: p.length > 3 ? p[3] : 1 };
  }
  const lin = (v) => { v /= 255; return v <= 0.03928 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4); };
  const lum = (c) => 0.2126 * lin(c.r) + 0.7152 * lin(c.g) + 0.0722 * lin(c.b);
  function contrast(fg, bg) { const a = lum(fg), b = lum(bg); return (Math.max(a, b) + 0.05) / (Math.min(a, b) + 0.05); }  // WCAG ratio
  const notRendered = (cs) => cs.display === 'none' || cs.visibility === 'hidden' || parseFloat(cs.opacity) === 0;
  function boxHidden(el) {
    const r = el.getBoundingClientRect();
    return r.width <= 2 || r.height <= 2 || r.right < 0 || r.bottom < 0 || r.left > innerWidth * 3;
  }
  function hasVisibleLabel(el) {
    const lab = el.closest('label') || (el.id && document.querySelector('label[for="' + CSS.escape(el.id) + '"]'));
    if (!lab) return false;
    const r = lab.getBoundingClientRect();
    return r.width > 4 && r.height > 4;
  }

  // ---------- 1. serialise the DOM (iterative pre-order, parents always before children) ----------
  function collect() {
    els = [];
    const lines = [];
    const body = document.body;
    if (!body) return null;
    const stack = [[body, -1, false, { r: 255, g: 255, b: 255, a: 1 }]];
    while (stack.length && els.length < MAX_NODES) {
      const [el, parentIdx, hiddenAnc, parentBg] = stack.pop();
      const tag = el.tagName.toUpperCase();
      if (SKIP.has(tag) || el.hasAttribute('data-darkweb-ui')) continue;
      const cs = getComputedStyle(el);
      const isCheck = tag === 'INPUT' && ['checkbox', 'radio'].includes((el.type || '').toLowerCase());
      let hidden = hiddenAnc || notRendered(cs);
      if (hidden && !hiddenAnc && !isCheck && !el.querySelector('input[type=checkbox]:checked')) continue;  // prune invisible subtrees unless they hide a ticked box

      let flags = 0;
      const type = tag === 'INPUT' ? (el.type || '').toLowerCase() : '';
      if (type === 'checkbox') { flags |= F.CHECKBOX; if (el.checked || el.hasAttribute('checked')) flags |= F.CHECKED; }
      if (type === 'radio') { flags |= F.RADIO; if (el.checked) flags |= F.CHECKED; }
      if (tag === 'BUTTON' || el.getAttribute('role') === 'button' || (tag === 'INPUT' && ['submit', 'button'].includes(type)) ||
          (tag === 'A' && /\b(btn|button)\b/i.test(typeof el.className === 'string' ? el.className : ''))) flags |= F.BUTTON;
      if (isCheck) {
        if (hidden || (boxHidden(el) && !hasVisibleLabel(el))) flags |= F.HIDDEN;   // truly invisible tick-box
      } else if (hidden) flags |= F.HIDDEN;

      const text = ownText(el);
      let bg = parseRGBA(cs.backgroundColor);
      bg = bg && bg.a > 0.5 ? bg : parentBg;
      if (text) {
        if (parseFloat(cs.fontSize) <= 11) flags |= F.TINY;
        const fg = parseRGBA(cs.color);
        if (fg && contrast(fg, bg) < 2.5) flags |= F.LOW;                        // WCAG contrast < 2.5 : deliberately faded
        if ((cs.textDecorationLine || '').includes('line-through')) flags |= F.STRIKE;
      }
      const idx = els.length;
      els.push(el);
      lines.push(parentIdx + '\t' + tag + '\t' + flags + '\t' + text);
      for (let c = el.lastElementChild; c; c = c.previousElementSibling) stack.push([c, idx, hidden, bg]);
    }
    return lines.join('\n');
  }

  // ---------- 2. highlighting ----------
  function clearHighlights() {
    document.querySelectorAll('.darkweb-flagged-pattern').forEach((e) => {
      e.classList.remove('darkweb-flagged-pattern');
      e.removeAttribute('data-darkweb-tip');
    });
  }
  function visibleTarget(el) {
    let e = el;
    while (e && e !== document.body) {
      const r = e.getBoundingClientRect();
      if (r.width > 4 && r.height > 4 && !(e.tagName === 'INPUT')) return e;
      e = e.parentElement;
    }
    return el;
  }
  function highlight(findings) {
    clearHighlights();
    for (const f of findings) {
      const el = els[f.node];
      if (!el || !el.isConnected) continue;
      const t = visibleTarget(el);
      t.classList.add('darkweb-flagged-pattern');
      const prev = t.getAttribute('data-darkweb-tip');
      const entry = f.category + '|' + f.score + '|' + f.reason + '|#' + f.rank;
      t.setAttribute('data-darkweb-tip', prev ? prev + '\n' + entry : entry);
    }
  }

  let tip = null;
  function ensureUI() {
    if (!document.body) return;
    if (!tip) {
      tip = document.createElement('div'); tip.id = 'darkweb-tooltip'; tip.setAttribute('data-darkweb-ui', '');
      document.body.appendChild(tip);
      document.addEventListener('mouseover', (e) => {
        const t = e.target.closest && e.target.closest('.darkweb-flagged-pattern');
        if (!t) { tip.style.display = 'none'; return; }
        const items = (t.getAttribute('data-darkweb-tip') || '').split('\n').slice(0, 3);
        tip.replaceChildren();
        items.forEach((it) => {
          const [cat, score, reason, rank] = it.split('|');
          const b = document.createElement('b'); b.textContent = '\u26A0 ' + cat + ' \u2022 risk ' + score;
          const p = document.createElement('div'); p.textContent = reason;
          const s = document.createElement('div'); s.className = 'dw-sub'; s.textContent = 'Rank ' + rank + ' in this page\u2019s priority queue';
          tip.append(b, p, s);
        });
        tip.style.display = 'block';
        const r = t.getBoundingClientRect();
        tip.style.left = Math.max(8, Math.min(innerWidth - 350, r.left)) + 'px';
        tip.style.top = (r.bottom + 8 + 150 > innerHeight ? Math.max(8, r.top - tip.offsetHeight - 8) : r.bottom + 8) + 'px';
      }, true);
    }
  }
  let badge = null;
  function showBadge(stats) {
    ensureUI();
    if (!badge) {
      badge = document.createElement('div'); badge.id = 'darkweb-badge'; badge.setAttribute('data-darkweb-ui', '');
      badge.title = 'Open Darkweb dashboard';
      badge.addEventListener('click', () => api.runtime.sendMessage({ type: 'OPEN_DASHBOARD' }));
      document.body.appendChild(badge);
    }
    badge.replaceChildren(document.createTextNode('Darkweb \u2022 ' + stats.level));
    const sc = document.createElement('span'); sc.className = 'dw-score'; sc.textContent = stats.deception + '/100';
    badge.appendChild(sc);
  }

  // ---------- 3. scan orchestration ----------
  async function scan() {
    if (scanning) return lastResult;
    scanning = true;
    try {
      const nodes = collect();
      if (!nodes) return null;
      const meta = { url: location.href, host: location.hostname, title: document.title };
      const resp = await api.runtime.sendMessage({ type: 'SCAN', nodes, meta });
      if (!resp || !resp.ok) throw new Error(resp && resp.error || 'engine unavailable');
      lastResult = resp.result;
      highlight(lastResult.findings);
      showBadge(lastResult.stats);
      return lastResult;
    } finally { scanning = false; }
  }

  function looksLikeShop() {
    if (SHOP_HOSTS.test(location.hostname + '.')) return true;
    if (document.querySelector('script[src*="cdn.shopify.com"], link[href*="cdn.shopify.com"], meta[name="shopify-checkout-api-token"]')) return true;
    return !!document.querySelector('form[action*="cart"], [name="add-to-cart"], [id*="add-to-cart" i], [class*="add-to-cart" i], button[name="add"]');
  }

  api.runtime.onMessage.addListener((msg, _s, sendResponse) => {
    if (msg.type === 'SCAN_NOW') { scan().then((r) => sendResponse({ ok: !!r, result: r })).catch((e) => sendResponse({ ok: false, error: String(e.message || e) })); return true; }
    if (msg.type === 'GET_LAST') { sendResponse({ ok: true, result: lastResult }); }
    if (msg.type === 'CLEAR') { clearHighlights(); if (badge) badge.remove(), badge = null; sendResponse({ ok: true }); }
  });

  async function boot() {
    const { settings = {} } = await api.storage.local.get('settings');
    if (settings.enabled === false || settings.autoScan === false) return;
    if (!looksLikeShop()) return;
    scan().catch(() => {});
    // dynamic pages: re-scan (debounced, max 5x) when new content is inserted
    new MutationObserver((muts) => {
      if (autoScans >= 5 || document.hidden) return;
      if (!muts.some((m) => [...m.addedNodes].some((n) => n.nodeType === 1 && !n.hasAttribute('data-darkweb-ui')))) return;
      clearTimeout(timer);
      timer = setTimeout(() => { autoScans++; scan().catch(() => {}); }, 3000);
    }).observe(document.body, { childList: true, subtree: true });
  }
  boot();
})();
