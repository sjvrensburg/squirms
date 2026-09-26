// Real WASM + two separate Chrome processes. Run after `cmake --build build --target stage`.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { mkdtemp, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { createServer } from '../server/server.mjs';

const delay = ms => new Promise(r => setTimeout(r, ms));
async function until(fn, label, timeout = 30000) {
  const start = Date.now();
  while (Date.now() - start < timeout) { if (await fn()) return; await delay(100); }
  throw new Error(`Timed out: ${label}`);
}
async function chrome(t, width = 1280, height = 800) {
  const dir = await mkdtemp(join(tmpdir(), 'squirms-browser-'));
  const proc = spawn(process.env.CHROME || 'google-chrome', [
    ...(process.env.HEADFUL ? ['--ozone-platform=x11'] : ['--headless=new']), '--no-sandbox', '--disable-dev-shm-usage', '--enable-unsafe-swiftshader',
    '--use-gl=angle', '--use-angle=swiftshader', '--remote-debugging-port=0',
    '--disable-background-timer-throttling', '--disable-renderer-backgrounding',
    '--autoplay-policy=no-user-gesture-required', `--window-size=${width},${height}`, `--user-data-dir=${dir}`, 'about:blank'
  ], { stdio: ['ignore', 'ignore', 'pipe'] });
  let stderr = '';
  proc.stderr.on('data', chunk => { stderr += chunk; });
  t.after(async () => { proc.kill(); await delay(300); await rm(dir, { recursive: true, force: true, maxRetries: 3 }); });
  await until(() => stderr.includes('DevTools listening on '), 'Chrome launch').catch(error => { throw new Error(error.message + '\n' + stderr.slice(-4000)); });
  const address = /DevTools listening on (ws:\/\/[^\s]+)/.exec(stderr)[1];
  const endpoint = new URL(address);
  let target;
  await until(async () => {
    const targets = await (await fetch(`http://${endpoint.host}/json/list`)).json();
    target = targets.find(x => x.type === 'page'); return !!target;
  }, 'Chrome page target');
  const ws = new WebSocket(target.webSocketDebuggerUrl);
  await new Promise(resolve => ws.addEventListener('open', resolve, { once: true }));
  let id = 0; const pending = new Map(), errors = [];
  ws.addEventListener('message', event => {
    const msg = JSON.parse(event.data);
    if (msg.id) { const entry = pending.get(msg.id); pending.delete(msg.id); msg.error ? entry.reject(new Error(JSON.stringify(msg.error))) : entry.resolve(msg.result); }
    if (msg.method === 'Runtime.exceptionThrown') errors.push(msg.params.exceptionDetails);
  });
  const call = (method, params = {}) => new Promise((resolve, reject) => { pending.set(++id, { resolve, reject }); ws.send(JSON.stringify({ id, method, params })); });
  const evaluate = async expression => {
    const res = await call('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true });
    if (res.exceptionDetails) throw new Error(JSON.stringify(res.exceptionDetails));
    return res.result.value;
  };
  await call('Runtime.enable'); await call('Page.enable');
  await call('Page.addScriptToEvaluateOnNewDocument', { source: `
    window.audioPeak = 0;
    const connect = AudioNode.prototype.connect;
    AudioNode.prototype.connect = function(...args) {
      if (args[0] instanceof AudioDestinationNode) {
        const analyser = this.context.createAnalyser(); connect.call(this, analyser);
        const samples = new Float32Array(analyser.fftSize);
        setInterval(()=>{analyser.getFloatTimeDomainData(samples);for(const v of samples) window.audioPeak=Math.max(window.audioPeak,Math.abs(v));},50);
      }
      return connect.apply(this,args);
    };` });
  const key = async (code, keyCode, ms = 220) => {
    await evaluate(`document.activeElement.dispatchEvent(new KeyboardEvent('keydown',{code:${JSON.stringify(code)},keyCode:${keyCode},which:${keyCode},bubbles:true}))`);
    await delay(ms);
    await evaluate(`document.activeElement.dispatchEvent(new KeyboardEvent('keyup',{code:${JSON.stringify(code)},keyCode:${keyCode},which:${keyCode},bubbles:true}))`);
    await delay(180);
  };
  const mouse = async (x, y, target = 'canvas') => {
    await evaluate(`document.getElementById('${target}').dispatchEvent(new MouseEvent('mousemove',{clientX:${x},clientY:${y},bubbles:true}))`);
    await delay(100);
    await evaluate(`document.getElementById('${target}').dispatchEvent(new MouseEvent('mousedown',{clientX:${x},clientY:${y},button:0,bubbles:true}))`);
    await delay(220);
    await evaluate(`document.getElementById('${target}').dispatchEvent(new MouseEvent('mouseup',{clientX:${x},clientY:${y},button:0,bubbles:true}))`);
    await delay(200);
  };
  const screenshot = async name => { const shot = await call('Page.captureScreenshot'); await writeFile(join(tmpdir(), name), Buffer.from(shot.data, 'base64')); };
  const load = async url => {
    await call('Page.navigate', { url });
    await until(() => evaluate(`document.getElementById('loading')?.hidden && typeof Module._squirms_in_match === 'function'`), 'WASM loaded', 60000);
    await evaluate(`window.observed = null; window.sounds = []; const oldState = SquirmsOnline.state;
      SquirmsOnline.state = (team,turn,phase,time) => { window.observed={team,turn,phase,time}; oldState(team,turn,phase,time); };
      const oldSound = SquirmsOnline.sound; SquirmsOnline.sound=(...args)=>{window.sounds.push(args);oldSound(...args);};`);
    await delay(500);
  };
  return { call, evaluate, key, mouse, screenshot, load, errors };
}

test('HTML controls retain native Backspace and Tab behavior', { timeout: 60000 }, async t => {
  const server = createServer({ iceServers: [] });
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  t.after(() => server.close());
  const page = await chrome(t);
  await page.load(`http://127.0.0.1:${server.address().port}/`);
  await page.evaluate(`document.getElementById('online-open').click();
    const room=document.getElementById('room'); room.value='abc'; room.focus(); room.setSelectionRange(3,3);`);
  // Use trusted CDP events for native editing/focus defaults. Untrusted DOM
  // events cannot delete text or move focus even when preventDefault is absent.
  const nativeKey = async (key, code, keyCode, modifiers = 0) => {
    for (const type of ['keyDown', 'keyUp'])
      await page.call('Input.dispatchKeyEvent', { type, key, code, windowsVirtualKeyCode: keyCode, modifiers });
  };
  await nativeKey('Backspace', 'Backspace', 8);
  assert.equal(await page.evaluate(`document.getElementById('room').value`), 'ab');
  await nativeKey('Tab', 'Tab', 9);
  assert.equal(await page.evaluate('document.activeElement.id'), 'join');
  await nativeKey('Tab', 'Tab', 9, 8); // Shift+Tab
  assert.equal(await page.evaluate('document.activeElement.id'), 'room');
  // Printable UI characters must not reach GLFW's menu seed character queue.
  assert.equal(await page.evaluate(`(() => {
    let reachedGame=false;
    const listener=()=>{reachedGame=true;};
    window.addEventListener('keypress',listener,true);
    const event=new KeyboardEvent('keypress',{key:'4',charCode:52,keyCode:52,bubbles:true,cancelable:true});
    document.activeElement.dispatchEvent(event);
    window.removeEventListener('keypress',listener,true);
    return !reachedGame && !event.defaultPrevented;
  })()`), true);
  assert.deepEqual(page.errors, []);
});

test('two browsers connect, stream video, enforce turns, play audio, fire and handle disconnect', { timeout: 180000 }, async t => {
  const server = createServer({ iceServers: [] });
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  t.after(() => server.close());
  const url = `http://127.0.0.1:${server.address().port}/`;
  const host = await chrome(t), guest = await chrome(t, 1000, 700);
  await Promise.all([host.load(url), guest.load(url)]);
  await host.screenshot('squirms-menu.png');
  // A known even seed starts with the host's team.
  await host.evaluate(`for(const n of '42') window.dispatchEvent(new KeyboardEvent('keypress',{charCode:n.charCodeAt(0),keyCode:n.charCodeAt(0),which:n.charCodeAt(0),bubbles:true}));`);
  await delay(300);
  await host.evaluate(`document.getElementById('online-open').click(); document.getElementById('host').click();`);
  await until(() => host.evaluate(`!document.getElementById('invite-box').hidden`), 'invite generated');
  const invite = await host.evaluate(`document.getElementById('invite').value`);
  await host.screenshot('squirms-lobby.png');
  await guest.evaluate(`document.getElementById('online-open').click(); document.getElementById('room').value=${JSON.stringify(invite)};document.getElementById('join').click();`);
  await until(() => guest.evaluate(`!document.getElementById('remote').hidden && document.getElementById('remote').videoWidth > 0`), 'guest receives video', 60000);
  await until(() => host.evaluate(`window.observed?.phase===1 && window.observed.team===0`), 'host aiming');
  assert.equal(await guest.evaluate('document.activeElement.id'), 'remote');
  await host.screenshot('squirms-host.png'); await guest.screenshot('squirms-guest.png');
  // The guest cannot fire or open the host's weapon panel during team 1's turn.
  await guest.key('Space', 32, 500);
  assert.equal(await host.evaluate('window.observed.phase'), 1);
  assert.equal(await host.evaluate('window.sounds.some(s=>s[0]===2)'), false);
  await host.key('ArrowUp', 38, 350);
  await host.key('Space', 32, 550);
  await until(() => host.evaluate('window.sounds.some(s=>s[0]===2)'), 'host fires bazooka');
  // Guest audio goes through the actual WASM sound export.
  await guest.evaluate(`window.remoteSounds=0; const oldRemoteSound=Module._squirms_sound;Module._squirms_sound=(...a)=>{window.remoteSounds++;oldRemoteSound(...a);};`);
  await until(() => host.evaluate('window.observed?.team===1 && window.observed.phase===1'), 'guest turn', 60000);
  await until(() => guest.evaluate(`document.getElementById('connection').textContent.includes('YOUR TURN')`), 'guest turn badge');
  const fireCount = await host.evaluate('window.sounds.filter(s=>s[0]===2).length');
  await host.key('Space', 32, 350);
  assert.equal(await host.evaluate('window.sounds.filter(s=>s[0]===2).length'), fireCount);
  // Record actual incoming controls on the host, including heartbeat packets.
  await host.evaluate(`window.receivedInputs=[]; const oldInput=Module._squirms_input;
    Module._squirms_input=(...args)=>{window.receivedInputs.push(args); oldInput(...args);};`);
  await guest.evaluate(`document.activeElement.dispatchEvent(new KeyboardEvent('keydown',
    {key:'ArrowRight',code:'ArrowRight',keyCode:39,bubbles:true,cancelable:true}));`);
  await until(() => host.evaluate('window.receivedInputs.some(a=>(a[1]&2)!==0)'), 'guest walking input');
  await host.evaluate('window.receivedInputs=[]');
  await guest.evaluate(`const sound=document.getElementById('audio-open'); sound.focus(); sound.click();`);
  await until(() => host.evaluate('window.receivedInputs.some(a=>a[1]===0)'), 'UI focus releases guest controls', 3000);
  await guest.evaluate(`document.activeElement.dispatchEvent(new KeyboardEvent('keyup',
    {key:'ArrowRight',code:'ArrowRight',keyCode:39,bubbles:true,cancelable:true}));`);
  await host.evaluate('window.receivedInputs=[]');
  await delay(350);
  const neutral = await host.evaluate('window.receivedInputs');
  assert.ok(neutral.length >= 2, 'heartbeats continue while editing UI');
  assert.ok(neutral.every(a => a.slice(1,5).every(value => value === 0)), 'UI heartbeats stay neutral');
  await guest.evaluate(`document.getElementById('audio-open').click();`);
  assert.equal(await guest.evaluate('document.activeElement.id'), 'remote');
  await host.evaluate('window.receivedInputs=[]');
  await delay(250);
  assert.ok(await host.evaluate('window.receivedInputs.length>0 && window.receivedInputs.every(a=>a[1]===0)'),
    'returning to the game does not restore a released key');
  // Guest opens the weapon panel and selects grenade using letterboxed coordinates.
  await guest.key('Tab', 9);
  const dims = await host.evaluate('({w:innerWidth,h:innerHeight})');
  const point = await guest.evaluate(`(()=>{const v=document.getElementById('remote'),r=v.getBoundingClientRect();const ratio=v.videoWidth/v.videoHeight;const w=Math.min(r.width,r.height*ratio),h=w/ratio;return {x:r.left+(r.width-w)/2+(${dims.w}-185)/${dims.w}*w,y:r.top+(r.height-h)/2+(${dims.h}/2-139)/${dims.h}*h};})()`);
  await guest.mouse(point.x, point.y, 'remote');
  await host.screenshot('squirms-remote-selection.png');
  await guest.key('Digit1', 49);
  await guest.key('ArrowUp', 38, 350);
  await guest.key('Space', 32, 500);
  await host.screenshot('squirms-remote-fire.png');
  await until(() => host.evaluate('window.sounds.some(s=>s[0]===3)'), 'guest throws grenade');
  await until(() => guest.evaluate('window.remoteSounds > 0'), 'guest sound events');
  await host.screenshot('squirms-online-action.png');
  assert.ok(await host.evaluate('window.audioPeak > 0.001'));
  assert.ok(await guest.evaluate('window.audioPeak > 0.001'));
  await guest.evaluate(`document.getElementById('online-open').click();document.getElementById('leave').click();`);
  await until(() => host.evaluate(`document.getElementById('lobby-status').textContent.includes('disconnected')`), 'host notified of disconnect');
  assert.equal(await host.evaluate('Module._squirms_in_match()'), 1);
  await host.evaluate(`document.getElementById('leave').click();document.getElementById('lobby-close').click();`);
  await until(() => host.evaluate('!Module._squirms_in_match()'), 'return to local menu');
  assert.deepEqual(host.errors, []); assert.deepEqual(guest.errors, []);
});


test('local weapons: all fourteen actions, sound mix and foreground timing', { timeout: 240000 }, async t => {
  const server = createServer({ iceServers: [] });
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  t.after(() => server.close());
  const page = await chrome(t);
  await page.load(`http://127.0.0.1:${server.address().port}/`);
  await page.key('ArrowDown', 40); await page.key('ArrowRight', 39); // hot-seat
  await page.evaluate(`for(const n of '42') window.dispatchEvent(new KeyboardEvent('keypress',{charCode:n.charCodeAt(0),keyCode:n.charCodeAt(0),which:n.charCodeAt(0),bubbles:true}));`);
  await delay(250);
  const sounds = [2,3,3,3,3,21,24,15,11,3,3,25,16,null];
  const names = ['Bazooka','Grenade','Cluster Bomb','Banana Bomb','Holy Hand Grenade','Dynamite','Mine','Sheep','Shotgun','Baseball Bat','Ninja Rope','Air Strike','Teleport','Skip Go'];
  const dims = await page.evaluate('({w:innerWidth,h:innerHeight})');
  for (let weapon = 0; weapon < 14; ++weapon) {
    await page.key('Enter', 13);
    await until(() => page.evaluate('window.observed?.phase===1 && Module._squirms_in_match()'), 'local aiming');
    // One real foreground second should remain approximately one simulation second.
    if (weapon === 0 && process.env.HEADFUL) {
      assert.equal(await page.evaluate('document.visibilityState'), 'visible');
      const before = await page.evaluate('window.observed.time');
      await delay(1000);
      const elapsed = (await page.evaluate('window.observed.time')) - before;
      assert.ok(elapsed > 0.8 && elapsed < 1.3, 'one wall-clock second advances about one simulation second');
    }
    await page.key('Tab', 9);
    await page.mouse(dims.w - 245 + (weapon % 4) * 60, dims.h / 2 - 139 + Math.floor(weapon / 4) * 60);
    await page.evaluate('window.sounds=[]');
    if (weapon <= 4) await page.key('ArrowUp', 38, 300);
    if (weapon === 11 || weapon === 12) await page.mouse(dims.w / 2, 180);
    else await page.key('Space', 32, weapon <= 4 ? 400 : 220);
    await page.screenshot('squirms-local-action.png');
    if (sounds[weapon] !== null)
      await until(() => page.evaluate(`window.sounds.some(s=>s[0]===${sounds[weapon]})`), names[weapon] + ' activated', 5000);
    else await until(() => page.evaluate('window.observed.phase!==1'), 'skip ends turn');
    if (weapon === 7) { await page.key('Space', 32); await until(() => page.evaluate('window.sounds.some(s=>s[0]===0)'), 'sheep detonation'); }
    if (weapon === 8) { await page.key('Space', 32); assert.equal(await page.evaluate('window.sounds.filter(s=>s[0]===11).length'), 2); }
    if (weapon === 10) { await page.key('ArrowLeft', 37); await page.key('ArrowUp', 38); await page.key('Enter', 13); }
    console.log('Verified', names[weapon]);
    await page.key('Escape', 27); await page.key('KeyY', 89);
    await until(() => page.evaluate('!Module._squirms_in_match()'), 'local exit');
  }
  assert.ok(await page.evaluate('window.audioPeak > 0.001'));
  await page.evaluate(`document.getElementById('muted').checked=true;document.getElementById('muted').dispatchEvent(new Event('input'));`);
  await delay(300); await page.evaluate('window.audioPeak=0'); await delay(700);
  assert.ok(await page.evaluate('window.audioPeak < 0.0001'), 'mute silences current voices');
  await page.evaluate(`document.getElementById('muted').checked=false;document.getElementById('muted').dispatchEvent(new Event('input'));`);
  await until(() => page.evaluate('window.audioPeak > 0.001'), 'unmute resumes audio');
  assert.deepEqual(page.errors, []);
});
