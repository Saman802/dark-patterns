// dashboard.js - companion dashboard (vanilla HTML5/ES modules-free JS; every string is inserted with textContent, never innerHTML).
const api = typeof browser !== 'undefined' ? browser : chrome;
const $ = (s, r = document) => r.querySelector(s);
const send = (m) => api.runtime.sendMessage(m);
function h(tag, attrs, ...kids) {
  const e = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs || {})) {
    if (k === 'class') e.className = v; else if (k.startsWith('on')) e[k] = v; else e.setAttribute(k, v);
  }
  kids.flat().forEach((c) => e.append(c instanceof Node ? c : document.createTextNode(String(c))));
  return e;
}
function table(head, rows) {
  return h('table', {}, h('thead', {}, h('tr', {}, head.map((x) => h('th', {}, x)))), h('tbody', {}, rows.map((r) => h('tr', {}, r.map((c) => h('td', {}, c))))));
}
const FLAGS = [[1, 'HIDDEN'], [2, 'CHECKED'], [4, 'CHECKBOX'], [8, 'BUTTON'], [16, 'TINY'], [32, 'LOW-CONTRAST'], [64, 'STRIKE'], [128, 'RADIO']];
const CTX = [[1, 'FREE_TRIAL'], [2, 'RENEW'], [4, 'SUBSCRIBE'], [8, 'DECLINE'], [16, 'CANCEL'], [32, 'URGENT']];
const bits = (v, defs) => defs.filter(([b]) => v & b).map(([, n]) => n);
let dumps = null;
async function getDumps() { if (!dumps) { const r = await send({ type: 'GET_DUMPS' }); if (r && r.ok) dumps = r; } return dumps; }

// ---------- tabs ----------
document.querySelectorAll('nav button').forEach((b) => b.onclick = () => {
  document.querySelectorAll('nav button').forEach((x) => x.classList.toggle('on', x === b));
  document.querySelectorAll('.tab').forEach((t) => t.classList.toggle('on', t.id === b.dataset.tab));
  ({ overview, history, rules, viva })[b.dataset.tab]();
});
document.querySelectorAll('.sub button').forEach((b) => b.onclick = () => {
  document.querySelectorAll('.sub button').forEach((x) => x.classList.toggle('on', x === b));
  document.querySelectorAll('.vp').forEach((t) => t.classList.toggle('on', t.id === 'v-' + b.dataset.v));
  viva();
});

// ---------- overview ----------
async function overview() {
  const { history = [] } = await api.storage.local.get('history');
  const root = $('#overview'); root.replaceChildren(h('h2', {}, 'Overview'));
  if (!history.length) { root.append(h('p', { class: 'muted' }, 'No scans yet. Visit a shopping page or press "Scan this page" in the popup.')); return; }
  const avg = Math.round(history.reduce((a, e) => a + e.score, 0) / history.length);
  const total = history.reduce((a, e) => a + e.findings, 0);
  const worst = history.reduce((a, e) => (e.score > a.score ? e : a), history[0]);
  root.append(h('div', { class: 'cards' },
    h('div', { class: 'card' }, 'Pages scanned', h('b', {}, history.length)),
    h('div', { class: 'card' }, 'Average deception score', h('b', {}, avg)),
    h('div', { class: 'card' }, 'Patterns flagged', h('b', {}, total)),
    h('div', { class: 'card' }, 'Worst site', h('b', {}, worst.score), worst.host)));
  const cats = {};
  history.forEach((e) => Object.entries(e.cats || {}).forEach(([c, n]) => { cats[c] = (cats[c] || 0) + n; }));
  const max = Math.max(1, ...Object.values(cats));
  root.append(h('h3', {}, 'Patterns by category'));
  Object.entries(cats).sort((a, b) => b[1] - a[1]).forEach(([c, n]) =>
    root.append(h('div', { class: 'row' }, h('span', {}, c), h('div', { class: 'bar', style: 'width:' + Math.round((n / max) * 420) + 'px' }), h('span', {}, n))));
  const { lastScan } = await api.storage.local.get('lastScan');
  if (lastScan) {
    root.append(h('h3', {}, 'Latest scan: ' + (lastScan.meta.host || '') + ' → ' + lastScan.result.stats.deception + '/100 (' + lastScan.result.stats.level + ')'));
    root.append(table(['#', 'Category', 'Risk', 'Evidence', 'Why'], lastScan.result.findings.slice(0, 10).map((f) => [f.rank, f.category, f.score, f.evidence, f.reason])));
  }
}

