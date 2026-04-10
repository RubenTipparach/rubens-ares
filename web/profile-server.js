// Combined HTTP + WebSocket server for ares WASM development
//
// Features:
// - Serves static files with COOP/COEP headers (SharedArrayBuffer)
// - WebSocket endpoint for profile data streaming
// - HTTP control endpoint to trigger client reload (POST /reload)
// - Appends samples to profile-data.csv with timestamp + label
// - Time-based or sample-based collection
//
// Usage:
//   node profile-server.js                              # run forever, no CSV
//   node profile-server.js --duration=300 --label="baseline"
//   node profile-server.js --duration=300 --label="ai-skip"
//   node profile-server.js --samples=10 --label="test"
//
// Control:
//   curl -X POST http://localhost:3000/reload   # reload connected clients
//
// View graphs:
//   open http://localhost:3000/graphs.html

const http = require('http');
const fs = require('fs');
const path = require('path');
const { WebSocketServer } = require('ws');

const PORT = 3000;
const DIR = __dirname;
const CSV_PATH = path.join(DIR, 'profile-data.csv');

// Parse args
let maxSamples = -1;
let durationSec = -1;
let label = 'unlabeled';
for (const arg of process.argv.slice(2)) {
  if (arg.startsWith('--samples=')) maxSamples = parseInt(arg.slice(10), 10);
  else if (arg.startsWith('--duration=')) durationSec = parseInt(arg.slice(11), 10);
  else if (arg.startsWith('--label=')) label = arg.slice(8);
}

// CSV columns (per-sample, fine-grained)
const CSV_HEADERS = ['timestamp','label','buildId','cpuMs','syncMs','rdpMs','renderMs','totalMs','instrPerSec','n64Fps','browserFps'];

// Initialize CSV with header if doesn't exist
if (!fs.existsSync(CSV_PATH)) {
  fs.writeFileSync(CSV_PATH, CSV_HEADERS.join(',') + '\n');
}

// Per-experiment summary CSV (one row per run, for trend tracking)
const SCORES_PATH = path.join(DIR, 'benchmark-scores.csv');
const SCORES_HEADERS = [
  'timestamp','label','buildId','sampleCount',
  'avgN64Fps','minN64Fps','maxN64Fps',
  'avgTotalMs','avgCpuMs','avgSyncMs','avgRdpMs','avgInstrPerSec',
  'avgBrowserFps',
];
if (!fs.existsSync(SCORES_PATH)) {
  fs.writeFileSync(SCORES_PATH, SCORES_HEADERS.join(',') + '\n');
}

const MIME = {
  '.html': 'text/html',
  '.js':   'application/javascript',
  '.wasm': 'application/wasm',
  '.data': 'application/octet-stream',
  '.css':  'text/css',
  '.png':  'image/png',
  '.json': 'application/json',
  '.csv':  'text/csv',
  '.z64':  'application/octet-stream',
};

let collectedSamples = 0;
let lastSampleData = null;
const startTime = Date.now();
let collecting = (maxSamples > 0 || durationSec > 0);

// In-memory list of collected samples (for end-of-run summary)
const samplesForSummary = [];

// Write a per-experiment summary row when the run ends.
// Excludes the first 2 samples (startup spikes) so the average reflects steady state.
function writeSummary(reason) {
  if (samplesForSummary.length === 0) return;
  const skip = Math.min(2, Math.max(0, samplesForSummary.length - 3));
  const steady = samplesForSummary.slice(skip);
  const buildId = steady[0].buildId || '';
  const num = (k) => steady.map(s => parseFloat(s[k]) || 0);
  const avg = (k) => (num(k).reduce((a, b) => a + b, 0) / steady.length).toFixed(2);
  const min = (k) => Math.min(...num(k)).toFixed(2);
  const max = (k) => Math.max(...num(k)).toFixed(2);
  const row = [
    Date.now(), label, buildId, steady.length,
    avg('n64Fps'), min('n64Fps'), max('n64Fps'),
    avg('totalMs'), avg('cpuMs'), avg('syncMs'), avg('rdpMs'), avg('instrPerSec'),
    avg('browserFps'),
  ].join(',');
  fs.appendFileSync(SCORES_PATH, row + '\n');
  console.log(`\n[summary ${reason}] label="${label}" build="${buildId}"`);
  console.log(`  samples (steady): ${steady.length} of ${samplesForSummary.length}`);
  console.log(`  N64 FPS: ${avg('n64Fps')} avg (${min('n64Fps')}-${max('n64Fps')})`);
  console.log(`  Total:   ${avg('totalMs')}ms  CPU: ${avg('cpuMs')}ms  Sync: ${avg('syncMs')}ms  RDP: ${avg('rdpMs')}ms`);
  console.log(`  Wrote row to ${path.basename(SCORES_PATH)}`);
}

