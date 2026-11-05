# Darkweb — Rule-Based Dark Pattern Detector

Browser extension (Chrome / Firefox, Manifest V3) + dashboard. All detection is hand-written DSA in C++ compiled to
WebAssembly. **No machine learning.**

## Folder structure
```
darkweb/
├── Makefile                  # make wasm | test | chrome | firefox
├── cpp/
│   ├── hash_map.hpp          # chaining hash table, FNV-1a, rehash at load 0.75
│   ├── max_heap.hpp          # array binary max-heap (sift-up / sift-down)
│   ├── dom_tree.hpp          # N-ary tree (left-child/right-sibling), iterative pre/post-order DFS
│   ├── aho_corasick.hpp      # trie + BFS failure links + dictionary links
│   ├── rules.hpp             # rule book: rules in HashMap, ~270 phrases, structural rule metadata
│   ├── engine.cpp            # pipeline + extern "C" exports (the Wasm bridge)
│   └── test_main.cpp         # native test harness (no browser needed)
├── tests/sample_nodes.tsv    # a fake checkout page containing every pattern
└── extension/
    ├── manifest.json (+ manifest.firefox.json)
    ├── background.js         # loads Wasm, owns the engine, stores history
    ├── content/content.js|css# DOM capture, red highlighting, tooltip, score badge
    ├── popup/                # quick scan + top findings
    ├── dashboard/            # overview, history, rule editor, Viva Defense Mode
    └── wasm/                 # darkweb_engine.js/.wasm appear here after `make wasm`
```

## Build & run
1. Install the Emscripten SDK and activate it (`source emsdk_env.sh`).
2. `make wasm` → creates `extension/wasm/darkweb_engine.{js,wasm}`.
3. Chrome: `chrome://extensions` → Developer mode → *Load unpacked* → select `extension/`.
   Firefox: `make firefox`, then `about:debugging` → *Load Temporary Add-on* → `build/firefox/manifest.json`.
4. Open a shopping page (auto-scan) or press **Scan this page** in the popup. Right-click the extension → *Options* (or click the
   on-page badge) for the dashboard.
5. No browser handy? `make test` runs the same C++ natively on `tests/sample_nodes.tsv`.

## Data flow
`content.js` walks the DOM iteratively and sends one line per element: `parent \t TAG \t flags \t own text` →
`background.js` copies it into Wasm memory → `dw_scan()` returns a JSON report → content script adds
`.darkweb-flagged-pattern` and a tooltip to each flagged element.

## Pipeline inside `dw_scan` (what to say in the viva)
1. **Tree build** – lines → `DomTree` (N-ary tree, LCRS). O(V).
2. **Aho-Corasick** over every node's own text – all phrases in one pass, O(N+Z). Whole-word check removes "trends in" ≠ "ends in".
3. **Post-order DFS** ORs context bits (FREE_TRIAL, RENEW, SUBSCRIBE, DECLINE, CANCEL, URGENT) and subtree sizes up the tree.
4. **Structural rules** use parent / sibling / subtree information:
   pre-ticked or hidden opt-in checkbox · tiny decline button beside a prominent button (misdirection) · free-trial + auto-renew in the
   smallest common subtree (forced continuity) · tiny cancel link · HH:MM:SS clock next to urgency wording.
5. **Hash map** gives O(1) rule/category-weight lookup and de-duplicates findings (`node|rule`).
6. **Max-heap** ranks findings by `risk = severity × category weight × confidence` (×1.4 if the text is tiny/low-contrast).
7. **Deception score** = `100·(1 − e^(−R/35))`, `R = Σ riskᵢ·0.9ⁱ` (sorted descending) → Low / Moderate / High / Severe.

## Known limitations (say these before your examiner does)
- English keyword lists only; custom phrases can be added in the dashboard.
- Style heuristics (font ≤ 11 px, WCAG contrast < 2.5) can misfire on legitimate fine print.
- A countdown can't be proven fake from a single snapshot; it is flagged as pressure, not as a lie.
- Custom-styled checkboxes are treated as "hidden" only if neither the box nor a label is visible.
