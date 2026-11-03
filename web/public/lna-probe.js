const out = document.getElementById('out');
const lines = [];
function render() { out.innerHTML = lines.join('\n'); }
function say(target, text, ok) {
  lines.push(`${target}: ${text}`);
  render();
}
async function permission() {
  try {
    const p = await navigator.permissions.query({ name: 'local-network' });
    return p.state;
  } catch (e) {
    try {
      const p = await navigator.permissions.query({ name: 'localNetworkAccess' });
      return p.state;
    } catch (e2) { return 'unqueryable'; }
  }
}
async function probe(target, url) {
  say(target, `requesting ${url} …`);
  const t0 = performance.now();
  try {
    const r = await fetch(url, { mode: 'cors', targetAddressSpace: 'local' });
    const text = await r.text();
    say(target, `OK ${r.status} in ${Math.round(performance.now() - t0)} ms → ${text.slice(0, 140)}`, true);
  } catch (e) {
    say(target, `FAILED in ${Math.round(performance.now() - t0)} ms → ${e}`, false);
  }
}
(async () => {
  say('browser', `${navigator.userAgent}`);
  say('permission', await permission());
  await probe('ip 192.168.0.38:8124', 'http://192.168.0.38:8124/api/status');
  await probe('k7.local', 'http://k7.local/api/status');
  say('done', 'both attempts finished');
})();