const server = http.createServer((req, res) => {
  res.setHeader('Cross-Origin-Opener-Policy', 'same-origin');
  res.setHeader('Cross-Origin-Embedder-Policy', 'require-corp');
  res.setHeader('Cache-Control', 'no-cache');

  if (req.method === 'POST' && req.url === '/reload') {
    broadcast({ type: 'reload' });
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({ ok: true, clients: wss.clients.size }));
    return;
  }

  if (req.method === 'POST' && req.url === '/clear-csv') {
    fs.writeFileSync(CSV_PATH, CSV_HEADERS.join(',') + '\n');
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({ ok: true }));
    return;
  }

  if (req.url === '/build-info') {
    // Returns the wasm build timestamp by reading mtime + scanning for embedded date string
    try {
      const wasmPath = path.join(DIR, 'ares.wasm');
      const stat = fs.statSync(wasmPath);
      // Read first ~5MB and search for "Mon DD YYYY HH:MM:SS" pattern
      const buf = fs.readFileSync(wasmPath);
      const m = buf.toString('latin1').match(/[A-Z][a-z]{2} [\s\d]\d \d{4} \d\d:\d\d:\d\d/);
      const buildId = m ? m[0] : '?';
      res.writeHead(200, { 'Content-Type': 'application/json' });
      res.end(JSON.stringify({
        buildId,
        mtime: stat.mtime.toISOString(),
        size: stat.size,
      }));
    } catch (e) {
      res.writeHead(500);
      res.end(JSON.stringify({ error: e.message }));
    }
    return;
  }

  if (req.url === '/stats') {
    // Quick summary endpoint for Claude to query via curl
    const stats = {
      collectedSamples,
      label,
      uptime: Math.round((Date.now() - startTime) / 1000),
      clients: wss.clients.size,
      lastSample: collectedSamples > 0 ? lastSampleData : null,
    };
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify(stats));
    return;
  }

  // Strip query string from URL before resolving to a file path
  let urlPath = req.url.split('?')[0];
  let filePath = path.join(DIR, urlPath === '/' ? 'index.html' : urlPath);
  let ext = path.extname(filePath);

  fs.readFile(filePath, (err, data) => {
    if (err) {
      res.writeHead(404);
      res.end('Not found: ' + req.url);
      return;
    }
    res.writeHead(200, { 'Content-Type': MIME[ext] || 'application/octet-stream' });
    res.end(data);
  });
});

const wss = new WebSocketServer({ server });

function broadcast(msg) {
  const payload = JSON.stringify(msg);
  for (const client of wss.clients) {
    if (client.readyState === 1) client.send(payload);
  }
}

function appendCsv(p) {
  const row = [
    Date.now(),
    label,
    p.buildId || '',
    p.cpuMs, p.syncMs, p.rdpMs, p.renderMs, p.totalMs,
    p.instrPerSec, p.n64Fps, p.browserFps,
  ].map(v => (typeof v === 'string' && v.includes(',')) ? `"${v}"` : v).join(',');
  fs.appendFileSync(CSV_PATH, row + '\n');
}

wss.on('connection', (ws) => {
  console.log('[ws] client connected');
  ws.on('message', (data) => {
    try {
      const msg = JSON.parse(data.toString());
      if (msg.type === 'profile') {
        collectedSamples++;
        lastSampleData = msg.data;
        samplesForSummary.push(msg.data);
        if (collecting) appendCsv(msg.data);
        const p = msg.data;
        const elapsed = ((Date.now() - startTime) / 1000).toFixed(0);
        console.log(`\n[${label} | sample ${collectedSamples} | server ${elapsed}s | client uptime ${p.uptimeSec || '?'}s | build: ${p.buildId || '?'}]`);
        console.log(`CPU: ${p.cpuMs}ms  Sync: ${p.syncMs}ms  RDP: ${p.rdpMs}ms  Total: ${p.totalMs}ms`);
        console.log(`MIPS: ${p.instrPerSec}M  N64fps: ${p.n64Fps}  Browser: ${p.browserFps}`);

        if (maxSamples > 0 && collectedSamples >= maxSamples) {
          console.log(`\n[done] collected ${collectedSamples} samples, exiting`);
          writeSummary('sample-limit');
          setTimeout(() => process.exit(0), 200);
        }
      } else if (msg.type === 'log') {
        console.log(`[client] ${msg.data}`);
      }
    } catch (e) {
      console.error('[ws] bad message:', e.message);
    }
  });
  ws.on('close', () => console.log('[ws] client disconnected'));
});

// Time-based exit
if (durationSec > 0) {
  setTimeout(() => {
    console.log(`\n[done] ${durationSec}s elapsed, collected ${collectedSamples} samples, exiting`);
    writeSummary('time-limit');
    setTimeout(() => process.exit(0), 100);
  }, durationSec * 1000);
}

// Also write summary on Ctrl+C
process.on('SIGINT', () => {
  console.log('\n[interrupted]');
  writeSummary('interrupted');
  process.exit(0);
});

server.listen(PORT, () => {
  console.log(`[server] http://localhost:${PORT}`);
  console.log(`[server] SharedArrayBuffer enabled (COOP/COEP)`);
  console.log(`[server] WebSocket on same port`);
  console.log(`[server] label="${label}"`);
  if (maxSamples > 0) console.log(`[server] will exit after ${maxSamples} samples`);
  if (durationSec > 0) console.log(`[server] will exit after ${durationSec}s`);
  if (collecting) console.log(`[server] writing samples to ${path.basename(CSV_PATH)}`);
});