// ---------- history ----------
async function history() {
  const { history: hist = [] } = await api.storage.local.get('history');
  const root = $('#history'); root.replaceChildren(h('h2', {}, 'Scan history'));
  root.append(h('button', { class: 'btn', onclick: async () => { await api.storage.local.set({ history: [] }); history(); } }, 'Clear history'));
  root.append(table(['When', 'Site', 'Score', 'Level', 'Patterns', 'Nodes', 'Wasm µs', 'Top rule'],
    hist.map((e) => [new Date(e.ts).toLocaleString(), e.host, e.score, e.level, e.findings, e.nodes, e.micros, e.top || '–'])));
}

// ---------- rules + settings ----------
async function rules() {
  const d = await getDumps();
  const { settings = {} } = await api.storage.local.get('settings');
  const custom = settings.customRules || [];
  const root = $('#rules'); root.replaceChildren(h('h2', {}, 'Rule configuration'));
  const cat = h('input', { placeholder: 'Category', value: 'Custom' }), sev = h('input', { type: 'number', min: 1, max: 10, value: 6, style: 'width:70px' }), pat = h('input', { placeholder: 'phrase to flag, e.g. "only for you"', size: 38 });
  root.append(h('div', { class: 'note' }, 'Add your own keyword rules. They are inserted into the Aho-Corasick trie and the rule HashMap when the engine re-initialises.'),
    h('div', { class: 'row' }, cat, sev, pat, h('button', { class: 'btn', onclick: async () => {
      if (!pat.value.trim()) return;
      custom.push({ id: String(Date.now()), category: cat.value.trim() || 'Custom', severity: Number(sev.value) || 6, pattern: pat.value.trim() });
      settings.customRules = custom; await api.storage.local.set({ settings }); await send({ type: 'RULES_UPDATED' }); dumps = null; rules();
    } }, 'Add rule')));
  if (custom.length) root.append(table(['Category', 'Severity', 'Phrase', ''], custom.map((r, i) => [r.category, r.severity, r.pattern,
    h('button', { class: 'btn', onclick: async () => { custom.splice(i, 1); settings.customRules = custom; await api.storage.local.set({ settings }); await send({ type: 'RULES_UPDATED' }); dumps = null; rules(); } }, 'Remove')])));
  if (!d) { root.append(h('p', {}, 'Engine not reachable.')); return; }
  root.append(h('h3', {}, 'Rules stored in the C++ HashMap (' + d.rules.rules.length + ')'));
  root.append(table(['Rule id', 'Category', 'Severity', 'Kind', 'Phrases', 'Explanation'], d.rules.rules.map((r) => [h('code', {}, r.id), r.category, r.severity,
    r.custom ? 'custom' : r.structural ? 'structural (DOM tree)' : 'keyword (Aho-Corasick)', r.patterns.length, r.explanation])));
}

// ---------- viva mode ----------
let vivaInit = {};
async function viva() {
  const active = ($('.sub button.on') || {}).dataset.v || 'ac';
  const fn = { ac: vAC, tree: vTree, hash: vHash, heap: vHeap, cheat: vCheat }[active];
  await fn();
}

