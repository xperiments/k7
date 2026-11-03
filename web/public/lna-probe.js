const out = document.getElementById('out');
const results = {};
function show() { out.textContent = JSON.stringify(results, null, 1); }
async function probe(name, url) {
  results[name] = 'pending';
  show();
  const started = Date.now();
  try {
    const r = await fetch(url, { mode: 'cors' });
    results[name] = { status: r.status, body: (await r.text()).slice(0, 120), ms: Date.now() - started };
  } catch (e) {
    results[name] = { error: String(e), ms: Date.now() - started };
  }
  show();
}
(async () => {
  await probe('ip', 'http://192.168.0.38:8124/api/status');
  await probe('mdns', 'http://k7.local/api/status');
})();
