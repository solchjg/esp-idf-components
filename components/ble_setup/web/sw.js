// Offline copy of the BLE setup page: the first visit over HTTPS caches these
// files; later visits try the network for up to 3 s (so an update shows when
// online) and otherwise open the cached copy, so only Bluetooth is needed.
const CACHE = 'ble-setup-v1';
const FILES = ['./', './index.html', './manifest.webmanifest', './icon.svg'];

self.addEventListener('install', (e) => {
  e.waitUntil(caches.open(CACHE).then((c) => c.addAll(FILES)).then(() => self.skipWaiting()));
});

self.addEventListener('activate', (e) => {
  e.waitUntil(caches.keys()
    .then((keys) => Promise.all(keys.filter((k) => k !== CACHE).map((k) => caches.delete(k))))
    .then(() => self.clients.claim()));
});

self.addEventListener('fetch', (e) => {
  const req = e.request;
  if (req.method !== 'GET' || new URL(req.url).origin !== self.location.origin) return;
  e.respondWith((async () => {
    const cache = await caches.open(CACHE);
    try {
      const res = await Promise.race([
        fetch(req),
        new Promise((_, no) => setTimeout(() => no(new Error('timeout')), 3000)),
      ]);
      if (res.ok) cache.put(req, res.clone());
      return res;
    } catch (err) {
      const hit = await cache.match(req, { ignoreSearch: true })
        || (req.mode === 'navigate' && await cache.match('./index.html'));
      if (hit) return hit;
      throw err;
    }
  })());
});