async function vAC() {
  const d = await getDumps(); const root = $('#v-ac'); root.replaceChildren();
  if (!d) { root.append('Engine not reachable.'); return; }
  const nodes = d.trie.nodes;
  root.append(h('h3', {}, 'Aho-Corasick automaton'),
    h('div', { class: 'cards' }, h('div', { class: 'card' }, 'Trie nodes', h('b', {}, nodes.length)), h('div', { class: 'card' }, 'Patterns', h('b', {}, d.trie.patternCount)),
      h('div', { class: 'card' }, 'Match cost', h('b', {}, 'O(N+Z)'))),
    h('div', { class: 'note' }, 'Build: insert every phrase into a trie, then a BFS computes each node\'s failure link (longest proper suffix that is also a trie prefix) and dictionary link. Search: one left-to-right pass; on a missing edge follow failure links (amortised O(1)). No pattern is ever re-scanned.'));
  const ta = h('textarea', {}, 'Hurry! Only 2 left in stock. Offer ends in 02:14:59');
  const out = h('div', {});
  const run = async () => {
    const r = await send({ type: 'TRACE_TEXT', text: ta.value }); if (!r || !r.ok) return;
    out.replaceChildren(table(['i', 'char', 'state before (string)', 'fail-link hops', 'state after (string)', 'patterns reported'], r.trace.steps.map((s) => [
      s.i, h('code', {}, s.ch === ' ' ? '␣' : s.ch), h('code', {}, s.fromPath || '(root)'), s.hops.length ? s.hops.map((p) => p || '(root)').join(' → ') : '–',
      h('code', {}, s.toPath || '(root)'), s.out.map((o) => h('span', { class: 'hit' }, o))])));
  };
  root.append(h('h3', {}, 'Live trace'), ta, h('button', { class: 'btn', onclick: run }, 'Trace automaton'), out);
  // pattern path through the trie with failure links
  const byEdge = new Map(); nodes.forEach((n) => { if (n.p >= 0) byEdge.set(n.p + ':' + n.c, n.id); });
  const pathOf = (id) => { let s = ''; for (let x = id; x > 0; x = nodes[x].p) s = nodes[x].c + s; return s; };
  const pin = h('input', { value: 'only 2 left', size: 28 }); const pout = h('div', {});
  const showPath = () => {
    let s = 0; const rows = [];
    for (const ch of pin.value.toLowerCase()) {
      const t = byEdge.get(s + ':' + ch); if (t === undefined) { rows.push(['–', ch, 'not a pattern prefix', '']); break; }
      s = t; rows.push([nodes[s].d, h('code', {}, ch), 'state #' + s, h('code', {}, pathOf(nodes[s].f) || '(root)')]);
    }
    pout.replaceChildren(table(['depth', 'edge', 'trie node', 'failure link → string'], rows));
  };
  root.append(h('h3', {}, 'Trie path & failure links of a phrase'), pin, ' ', h('button', { class: 'btn', onclick: showPath }, 'Show path'), pout);
  run(); showPath();
}

async function vTree() {
  const root = $('#v-tree'); root.replaceChildren(h('h3', {}, 'N-ary DOM tree (left-child / right-sibling)'));
  const { lastScan } = await api.storage.local.get('lastScan');
  root.append(h('div', { class: 'note' }, 'Each element is a node with parent / firstChild / nextSibling indices. A post-order DFS (explicit stack) ORs context bits and subtree sizes upward, so a parent instantly "knows" what text lives below it. Structural rules then look at parent, siblings and children of a node: e.g. a ticked checkbox + SUBSCRIBE bit within 3 ancestors, or a faded DECLINE button beside a prominent BUTTON sibling.'));
  if (!lastScan) { root.append(h('p', { class: 'muted' }, 'Scan a page first.')); return; }
  const t = lastScan.result.tree, kids = new Map();
  t.forEach((n) => { (kids.get(n.p) || kids.set(n.p, []).get(n.p)).push(n); });
  const lines = [];
  const walk = (n, d) => {   // pre-order, iterative
    const st = [[n, d]];
    while (st.length) {
      const [x, depth] = st.pop();
      const f = bits(x.f, FLAGS), m = bits(x.m, CTX);
      lines.push([depth, x, f, m]);
      (kids.get(x.id) || []).slice().reverse().forEach((c) => st.push([c, depth + 1]));
    }
  };
  (kids.get(-1) || []).forEach((r) => walk(r, 0));
  const pre = h('div', { class: 'tree' });
  lines.forEach(([d, x, f, m]) => {
    const row = h('div', {}, '  '.repeat(d) + '<' + x.tag.toLowerCase() + '> #' + x.id + ' size=' + x.s + (f.length ? ' [' + f.join(',') + ']' : '') + (m.length ? ' ctx{' + m.join(',') + '}' : '') + (x.t ? ' "' + x.t + '"' : ''));
    if (x.r.length) row.append(h('span', { class: 'flag' }, '  ⚠ ' + x.r.join(', ')));
    pre.append(row);
  });
  root.append(h('p', {}, 'Showing only flagged nodes and their ancestors (' + t.length + ' of ' + lastScan.result.stats.nodes + ' nodes, max depth ' + lastScan.result.stats.maxDepth + ').'), pre);
}

