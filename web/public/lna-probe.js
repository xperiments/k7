const results = {};
async function probe(name, url) {
  try {
    const r = await fetch(url, { mode: 'cors' });
    results[name] = { ok: r.ok, status: r.status, body: (await r.text()).slice(0, 90) };
  } catch (e) {
    results[name] = { error: e && e.message ? e.message : String(e) };
  }
  document.getElementById('out').textContent = JSON.stringify(results);
}
probe('ip', 'http://192.168.0.38:8124/api/status').then(() =>
  probe('mdns', 'http://k7.local/api/status')
);
