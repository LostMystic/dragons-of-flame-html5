// The game's files (data/, assets/, the soundfont) come in packs of about 1 MB, named by what's
// in them (files/<name>.<hash>.pack, made by tools/web/pack.py). version.json lists the current
// ones. A pack is downloaded once and kept in the browser's cache storage; a new version of the
// game only downloads the packs that changed. Used by the game page and by exit.html.
//
// Pack format: "FEPK", a 4-byte little-endian length, that much JSON ([[path, offset, size], ...]),
// then the files' bytes (offsets count from the end of the JSON).
(function () {
  const CACHE = 'dof-packs';
  const base = new URL('.', location.href);
  const url = p => new URL('files/' + p.file, base).href;
  const caches_ok = typeof caches !== 'undefined';

  async function manifest() {
    const r = await fetch(new URL('version.json', base), { cache: 'no-store' });
    if (!r.ok) throw new Error('version.json: ' + r.status);
    return r.json();
  }

  async function cached(p) {
    if (!caches_ok) return null;
    try {
      const c = await caches.open(CACHE);
      const r = await c.match(url(p));
      return r ? await r.arrayBuffer() : null;
    } catch (e) { return null; }
  }

  // Download a pack (progress(bytes) as it comes), keep it in the cache, return its bytes.
  async function download(p, progress) {
    const r = await fetch(url(p));
    if (!r.ok) throw new Error(p.file + ': ' + r.status);
    let buf;
    if (r.body && r.body.getReader) {
      const reader = r.body.getReader(), parts = [];
      let got = 0;
      for (;;) {
        const { done, value } = await reader.read();
        if (done) break;
        parts.push(value);
        got += value.length;
        if (progress) progress(got);
      }
      const all = new Uint8Array(got);
      let at = 0;
      for (const v of parts) { all.set(v, at); at += v.length; }
      buf = all.buffer;
    } else {
      buf = await r.arrayBuffer();
      if (progress) progress(buf.byteLength);
    }
    if (caches_ok) {
      try {
        const c = await caches.open(CACHE);
        await c.put(url(p), new Response(buf, { headers: { 'Content-Type': 'application/octet-stream' } }));
      } catch (e) { console.warn('could not keep', p.file, e); }
    }
    return buf;
  }

  async function get(p, progress) {
    const buf = await cached(p);
    if (buf) { if (progress) progress(p.size); return buf; }
    return download(p, progress);
  }

  function unpack(buf) {
    const bytes = new Uint8Array(buf), view = new DataView(buf);
    if (String.fromCharCode(...bytes.subarray(0, 4)) !== 'DFPK') throw new Error('not a pack');
    const n = view.getUint32(4, true);
    const index = JSON.parse(new TextDecoder().decode(bytes.subarray(8, 8 + n)));
    const start = 8 + n;
    return index.map(([path, off, size]) => ({ path, data: bytes.subarray(start + off, start + off + size) }));
  }

  // The packs of `m` not yet in the cache.
  async function missing(m) {
    const out = [];
    for (const p of m.packs) if (!(await has(p))) out.push(p);
    return out;
  }
  async function has(p) {
    if (!caches_ok) return false;
    try { return !!(await (await caches.open(CACHE)).match(url(p))); } catch (e) { return false; }
  }

  // Throw away packs no longer in `m` (old versions).
  async function prune(m) {
    if (!caches_ok) return;
    try {
      const keep = new Set(m.packs.map(url));
      const c = await caches.open(CACHE);
      for (const req of await c.keys()) if (!keep.has(req.url)) await c.delete(req);
    } catch (e) { /* not important */ }
  }

  // Download every pack of `m` that isn't kept yet. progress(done, total) in bytes.
  async function fetch_all(m, progress) {
    const need = await missing(m);
    const total = need.reduce((a, p) => a + p.size, 0);
    let done = 0;
    for (const p of need) {
      await download(p, got => progress && progress(done + got, total));
      done += p.size;
    }
    return need.length;
  }

  const installed = () => { try { return JSON.parse(localStorage.getItem('dof_version') || 'null'); } catch (e) { return null; } };
  const set_installed = m => { try { localStorage.setItem('dof_version', JSON.stringify({ version: m.version, built: m.built })); } catch (e) { } };

  window.DofPacks = { manifest, get, unpack, missing, prune, fetch_all, installed, set_installed };
})();