// 64-bit FNV-1a exactly as in cpp/hash_map.hpp
function fnv1a(s) { let hv = 0xcbf29ce484222325n; const M = (1n << 64n) - 1n; for (const b of new TextEncoder().encode(s)) { hv ^= BigInt(b); hv = (hv * 0x100000001b3n) & M; } return hv; }
async function vHash() {
  const d = await getDumps(); const root = $('#v-hash'); root.replaceChildren(h('h3', {}, 'Hash map (separate chaining, FNV-1a)'));
  if (!d) return;
  const H = d.hash;
  root.append(h('div', { class: 'cards' }, h('div', { class: 'card' }, 'Entries', h('b', {}, H.size)), h('div', { class: 'card' }, 'Buckets', h('b', {}, H.buckets)),
    h('div', { class: 'card' }, 'Load factor', h('b', {}, H.load)), h('div', { class: 'card' }, 'Collisions', h('b', {}, H.collisions)), h('div', { class: 'card' }, 'Longest chain', h('b', {}, H.maxChain))),
    h('div', { class: 'note' }, 'slot = FNV-1a(key) & (buckets-1). Colliding keys chain in a linked list; the table doubles and rehashes when load > 0.75, so lookups stay O(1) amortised. Used for rule id → {category, severity, explanation} and category → weight.'));
  const inp = h('input', { value: 'T_URGENCY', size: 26 }), out = h('div', {});
  const go = () => {
    const hv = fnv1a(inp.value), slot = Number(hv & BigInt(H.buckets - 1)), b = H.table.find((x) => x.b === slot);
    out.replaceChildren(h('p', {}, 'FNV-1a(' + inp.value + ') = 0x' + hv.toString(16) + ' → slot ' + slot + ' → chain: ', b ? b.keys.map((k) => h('span', { class: k === inp.value ? 'pill r' : 'pill' }, k)) : 'empty bucket (key not present)'));
  };
  root.append(h('h3', {}, 'Lookup demo'), inp, ' ', h('button', { class: 'btn', onclick: go }, 'Hash it'), out,
    h('h3', {}, 'Bucket table'), table(['bucket', 'chain'], H.table.map((x) => [x.b, x.keys.map((k) => h('span', { class: 'pill' }, k))])));
  go();
}

