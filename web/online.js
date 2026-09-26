/* WebRTC transport and page controls. Physics, weapons, rendering and audio
 * synthesis stay in C++. Only the host simulates an online match. */
(() => {
  'use strict';
  const $ = id => document.getElementById(id);
  const canvas = $('canvas'), video = $('remote'), lobby = $('lobby');
  let loaded = false, session = null, latestState = null;
  const keyBits = new Map([
    ['ArrowLeft', 0], ['KeyA', 0], ['ArrowRight', 1], ['KeyD', 1],
    ['ArrowUp', 2], ['KeyW', 2], ['ArrowDown', 3], ['KeyS', 3],
    ['Space', 4], ['Enter', 5], ['Backspace', 6], ['Tab', 7], ['KeyH', 8],
    ['Digit1', 9], ['Digit2', 10], ['Digit3', 11], ['Digit4', 12], ['Digit5', 13]
  ]);
  const heldCodes = new Set();
  let keys = 0, pressed = 0, buttons = 0, clicks = 0, x = .5, y = .5, wheel = 0;
  const status = text => { $('lobby-status').textContent = text; };
  const badge = text => { $('connection').textContent = text; };
  function clearInput() { heldCodes.clear(); keys = pressed = buttons = clicks = wheel = 0; }
  function send(message) {
    const dc = session?.channel;
    if (dc?.readyState === 'open' && dc.bufferedAmount < 65536) dc.send(JSON.stringify(message));
  }
  function controlsEnabled() {
    return session?.role === 2 && session.live && session.state?.team === 1 &&
      !lobby.open && !document.hidden && !uiTarget(document.activeElement);
  }
  function sendInput() {
    if (session?.role !== 2) return;
    send({ type: 'input', turn: session.state?.turn ?? -1, keys: controlsEnabled() ? keys : 0,
      pressed: controlsEnabled() ? pressed : 0, buttons: controlsEnabled() ? buttons : 0,
      clicks: controlsEnabled() ? clicks : 0, x, y, wheel: controlsEnabled() ? wheel : 0 });
    pressed = clicks = wheel = 0;
  }
  const uiTarget = target => target instanceof Element && !!target.closest('button,input,dialog,#audio-panel');
  function blockUI() {
    const blocked = lobby.open || (session && !session.started) || uiTarget(document.activeElement);
    if (blocked) { clearInput(); sendInput(); }
    if (loaded) Module._squirms_ui(blocked ? 1 : 0);
  }
  // Installed before GLFW's window capture listeners. Keep UI presses and
  // characters out of the game without cancelling native editing or focus.
  // Let keyup reach GLFW so local keys held before entering the UI can release.
  for (const type of ['keydown', 'keypress']) window.addEventListener(type, event => {
    if (lobby.open || uiTarget(event.target)) event.stopImmediatePropagation();
  }, true);
  function focusGame() { (session?.role === 2 && session.started ? video : canvas).focus(); blockUI(); }
  function openLobby() { clearInput(); lobby.showModal(); blockUI(); }
  $('online-open').onclick = openLobby;
  $('lobby-close').onclick = () => lobby.close();
  lobby.addEventListener('close', () => { focusGame(); });
  document.addEventListener('focusin', blockUI);
  document.addEventListener('focusout', () => queueMicrotask(blockUI));
  $('audio-open').onclick = () => {
    const panel = $('audio-panel'); panel.hidden = !panel.hidden;
    $('audio-open').setAttribute('aria-expanded', String(!panel.hidden));
    if (panel.hidden) focusGame();
  };
  $('fullscreen').onclick = async () => {
    try {
      if (document.fullscreenElement) await document.exitFullscreen();
      else await document.documentElement.requestFullscreen();
    } catch { badge('Fullscreen unavailable'); }
    focusGame();
  };
  function audioSettings() {
    const settings = { effects: +$('effects').value, music: +$('music').value, muted: $('muted').checked };
    if (loaded) Module._squirms_audio(settings.effects / 100, settings.music / 100, Number(settings.muted));
    try { localStorage.setItem('squirms-audio', JSON.stringify(settings)); } catch { /* private browsing */ }
  }
  try {
    const saved = JSON.parse(localStorage.getItem('squirms-audio'));
    if (saved) {
      for (const id of ['effects', 'music']) if (Number.isFinite(saved[id])) $(id).value = Math.max(0, Math.min(100, saved[id]));
      $('muted').checked = saved.muted === true;
    }
  } catch { /* preferences are optional */ }
  for (const id of ['effects', 'music', 'muted']) $(id).addEventListener('input', audioSettings);

  async function api(path, { method = 'GET', body, secret, signal } = {}) {
    const response = await fetch(path, { method, signal, headers: {
      ...(body ? { 'Content-Type': 'application/json' } : {}),
      ...(secret ? { Authorization: `Bearer ${secret}` } : {})
    }, body: body ? JSON.stringify(body) : undefined });
    let data;
    try { data = await response.json(); }
    catch { throw new Error('Online rooms need the Squirms server. Start it with node server/server.mjs.'); }
    if (!response.ok) throw new Error(data.error || 'The room server could not be reached.');
    return data;
  }
  function roomID(value) {
    const raw = value.trim();
    if (/^[a-f0-9]{32}$/.test(raw)) return raw;
    try {
      const parsed = new URL(raw);
      const id = new URLSearchParams(parsed.hash.slice(1)).get('room');
      if (/^[a-f0-9]{32}$/.test(id)) return id;
    } catch { /* report below */ }
    throw new Error('Paste a complete invite link or its 32-character room code.');
  }
  function closeSession(message = 'You left the online session.') {
    const old = session;
    session = null;
    clearInput(); latestState = null;
    if (old) {
      clearTimeout(old.timeout);
      old.abort.abort();
      old.channel?.close(); old.pc?.close();
      old.stream?.getTracks().forEach(track => track.stop());
      if (old.id && old.secret) api(`/api/rooms/${old.id}`, { method: 'DELETE', secret: old.secret }).catch(() => {});
      if (old.started) Module._squirms_online(0, 0);
    }
    video.srcObject = null; video.hidden = true; canvas.style.visibility = '';
    $('host').disabled = $('join').disabled = false;
    $('leave').hidden = $('invite-box').hidden = true;
    badge('LOCAL PLAY'); status(message); blockUI();
  }
  $('leave').onclick = () => closeSession();
  $('copy').onclick = async () => {
    try { await navigator.clipboard.writeText($('invite').value); status('Invite copied. Send it to your friend.'); }
    catch { $('invite').select(); status('Select and copy the invite link above.'); }
  };
  function failSession(s, error) {
    if (session !== s || error.name === 'AbortError') return;
    closeSession(error.message);
    if (!lobby.open) openLobby();
  }
  async function gather(s) {
    // Wait for all candidates so the signaling service needs just offer/answer.
    if (s.pc.iceGatheringState === 'complete') return;
    await new Promise((resolve, reject) => {
      const timer = setTimeout(() => finish(new Error('Connection setup timed out. Check your network or relay settings.')), 12000);
      const change = () => { if (s.pc.iceGatheringState === 'complete') finish(); };
      const abort = () => finish(new DOMException('Cancelled', 'AbortError'));
      function finish(error) {
        clearTimeout(timer);
        s.pc.removeEventListener('icegatheringstatechange', change);
        s.abort.signal.removeEventListener('abort', abort);
        error ? reject(error) : resolve();
      }
      s.pc.addEventListener('icegatheringstatechange', change);
      s.abort.signal.addEventListener('abort', abort, { once: true });
      if (s.abort.signal.aborted) abort();
    });
  }
  function attachChannel(s, dc) {
    s.channel = dc;
    dc.onopen = () => {
      if (session !== s) return;
      s.live = s.started = true; s.lastHeard = performance.now();
      clearTimeout(s.timeout);
      Module._squirms_online(s.role, 1);
      if (s.role === 2) { canvas.style.visibility = 'hidden'; video.hidden = false; video.play().catch(() => {}); }
      lobby.close(); focusGame();
      status('Connected. Use Play online to leave the session.');
      badge(s.role === 1 ? 'ONLINE · YOU ARE TEAM 1' : 'ONLINE · YOU ARE TEAM 2');
    };
    dc.onclose = () => {
      if (session !== s) return;
      if (!s.started) { failSession(s, new Error('The connection closed before the match started. Create a new room to retry.')); return; }
      s.live = false;
      Module._squirms_online(s.role, 0);
      badge('DISCONNECTED · MATCH PAUSED');
      status('Your friend disconnected. Leave this session and create a new room to play again.');
      if (!lobby.open) openLobby();
    };
    dc.onmessage = event => {
      if (session !== s || typeof event.data !== 'string' || event.data.length > 4096) return;
      let m;
      try { m = JSON.parse(event.data); } catch { return; }
      if (!m || typeof m !== 'object') return;
      s.lastHeard = performance.now();
      if (s.role === 1 && m.type === 'input') {
        const ints = ['turn', 'keys', 'pressed', 'buttons', 'clicks'];
        if (!ints.every(k => Number.isSafeInteger(m[k])) || !['x', 'y', 'wheel'].every(k => Number.isFinite(m[k]))) return;
        // Recheck the live game turn; data can arrive between rendered frames.
        if (latestState?.team !== 1 || m.turn !== latestState.turn) return;
        Module._squirms_input(m.turn, m.keys, m.pressed, m.buttons, m.clicks, m.x, m.y, m.wheel);
      } else if (s.role === 2 && m.type === 'state') {
        if (!Number.isInteger(m.team) || !Number.isInteger(m.turn) || !Number.isInteger(m.phase)) return;
        if (m.turn !== s.state?.turn) clearInput();
        s.state = m;
        badge(m.paused ? 'HOST PAUSED · KEEP BOTH TABS VISIBLE' :
          (m.phase === 5 ? 'MATCH COMPLETE' : m.team === 1 ? 'YOUR TURN · TEAM 2' : 'HOST’S TURN · TEAM 1'));
      } else if (s.role === 2 && m.type === 'sound') {
        if (Number.isInteger(m.id) && Number.isFinite(m.volume) && Number.isFinite(m.pitch))
          Module._squirms_sound(m.id, m.volume, m.pitch);
      } else if (m.type === 'ended' && s.role === 2) {
        closeSession('The host ended the match. Create or join a new room to play again.'); openLobby();
      }
    };
  }
  async function begin(role) {
    if (!loaded || session) return null;
    if (Module._squirms_in_match()) throw new Error('Return to the game menu before creating or joining an online match.');
    if (!window.RTCPeerConnection || !canvas.captureStream) throw new Error('Online play needs a browser with WebRTC and canvas streaming support.');
    const s = { role, abort: new AbortController(), started: false, live: false, lastHeard: performance.now() };
    session = s; clearInput(); blockUI();
    $('host').disabled = $('join').disabled = true; $('leave').hidden = false;
    status('Preparing a connection…');
    try {
      const config = await api('/api/config', { signal: s.abort.signal });
      if (session !== s) return null;
      s.pc = new RTCPeerConnection({ iceServers: config.iceServers });
      s.pc.onconnectionstatechange = () => {
        if (session === s && s.pc.connectionState === 'failed')
          failSession(s, new Error('The direct connection failed. Try another network, or configure a TURN relay on the server.'));
      };
      if (role === 1) {
        s.stream = canvas.captureStream(30);
        for (const track of s.stream.getTracks()) {
          track.contentHint = 'detail';
          s.pc.addTrack(track, s.stream);
        }
        attachChannel(s, s.pc.createDataChannel('squirms-v1', { ordered: true }));
      } else {
        s.pc.ondatachannel = event => attachChannel(s, event.channel);
        s.pc.ontrack = event => {
          if (session === s) { video.srcObject = event.streams[0] || new MediaStream([event.track]); video.play().catch(() => {}); }
        };
      }
      return s;
    } catch (error) { failSession(s, error); return null; }
  }
  function connectionDeadline(s) {
    if (s.started) return;
    s.timeout = setTimeout(() => failSession(s, new Error('Your friend could not connect. Try a new room or a TURN relay.')), 30000);
  }
  $('host').onclick = async () => {
    let s;
    try {
      s = await begin(1); if (!s) return;
      await s.pc.setLocalDescription(await s.pc.createOffer()); await gather(s);
      const room = await api('/api/rooms', { method: 'POST', body: { offer: s.pc.localDescription }, signal: s.abort.signal });
      if (session !== s) return;
      s.id = room.id; s.secret = room.secret;
      const link = new URL(location.href); link.hash = new URLSearchParams({ room: room.id }).toString();
      $('invite').value = link.href; $('invite-box').hidden = false;
      status('Room ready. Share the link; the match starts when your friend joins.');
      const poll = async () => {
        if (session !== s) return;
        try {
          const result = await api(`/api/rooms/${s.id}/answer`, { secret: s.secret, signal: s.abort.signal });
          if (session !== s) return;
          if (!result.answer) { s.timeout = setTimeout(poll, 1000); return; }
          await s.pc.setRemoteDescription(result.answer);
          status('Friend found. Connecting…'); connectionDeadline(s);
        } catch (error) { failSession(s, error); }
      };
      poll();
    } catch (error) { s ? failSession(s, error) : status(error.message); }
  };
  $('join').onclick = async () => {
    let s;
    try {
      const id = roomID($('room').value);
      s = await begin(2); if (!s) return;
      const room = await api(`/api/rooms/${id}`, { signal: s.abort.signal });
      await s.pc.setRemoteDescription(room.offer);
      await s.pc.setLocalDescription(await s.pc.createAnswer()); await gather(s);
      await api(`/api/rooms/${id}/answer`, { method: 'POST', body: { answer: s.pc.localDescription }, signal: s.abort.signal });
      if (session !== s) return;
      status('Connecting to your friend…'); connectionDeadline(s);
    } catch (error) { s ? failSession(s, error) : status(error.message); }
  };

  // Guest input is normalized to the actual video image, excluding letterboxing.
  function pointer(event) {
    const rect = video.getBoundingClientRect();
    const ratio = (video.videoWidth || canvas.width) / (video.videoHeight || canvas.height);
    const width = Math.min(rect.width, rect.height * ratio), height = width / ratio;
    x = Math.max(0, Math.min(1, (event.clientX - rect.left - (rect.width - width) / 2) / width));
    y = Math.max(0, Math.min(1, (event.clientY - rect.top - (rect.height - height) / 2) / height));
  }
  for (const type of ['keydown', 'keyup']) window.addEventListener(type, event => {
    if (session?.role !== 2) return;
    const bit = keyBits.get(event.code);
    if (bit === undefined) return;
    const inUI = uiTarget(event.target) || lobby.open;
    if (inUI && type === 'keydown') return;
    // Releases still update guest state when focus moved after keydown.
    if (!inUI) { event.preventDefault(); event.stopImmediatePropagation(); }
    if (type === 'keydown') {
      if (!heldCodes.has(event.code)) pressed |= 1 << bit;
      heldCodes.add(event.code);
    } else heldCodes.delete(event.code);
    keys = 0; for (const code of heldCodes) keys |= 1 << keyBits.get(code);
    sendInput();
  }, true);
  for (const type of ['mousedown', 'mouseup', 'mousemove']) window.addEventListener(type, event => {
    if (session?.role !== 2 || uiTarget(event.target) || lobby.open) return;
    if (type === 'mousedown') focusGame();
    pointer(event);
    // DOM middle/right are 1/2; raylib right/middle are 1/2 respectively.
    const bit = 1 << (event.button === 1 ? 2 : event.button === 2 ? 1 : 0);
    if (type === 'mousedown') { buttons |= bit; clicks |= bit; }
    if (type === 'mouseup') buttons &= ~bit;
    if (type !== 'mousemove') sendInput();
    event.preventDefault(); event.stopImmediatePropagation();
  }, true);
  video.addEventListener('contextmenu', event => event.preventDefault());
  video.addEventListener('wheel', event => { event.preventDefault(); pointer(event); wheel += Math.sign(-event.deltaY); sendInput(); }, { passive: false });
  const release = () => { clearInput(); sendInput(); };
  window.addEventListener('blur', release);
  document.addEventListener('visibilitychange', release);
  window.addEventListener('beforeunload', () => session?.pc?.close());

  setInterval(() => {
    const s = session;
    if (!s?.started || s.channel?.readyState !== 'open') return;
    const healthy = performance.now() - s.lastHeard < 2500;
    s.live = healthy;
    if (s.role === 1) {
      Module._squirms_online(1, Number(healthy && !document.hidden));
      send({ type: 'state', ...latestState, paused: !healthy || document.hidden });
      badge(!healthy ? 'CONNECTION LOST · MATCH PAUSED' : document.hidden ? 'MATCH PAUSED' :
        latestState?.team === 0 ? 'YOUR TURN · TEAM 1' : 'FRIEND’S TURN · TEAM 2');
    } else {
      sendInput();
      if (!healthy) badge('WAITING FOR HOST · MATCH PAUSED');
    }
  }, 100);

  window.SquirmsOnline = {
    ready() {
      loaded = true; $('loading').hidden = true; $('online-open').disabled = false;
      audioSettings(); canvas.focus();
      const invite = new URLSearchParams(location.hash.slice(1)).get('room');
      if (invite) { $('room').value = invite; openLobby(); status('Your friend invited you. Press Join when you’re ready.'); }
    },
    state(team, turn, phase, time) { latestState = { team, turn, phase, time }; },
    sound(id, volume, pitch) { if (session?.role === 1) send({ type: 'sound', id, volume, pitch }); },
    matchEnded() {
      if (session?.role === 1) { send({ type: 'ended' }); closeSession('Match finished. Create another room for a rematch.'); }
    }
  };
})();
