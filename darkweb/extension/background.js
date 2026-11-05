// background.js - owns the WebAssembly engine (service worker in Chrome, event page in Firefox).
// Content scripts never touch Wasm: they send a flat DOM dump here and get a JSON report back.
// This avoids page-CSP problems with WebAssembly in content scripts.
if (typeof importScripts === 'function') importScripts('wasm/darkweb_engine.js');  // Chrome SW (top level only)

const api = typeof browser !== 'undefined' ? browser : chrome;
let modulePromise = null;

function loadModule() {
  if (!modulePromise) {
    modulePromise = createDarkweb({ locateFile: (f) => api.runtime.getURL('wasm/' + f) }).then(async (M) => {
      await reinit(M);
      return M;
    });
  }
  return modulePromise;
}

// call a C function taking one string (malloc, NOT stack: DOM dumps can be megabytes)
function callStr(M, fn, arg) {
  let ptr = 0;
  try {
    if (arg !== undefined) {
      const n = M.lengthBytesUTF8(arg) + 1;
      ptr = M._malloc(n);
      M.stringToUTF8(arg, ptr, n);
    }
    return M.UTF8ToString(M['_' + fn](ptr));
  } finally { if (ptr) M._free(ptr); }
}

async function reinit(M) {
  const { settings = {} } = await api.storage.local.get('settings');
  const tsv = (settings.customRules || []).map((r) => [r.id, r.category, r.severity, r.pattern].join('\t')).join('\n');
  callStr(M, 'dw_init', tsv);           // rebuilds HashMap + Aho-Corasick automaton
}

async function saveScan(result, meta) {
  const s = result.stats;
  const cats = {};
  result.findings.forEach((f) => { cats[f.category] = (cats[f.category] || 0) + 1; });
  const entry = {
    ts: Date.now(), url: meta.url, host: meta.host, title: (meta.title || '').slice(0, 80),
    score: s.deception, level: s.level, findings: s.findings, nodes: s.nodes, micros: s.micros, cats,
    top: result.findings[0] ? result.findings[0].rule : null,
  };
  const { history = [] } = await api.storage.local.get('history');
  history.unshift(entry);
  await api.storage.local.set({ history: history.slice(0, 200), lastScan: { meta, result } });
}

api.runtime.onMessage.addListener((msg, sender, sendResponse) => {
  (async () => {
    const M = await loadModule();
    switch (msg.type) {
      case 'SCAN': {
        const result = JSON.parse(callStr(M, 'dw_scan', msg.nodes));
        await saveScan(result, msg.meta);
        if (sender.tab && api.action) {
          api.action.setBadgeText({ tabId: sender.tab.id, text: String(result.stats.deception) });
          api.action.setBadgeBackgroundColor({ tabId: sender.tab.id, color: result.stats.deception >= 45 ? '#d90429' : '#e09f00' });
        }
        return { ok: true, result };
      }
      case 'TRACE_TEXT': return { ok: true, trace: JSON.parse(callStr(M, 'dw_trace_text', msg.text)) };
      case 'GET_DUMPS':
        return { ok: true, rules: JSON.parse(callStr(M, 'dw_rules_dump')), trie: JSON.parse(callStr(M, 'dw_trie_dump')), hash: JSON.parse(callStr(M, 'dw_hash_dump')) };
      case 'RULES_UPDATED': await reinit(M); return { ok: true };
      case 'OPEN_DASHBOARD': api.runtime.openOptionsPage(); return { ok: true };
      default: return { ok: false, error: 'unknown message' };
    }
  })().then(sendResponse).catch((e) => sendResponse({ ok: false, error: String(e && e.message || e) }));
  return true;  // async response
});