async function vHeap() {
  const root = $('#v-heap'); root.replaceChildren(h('h3', {}, 'Max-priority queue (binary heap)'));
  const { lastScan } = await api.storage.local.get('lastScan');
  root.append(h('div', { class: 'note' }, 'Every finding is pushed into an array-backed max-heap keyed by risk score (sift-up). Popping the root repeatedly (sift-down) yields findings in descending risk: O(log n) each, O(n log n) for the whole report. Parent(i)=⌊(i-1)/2⌋, children 2i+1, 2i+2.'));
  if (!lastScan) { root.append(h('p', { class: 'muted' }, 'Scan a page first.')); return; }
  const arr = lastScan.result.heap.array.map((x) => ({ ...x }));
  const view = h('div', {}), log = h('div', {});
  const draw = (a, hi) => {
    view.replaceChildren();
    for (let lvl = 0, i = 0; i < a.length; ++lvl) {
      const row = h('div', { class: 'lvl' });
      for (let k = 0; k < 2 ** lvl && i < a.length; ++k, ++i) row.append(h('div', { class: 'cell' + (i === hi ? ' now' : '') }, a[i].s, h('small', {}, '[' + i + '] ' + a[i].r.replace(/^[TS]_/, '').slice(0, 14))));
      view.append(row);
    }
  };
  const siftDown = (a, i) => { for (;;) { const l = 2 * i + 1, r = l + 1; let b = i; if (l < a.length && a[l].s > a[b].s) b = l; if (r < a.length && a[r].s > a[b].s) b = r; if (b === i) break; [a[i], a[b]] = [a[b], a[i]]; i = b; } };
  let work = arr.map((x) => ({ ...x })), popped = [];
  draw(work, 0);
  root.append(h('p', {}, 'Heap array in level order (exactly what the C++ MaxHeap held before draining):'), view,
    h('button', { class: 'btn', onclick: () => {
      if (!work.length) { work = arr.map((x) => ({ ...x })); popped = []; log.replaceChildren(); draw(work, 0); return; }
      const top = work[0]; work[0] = work[work.length - 1]; work.pop(); if (work.length) siftDown(work, 0);
      popped.push(top); draw(work, 0);
      log.replaceChildren(table(['pop #', 'risk score', 'rule', 'node'], popped.map((p, i) => [i + 1, p.s, p.r, p.n])));
    } }, 'Pop max (step)'), ' ', h('span', { class: 'muted' }, '(press repeatedly; when empty it resets)'), log,
    h('h3', {}, 'Final ranking (report order)'), table(['rank', 'rule', 'category', 'risk', 'severity × weight × confidence'],
      lastScan.result.findings.map((f) => [f.rank, f.rule, f.category, f.score, f.severity + ' × w × ' + f.confidence])),
    h('p', {}, 'Deception score = 100·(1 − e^(−R/35)),  R = Σ riskᵢ·0.9ⁱ  ⇒ the top risks dominate and repeated minor patterns have diminishing returns. This page: R = ' + lastScan.result.stats.rawScore + ' → ' + lastScan.result.stats.deception + '/100.'));
}

function vCheat() {
  const root = $('#v-cheat'); root.replaceChildren(h('h3', {}, 'Complexity & defence notes'));
  root.append(table(['Structure', 'Where used', 'Time', 'Why this one'], [
    ['Aho-Corasick', 'Find ~270 deceptive phrases in every text node', 'build O(Σ|p|), search O(N+Z)', 'Naive matching is O(N·Σ|p|); one pass finds all patterns at once'],
    ['N-ary tree (LCRS) + iterative DFS', 'Pre-order text gathering, post-order bit aggregation, parent/sibling checks', 'O(V) per traversal', 'DOM is a tree; explicit stack means no recursion-depth crash on deep pages'],
    ['Hash map (chaining, FNV-1a)', 'rule id → metadata, category → weight, finding dedupe', 'O(1) amortised', 'Every hit needs its severity instantly; dedupe keys are node|rule'],
    ['Max-heap', 'Rank findings by risk score', 'push/pop O(log n)', 'We only ever need "current highest risk"; no full sort needed until drain'],
  ]), h('div', { class: 'note' }, 'Zero machine learning: every verdict is a lookup, a tree walk or arithmetic on fixed severity weights, so each flag has an exact, explainable reason. Known limitations: keyword lists are English-only; style heuristics (tiny font, contrast < 2.5) can misfire on legitimate fine print; countdown detection looks for HH:MM:SS near urgency wording and cannot tell a real deadline from a fake one.'));
}

overview();
