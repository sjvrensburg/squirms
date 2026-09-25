// Dependency-free static server + short-lived WebRTC signaling mailboxes.
// Gameplay/video travel peer-to-peer, never through this process.
import http from 'node:http';
import { randomBytes, timingSafeEqual } from 'node:crypto';
import { readFile } from 'node:fs/promises';
import { resolve, extname } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = resolve(fileURLToPath(new URL('../dist/', import.meta.url)));
const ttl = 10 * 60 * 1000;
const mime = { '.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css', '.wasm': 'application/wasm' };
const fail = (status, message) => Object.assign(new Error(message), { status });
const token = () => randomBytes(16).toString('hex');

export function createServer({ staticRoot = root, iceServers = [{ urls: 'stun:stun.l.google.com:19302' }] } = {}) {
  const rooms = new Map();
  const creates = new Map();
  const server = http.createServer(async (req, res) => {
    const json = (status, body) => {
      res.writeHead(status, { 'Content-Type': 'application/json', 'Cache-Control': 'no-store',
        'X-Content-Type-Options': 'nosniff', 'Referrer-Policy': 'no-referrer' });
      res.end(JSON.stringify(body));
    };
    try {
      const url = new URL(req.url, 'http://localhost');
      const now = Date.now();
      for (const [id, room] of rooms) if (room.expires < now) rooms.delete(id);
      for (const [ip, entry] of creates) if (entry.until < now) creates.delete(ip);
      if (url.pathname.startsWith('/api/')) {
        if (req.headers.origin && new URL(req.headers.origin).host !== req.headers.host)
          throw fail(403, 'Use the same server for the game and its rooms.');
        const body = async () => {
          if (!req.headers['content-type']?.startsWith('application/json')) throw fail(415, 'Expected JSON.');
          let chunks = [], size = 0;
          for await (const chunk of req) {
            size += chunk.length;
            if (size > 64 * 1024) throw fail(413, 'Message too large.');
            chunks.push(chunk);
          }
          try { return JSON.parse(Buffer.concat(chunks).toString()); }
          catch { throw fail(400, 'Invalid JSON.'); }
        };
        const description = (value, type) => {
          if (!value || value.type !== type || typeof value.sdp !== 'string' ||
              !value.sdp.startsWith('v=0') || value.sdp.length > 60000)
            throw fail(400, 'Invalid connection description.');
          return { type, sdp: value.sdp };
        };
        if (url.pathname === '/api/config' && req.method === 'GET') return json(200, { iceServers });
        if (url.pathname === '/api/rooms' && req.method === 'POST') {
          const ip = req.socket.remoteAddress;
          const entry = creates.get(ip) || { count: 0, until: now + 60000 };
          if (++entry.count > 12 || rooms.size >= 200) throw fail(429, 'Too many rooms. Please try again shortly.');
          creates.set(ip, entry);
          const offer = description((await body()).offer, 'offer');
          const id = token(), secret = token();
          rooms.set(id, { offer, secret, answer: null, expires: now + ttl });
          return json(201, { id, secret, expires: now + ttl });
        }
        const match = /^\/api\/rooms\/([a-f0-9]{32})(\/answer)?$/.exec(url.pathname);
        if (!match) throw fail(404, 'Unknown room endpoint.');
        const room = rooms.get(match[1]);
        if (!room) throw fail(404, 'This room has expired or closed. Ask the host for a new link.');
        const authorize = () => {
          const supplied = Buffer.from(req.headers.authorization || '');
          const expected = Buffer.from(`Bearer ${room.secret}`);
          if (supplied.length !== expected.length || !timingSafeEqual(supplied, expected))
            throw fail(403, 'Only the host can manage this room.');
        };
        if (req.method === 'DELETE' && !match[2]) {
          authorize(); rooms.delete(match[1]); return json(200, { closed: true });
        }
        if (req.method === 'GET' && !match[2]) {
          if (room.answer) throw fail(409, 'This room already has a guest.');
          return json(200, { offer: room.offer });
        }
        if (req.method === 'GET' && match[2]) { authorize(); return json(200, { answer: room.answer }); }
        if (req.method === 'POST' && match[2]) {
          const answer = description((await body()).answer, 'answer');
          if (room.answer) throw fail(409, 'This room already has a guest.');
          room.answer = answer;
          return json(200, { accepted: true });
        }
        throw fail(405, 'Method not allowed.');
      }
      if (!['GET', 'HEAD'].includes(req.method)) throw fail(405, 'Method not allowed.');
      // Explicit allowlist: never serve sources, credentials, or files outside dist.
      const path = url.pathname === '/' ? '/index.html' : url.pathname;
      if (!/^\/(index\.html|squirms_wasm\.(js|wasm)|web\/(online\.js|shell\.css))$/.test(path))
        throw fail(404, 'File not found.');
      const data = await readFile(resolve(staticRoot, '.' + path));
      res.writeHead(200, { 'Content-Type': mime[extname(path)], 'Cache-Control': 'no-cache',
        'X-Content-Type-Options': 'nosniff', 'Referrer-Policy': 'no-referrer' });
      res.end(req.method === 'HEAD' ? undefined : data);
    } catch (err) {
      if (!res.headersSent) json(err.status || (err.code === 'ENOENT' ? 404 : 500),
        { error: err.status ? err.message : 'Unable to serve this request. Check that dist has been built.' });
      else res.end();
    }
  });
  server.requestTimeout = 15000;
  server.headersTimeout = 10000;
  return server;
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const options = process.env.ICE_SERVERS ? { iceServers: JSON.parse(process.env.ICE_SERVERS) } : {};
  const port = Number(process.env.PORT || 8080);
  const host = process.env.HOST || '127.0.0.1';
  createServer(options).listen(port, host, () => console.log(`Squirms: http://${host}:${port}`));
}
