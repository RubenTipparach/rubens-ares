/*! coi-serviceworker — adds COOP/COEP headers via Service Worker
 *  Enables SharedArrayBuffer on any static host (itch.io, GitHub Pages, etc.)
 *  No server configuration needed.
 *
 *  How it works:
 *  1. Page registers this as a service worker
 *  2. On first load, SW installs and reloads the page
 *  3. On subsequent loads, SW intercepts all responses and adds COOP/COEP headers
 *  4. Browser sees the headers → enables crossOriginIsolated → SharedArrayBuffer works
 */

if (typeof window !== 'undefined') {
  // ── Page context: register the service worker ──
  const register = () => {
    if (!window.isSecureContext) {
      console.warn('coi-serviceworker: not in a secure context (HTTPS or localhost required)');
      return;
    }
    if (!('serviceWorker' in navigator)) {
      console.warn('coi-serviceworker: Service Workers not supported');
      return;
    }
    navigator.serviceWorker.register(window.document.currentScript.src).then(
      reg => {
        // If the SW is active and controlling the page, we're good
        if (reg.active && navigator.serviceWorker.controller) {
          return;
        }
        // Otherwise reload once the SW is ready
        reg.addEventListener('updatefound', () => {
          const sw = reg.installing;
          sw.addEventListener('statechange', () => {
            if (sw.state === 'activated') {
              window.location.reload();
            }
          });
        });
      },
      err => console.error('coi-serviceworker: registration failed:', err)
    );
  };
  register();
} else {
  // ── Service Worker context: intercept and add headers ──
  self.addEventListener('install', () => self.skipWaiting());
  self.addEventListener('activate', e => e.waitUntil(self.clients.claim()));

  self.addEventListener('fetch', e => {
    // Only handle same-origin navigations and subresources
    if (e.request.cache === 'only-if-cached' && e.request.mode !== 'same-origin') return;

    e.respondWith(
      fetch(e.request).then(response => {
        // Clone response and add COOP/COEP headers
        const headers = new Headers(response.headers);
        headers.set('Cross-Origin-Opener-Policy', 'same-origin');
        headers.set('Cross-Origin-Embedder-Policy', 'require-corp');

        return new Response(response.body, {
          status: response.status,
          statusText: response.statusText,
          headers
        });
      })
    );
  });
}
