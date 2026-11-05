const api = typeof browser !== 'undefined' ? browser : chrome;
const $ = (id) => document.getElementById(id);

function render(res) {
  if (!res) return;
  $('score').textContent = res.stats.deception;
  $('level').textContent = res.stats.level + ' deception • ' + res.stats.findings + ' patterns • ' + res.stats.micros + ' µs in Wasm';
  const ol = $('top'); ol.replaceChildren();
  res.findings.slice(0, 5).forEach((f) => {
    const li = document.createElement('li');
    const b = document.createElement('b'); b.textContent = f.category + ' (' + f.score + ') ';
    const s = document.createElement('small'); s.textContent = f.evidence;
    li.append(b, s); ol.appendChild(li);
  });
}
async function tab() { return (await api.tabs.query({ active: true, currentWindow: true }))[0]; }

$('scan').onclick = async () => {
  $('msg').textContent = 'Scanning…';
  try {
    const r = await api.tabs.sendMessage((await tab()).id, { type: 'SCAN_NOW' });
    if (r && r.ok) { $('msg').textContent = ''; render(r.result); } else $('msg').textContent = 'Scan failed: ' + (r && r.error || 'unknown');
  } catch (e) { $('msg').textContent = 'Cannot scan this page (reload it, or it is a browser-internal page).'; }
};
$('clear').onclick = async () => { try { await api.tabs.sendMessage((await tab()).id, { type: 'CLEAR' }); } catch (e) {} };
$('dash').onclick = () => api.runtime.openOptionsPage();
$('auto').onchange = async (e) => {
  const { settings = {} } = await api.storage.local.get('settings');
  settings.autoScan = e.target.checked;
  await api.storage.local.set({ settings });
};
(async () => {
  const { settings = {} } = await api.storage.local.get('settings');
  $('auto').checked = settings.autoScan !== false;
  try { const r = await api.tabs.sendMessage((await tab()).id, { type: 'GET_LAST' }); if (r && r.result) render(r.result); } catch (e) {}
})();
