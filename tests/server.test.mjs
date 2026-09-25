import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createServer } from '../server/server.mjs';

test('room lifecycle, authorization, competing guests, validation and static isolation', async t => {
  const server = createServer({ iceServers: [] });
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  t.after(() => server.close());
  const base = `http://127.0.0.1:${server.address().port}`;
  const call = async (path, method = 'GET', body, secret, headers = {}) => {
    const res = await fetch(base + path, { method, headers: { 'Content-Type': 'application/json',
      ...(secret ? { Authorization: `Bearer ${secret}` } : {}), ...headers }, body: body && JSON.stringify(body) });
    return { status: res.status, body: await res.json() };
  };
  assert.deepEqual((await call('/api/config')).body, { iceServers: [] });
  assert.equal((await call('/api/rooms', 'POST', { offer: {} })).status, 400);
  assert.equal((await call('/api/rooms', 'POST', null, null, { Origin: 'https://evil.example' })).status, 403);
  const offer = { type: 'offer', sdp: 'v=0\r\n' };
  const created = await call('/api/rooms', 'POST', { offer });
  assert.equal(created.status, 201);
  const { id, secret } = created.body;
  const path = `/api/rooms/${id}`;
  assert.equal(id.length, 32);
  assert.deepEqual((await call(path)).body, { offer });
  assert.equal((await call(path + '/answer')).status, 403);
  assert.equal((await call(path, 'DELETE', null, 'wrong')).status, 403);
  assert.deepEqual((await call(path + '/answer', 'GET', null, secret)).body, { answer: null });
  const answer = { type: 'answer', sdp: 'v=0\r\n' };
  const attempts = await Promise.all([call(path + '/answer', 'POST', { answer }), call(path + '/answer', 'POST', { answer })]);
  assert.deepEqual(attempts.map(x => x.status).sort(), [200, 409]);
  assert.equal((await call(path)).status, 409);
  assert.deepEqual((await call(path + '/answer', 'GET', null, secret)).body, { answer });
  assert.equal((await call(path, 'DELETE', null, secret)).status, 200);
  assert.equal((await call(path)).status, 404);
  assert.equal((await call('/api/rooms', 'POST', { offer: { ...offer, sdp: 'v=0' + 'a'.repeat(70000) } })).status, 413);
  assert.equal((await fetch(base + '/server/server.mjs')).status, 404);
  assert.equal((await fetch(base + '/%2e%2e/.git/config')).status, 404);
});
