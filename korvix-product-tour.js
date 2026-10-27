/* Korvix product tour
   The 3D model is the real printed case (base + bezel from the print files, assembled), drawn with
   WebGL. The display shows the real screen recording of the firmware. Drag or use the buttons to turn
   it. Without WebGL the recording is shown flat. Each model can bring its own camera (view), shadow and a
   portrait screen (the handheld Spot Finder). */
(() => {
  const reducedMotion = window.matchMedia('(prefers-reduced-motion: reduce)');
  const clamp = (v, a, b) => Math.min(b, Math.max(a, v));
  const done = new WeakSet();
  const DEG = Math.PI / 180;
  const loading = {};

  function loadModel(url, edition) {
    if (window.KorvixCase && window.KorvixCase[edition]) return Promise.resolve(window.KorvixCase[edition]);
    if (!loading[url]) loading[url] = new Promise((resolve, reject) => {
      const s = document.createElement('script');
      s.src = url; s.async = true;
      s.onload = () => (window.KorvixCase && window.KorvixCase[edition] ? resolve(window.KorvixCase[edition]) : reject(new Error('model')));
      s.onerror = reject;
      document.head.appendChild(s);
    });
    return loading[url];
  }
  const b64 = (s, Type) => { const bin = atob(s); const u8 = new Uint8Array(bin.length); for (let i = 0; i < bin.length; i++) u8[i] = bin.charCodeAt(i); return new Type(u8.buffer); };

  // ---- tiny matrix helpers (column-major, like WebGL) ----
  const mul = (a, b) => { const o = new Float32Array(16); for (let c = 0; c < 4; c++) for (let r = 0; r < 4; r++) { let s = 0; for (let k = 0; k < 4; k++) s += a[k * 4 + r] * b[c * 4 + k]; o[c * 4 + r] = s; } return o; };
  const persp = (fov, asp, n, f) => { const t = 1 / Math.tan(fov / 2); return new Float32Array([t / asp, 0, 0, 0, 0, t, 0, 0, 0, 0, (f + n) / (n - f), -1, 0, 0, 2 * f * n / (n - f), 0]); };
  const rotX = a => { const c = Math.cos(a), s = Math.sin(a); return new Float32Array([1, 0, 0, 0, 0, c, s, 0, 0, -s, c, 0, 0, 0, 0, 1]); };
  const rotY = a => { const c = Math.cos(a), s = Math.sin(a); return new Float32Array([c, 0, -s, 0, 0, 1, 0, 0, s, 0, c, 0, 0, 0, 0, 1]); };
  const trans = (x, y, z) => new Float32Array([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, x, y, z, 1]);

  const VS = `
    attribute vec3 p; attribute vec3 n; attribute vec2 uv;
    uniform mat4 mvp; uniform mat4 model; uniform float scale;
    varying vec3 vN; varying vec3 vP; varying vec2 vUv;
    void main() {
      vec4 wp = model * vec4(p * scale, 1.0);
      vP = wp.xyz; vN = mat3(model) * n; vUv = uv;
      gl_Position = mvp * vec4(p * scale, 1.0);
    }`;
  // matte black PLA: no gloss, only a broad soft sheen and a faint edge light so the shape still reads
  const FS_CASE = `
    precision mediump float;
    varying vec3 vN; varying vec3 vP; uniform vec3 base; uniform float sheen;
    void main() {
      vec3 N = normalize(vN); vec3 V = normalize(vec3(0.0, 0.0, 420.0) - vP);
      vec3 L1 = normalize(vec3(-0.55, 0.85, 0.75)), L2 = normalize(vec3(0.8, 0.25, 0.35)), L3 = normalize(vec3(0.2, 0.4, -0.9));
      float d = 0.55 * max(dot(N, L1), 0.0) + 0.22 * max(dot(N, L2), 0.0) + 0.28 * max(dot(N, L3), 0.0);
      float s = pow(max(dot(N, normalize(L1 + V)), 0.0), 3.0) * sheen;      // wide and soft: matte, not shiny
      float rim = pow(1.0 - max(dot(N, V), 0.0), 2.5) * 0.006;
      float grain = fract(sin(dot(floor(vP.xy * 7.0), vec2(12.99, 78.23))) * 43758.5) * 0.0015;   // print texture
      vec3 c = base * (0.30 + 1.10 * d) + vec3(s + rim + grain);
      gl_FragColor = vec4(pow(c, vec3(1.0 / 2.2)), 1.0);
    }`;
  // display: the recording on the active area, black margin, a faint glass reflection
  const FS_SCREEN = `
    precision mediump float;
    varying vec2 vUv; varying vec3 vP; uniform sampler2D tex; uniform float ready; uniform vec2 win; uniform vec4 act;
    void main() {
      vec2 a = (vUv * win - act.xy) / act.zw;     // the glass (win, mm) and its active area (act: offset, size)
      vec3 c = vec3(0.012);
      if (a.x > 0.0 && a.x < 1.0 && a.y > 0.0 && a.y < 1.0) c = mix(vec3(0.02), texture2D(tex, a).rgb, ready);
      float g = smoothstep(0.0, 1.0, 1.0 - (vUv.x * 0.7 + vUv.y * 0.9));
      c += vec3(0.06) * g * g;
      gl_FragColor = vec4(c, 1.0);
    }`;
  // soft shadow on the desk (lighter while the case floats up)
  const FS_SHADOW = `
    precision mediump float;
    varying vec2 vUv; uniform float sheen;
    void main() { float r = length(vUv * 2.0 - 1.0); gl_FragColor = vec4(0.0, 0.0, 0.0, sheen * pow(clamp(1.0 - r, 0.0, 1.0), 1.6)); }`;

  function program(gl, vs, fs) {
    const sh = (type, src) => { const s = gl.createShader(type); gl.shaderSource(s, src); gl.compileShader(s); if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) throw new Error(gl.getShaderInfoLog(s)); return s; };
    const p = gl.createProgram();
    gl.attachShader(p, sh(gl.VERTEX_SHADER, vs)); gl.attachShader(p, sh(gl.FRAGMENT_SHADER, fs));
    gl.bindAttribLocation(p, 0, 'p'); gl.bindAttribLocation(p, 1, 'n'); gl.bindAttribLocation(p, 2, 'uv');
    gl.linkProgram(p);
    if (!gl.getProgramParameter(p, gl.LINK_STATUS)) throw new Error(gl.getProgramInfoLog(p));
    const u = {}; ['mvp', 'model', 'scale', 'base', 'sheen', 'tex', 'ready', 'win', 'act'].forEach(k => (u[k] = gl.getUniformLocation(p, k)));
    return { p, u };
  }
  function meshBuffers(gl, part) {
    const v = b64(part.v, Int16Array), n = b64(part.n, Int8Array), i = b64(part.i, Uint16Array);
    const vb = gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER, vb); gl.bufferData(gl.ARRAY_BUFFER, v, gl.STATIC_DRAW);
    const nb = gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER, nb); gl.bufferData(gl.ARRAY_BUFFER, n, gl.STATIC_DRAW);
    const ib = gl.createBuffer(); gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, ib); gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, i, gl.STATIC_DRAW);
    return { vb, nb, ib, count: part.count };
  }
  function quadBuffers(gl, corners) {   // corners: TL, TR, BR, BL
    const pos = new Float32Array(corners.flat()), uv = new Float32Array([0, 0, 1, 0, 1, 1, 0, 1]);
    const vb = gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER, vb); gl.bufferData(gl.ARRAY_BUFFER, pos, gl.STATIC_DRAW);
    const tb = gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER, tb); gl.bufferData(gl.ARRAY_BUFFER, uv, gl.STATIC_DRAW);
    const ib = gl.createBuffer(); gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, ib); gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, new Uint16Array([0, 3, 1, 1, 3, 2]), gl.STATIC_DRAW);
    return { vb, tb, ib };
  }

  function viewer(canvas, video, model, view) {
    const gl = canvas.getContext('webgl', { antialias: true, alpha: true, premultipliedAlpha: false }) ||
               canvas.getContext('experimental-webgl', { antialias: true, alpha: true });
    if (!gl) throw new Error('no webgl');
    const pCase = program(gl, VS, FS_CASE), pScreen = program(gl, VS, FS_SCREEN), pShadow = program(gl, VS, FS_SHADOW);
    const parts = { case: meshBuffers(gl, model.case) };
    const screen = quadBuffers(gl, model.display);
    const [lo, hi] = model.bounds;
    // per model: the camera (desk case or handheld) and its shadow on the "desk"
    const cam0 = Object.assign({ dist: 182, cy: 5, fitAsp: 1.5 }, model.view || {});
    const [shW, shD, shGap] = model.shadow || [62, 46, 0];
    const floorY = Math.min(lo[1], hi[1]) - 0.2 + shGap;
    const shadow = quadBuffers(gl, [[-shW, floorY, -shD], [shW, floorY, -shD], [shW, floorY, shD], [-shW, floorY, shD]]);
    const win = model.portrait ? [45, 60] : [60, 45], act = model.portrait ? [0.9, 1.2, 43.2, 57.6] : [1.2, 0.9, 57.6, 43.2];
    const tex = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, tex);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGB, 1, 1, 0, gl.RGB, gl.UNSIGNED_BYTE, new Uint8Array([0, 0, 0]));
    [gl.TEXTURE_WRAP_S, gl.TEXTURE_WRAP_T].forEach(k => gl.texParameteri(gl.TEXTURE_2D, k, gl.CLAMP_TO_EDGE));
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
    let texReady = 0;

    // yaw/pitch: where you turned it; idle*: the gentle idle sway and float added on top
    const state = { yaw: view.yaw, pitch: view.pitch, tyaw: view.yaw, tpitch: view.pitch, idleYaw: 0, idlePitch: 0, lift: 0 };
    function size() {
      const dpr = Math.min(window.devicePixelRatio || 1, 2);
      const w = Math.round(canvas.clientWidth * dpr), h = Math.round(canvas.clientHeight * dpr);
      if (canvas.width !== w || canvas.height !== h) { canvas.width = w; canvas.height = h; }
    }
    function draw() {
      size();
      gl.viewport(0, 0, canvas.width, canvas.height);
      gl.clearColor(0, 0, 0, 0); gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
      gl.enable(gl.DEPTH_TEST); gl.enable(gl.CULL_FACE); gl.cullFace(gl.BACK);
      const asp = canvas.width / canvas.height;
      const proj = persp(26 * DEG, asp, 50, 1200);
      const dist = cam0.dist / Math.min(1, asp / cam0.fitAsp);   // the case fills most of the stage (further back on narrow screens)
      const modelM = mul(rotX((state.pitch + state.idlePitch) * DEG), rotY((state.yaw + state.idleYaw) * DEG));
      const cam = trans(0, cam0.cy, -dist);    // centres the case (its screen face) in the stage
      const lifted = mul(trans(0, state.lift, 0), modelM);        // the case floats; its shadow stays on the desk
      const mvp = mul(proj, mul(cam, lifted));
      const mvpShadow = mul(proj, mul(cam, modelM));
      const modelW = lifted;
      // shadow (no depth write, blended)
      gl.useProgram(pShadow.p); gl.depthMask(false); gl.enable(gl.BLEND); gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA); gl.disable(gl.CULL_FACE);
      gl.uniformMatrix4fv(pShadow.u.mvp, false, mvpShadow); gl.uniformMatrix4fv(pShadow.u.model, false, modelM); gl.uniform1f(pShadow.u.scale, 1);
      gl.uniform1f(pShadow.u.sheen, 0.34 - state.lift * 0.035);
      bindQuad(shadow); gl.drawElements(gl.TRIANGLES, 6, gl.UNSIGNED_SHORT, 0);
      gl.depthMask(true); gl.disable(gl.BLEND); gl.enable(gl.CULL_FACE);
      // case
      gl.useProgram(pCase.p);
      gl.uniformMatrix4fv(pCase.u.mvp, false, mvp); gl.uniformMatrix4fv(pCase.u.model, false, modelW); gl.uniform1f(pCase.u.scale, 0.01);
      gl.disable(gl.CULL_FACE);
      gl.uniform3f(pCase.u.base, 0.0042, 0.0043, 0.0046); gl.uniform1f(pCase.u.sheen, 0.0); bindMesh(parts.case); gl.drawElements(gl.TRIANGLES, parts.case.count, gl.UNSIGNED_SHORT, 0);
      // display
      gl.useProgram(pScreen.p);
      gl.uniformMatrix4fv(pScreen.u.mvp, false, mvp); gl.uniformMatrix4fv(pScreen.u.model, false, modelW); gl.uniform1f(pScreen.u.scale, 1);
      gl.activeTexture(gl.TEXTURE0); gl.bindTexture(gl.TEXTURE_2D, tex);
      if (video.readyState >= 2) {
        try { gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGB, gl.RGB, gl.UNSIGNED_BYTE, video); texReady = 1; } catch (e) { /* not ready */ }
      }
      gl.uniform1i(pScreen.u.tex, 0); gl.uniform1f(pScreen.u.ready, texReady);
      gl.uniform2f(pScreen.u.win, win[0], win[1]); gl.uniform4f(pScreen.u.act, act[0], act[1], act[2], act[3]);
      bindQuad(screen); gl.drawElements(gl.TRIANGLES, 6, gl.UNSIGNED_SHORT, 0);
    }
    function bindMesh(m) {
      gl.bindBuffer(gl.ARRAY_BUFFER, m.vb); gl.enableVertexAttribArray(0); gl.vertexAttribPointer(0, 3, gl.SHORT, false, 0, 0);
      gl.bindBuffer(gl.ARRAY_BUFFER, m.nb); gl.enableVertexAttribArray(1); gl.vertexAttribPointer(1, 3, gl.BYTE, true, 0, 0);
      gl.disableVertexAttribArray(2); gl.vertexAttrib2f(2, 0, 0);
      gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, m.ib);
    }
    function bindQuad(q) {
      gl.bindBuffer(gl.ARRAY_BUFFER, q.vb); gl.enableVertexAttribArray(0); gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 0, 0);
      gl.disableVertexAttribArray(1); gl.vertexAttrib3f(1, 0, 0, 1);
      gl.bindBuffer(gl.ARRAY_BUFFER, q.tb); gl.enableVertexAttribArray(2); gl.vertexAttribPointer(2, 2, gl.FLOAT, false, 0, 0);
      gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, q.ib);
    }
    return { state, draw };
  }

  function setup(root) {
    if (done.has(root)) return;
    done.add(root);
    const stage = root.querySelector('[data-tour-scene]');
    const canvas = root.querySelector('[data-tour-canvas]');
    const video = root.querySelector('[data-tour-video]');
    const playBtn = root.querySelector('[data-tour-play]');
    const playLabel = root.querySelector('[data-tour-play-label]');
    const steps = [...root.querySelectorAll('[data-tour-step]')];
    const chapters = (root.dataset.chapters || '0').split(',').map(Number);
    if (!stage || !canvas || !video) return;
    // Default view: a little from the left and from above, so the screen (on a 30-degree face) faces you.
    const VIEW = { yaw: -20, pitch: 28 };
    const hint = root.querySelector('[data-tour-hint]');

    // ---- video: silent loop; pauses off screen; Space on the focused model pauses / plays ----
    let userPaused = false, visible = false;
    video.muted = true; video.loop = true;
    const syncButton = () => {
      if (!playBtn) return;
      const playing = !video.paused;
      playBtn.setAttribute('aria-pressed', String(playing));
      playBtn.classList.toggle('is-paused', !playing);
      playLabel.textContent = playing ? 'Pause' : 'Play';
    };
    const play = () => { if (visible && !userPaused) video.play().catch(syncButton); };
    if (userPaused) video.pause();
    video.addEventListener('play', () => { syncButton(); kick(); });
    video.addEventListener('pause', syncButton);
    const togglePlay = () => {
      if (video.paused) { userPaused = false; visible = true; video.play().catch(syncButton); }
      else { userPaused = true; video.pause(); }
    };
    playBtn?.addEventListener('click', togglePlay);
    syncButton();

    // ---- steps follow the video; tapping a step jumps to it ----
    const current = () => { let k = 0; chapters.forEach((c, i) => { if (video.currentTime >= c - 0.05) k = i; }); return k; };
    let last = -1;
    video.addEventListener('timeupdate', () => {
      const k = current(); if (k === last) return; last = k;
      steps.forEach((s, i) => { s.classList.toggle('is-active', i === k); s.setAttribute('aria-current', i === k ? 'step' : 'false'); });
    });
    steps.forEach((s, i) => s.addEventListener('click', () => { video.currentTime = chapters[i] || 0; if (video.paused && !userPaused) play(); kick(); }));
    steps[0]?.classList.add('is-active');

    // ---- 3D ----
    // Idle: when nobody is touching it, the case sways gently and floats a little above the desk.
    // It eases out as soon as you drag or press a button, and back in a few seconds later.
    let v = null, raf = 0, drag = null;
    let lastTouch = -1e9, idleAmt = 0, idleClock = 0, lastFrame = 0;
    const IDLE_AFTER = 3500;
    const idleWanted = () => !reducedMotion.matches && !drag && performance.now() - lastTouch > IDLE_AFTER;
    const touched = () => {
      lastTouch = performance.now();
      if (hint && !hint.hidden) { hint.classList.add('is-gone'); setTimeout(() => (hint.hidden = true), 400); }
    };
    function frame(now) {
      raf = 0;
      if (!v) return;
      const s = v.state;
      const dt = Math.min(0.05, lastFrame ? (now - lastFrame) / 1000 : 0.016); lastFrame = now;
      s.yaw += (s.tyaw - s.yaw) * 0.12; s.pitch += (s.tpitch - s.pitch) * 0.12;
      const want = idleWanted() ? 1 : 0;
      idleAmt += (want - idleAmt) * Math.min(1, dt * (want ? 0.9 : 6));    // slow in, quick out
      if (idleAmt > 0.001) idleClock += dt;
      const t = idleClock, k = idleAmt * idleAmt * (3 - 2 * idleAmt);     // smoothstep
      s.idleYaw = k * 11 * Math.sin(t * 2 * Math.PI / 11);
      s.idlePitch = k * 2.5 * Math.sin(t * 2 * Math.PI / 7.5 + 1.1);
      s.lift = k * (1.6 + 1.6 * Math.sin(t * 2 * Math.PI / 4.8));
      v.draw();
      const moving = Math.abs(s.tyaw - s.yaw) > 0.05 || Math.abs(s.tpitch - s.pitch) > 0.05;
      const idling = idleAmt > 0.001 || want;
      if (visible && (moving || drag || idling || !video.paused)) raf = requestAnimationFrame(frame);
      else lastFrame = 0;
    }
    function kick() { if (!raf && v) raf = requestAnimationFrame(frame); }
    const turn = (dyaw, dpitch, abs) => {
      if (!v) return;
      touched();
      const s = v.state;
      if (abs) { s.tyaw = dyaw; s.tpitch = dpitch; } else { s.tyaw = clamp(s.tyaw + dyaw, -170, 170); s.tpitch = clamp(s.tpitch + dpitch, -10, 70); }
      kick();
    };
    root.querySelectorAll('[data-tour-rotate]').forEach(b => b.addEventListener('click', () => turn(Number(b.dataset.tourRotate) * 35, 0)));
    root.querySelector('[data-tour-reset]')?.addEventListener('click', () => turn(VIEW.yaw, VIEW.pitch, true));
    stage.addEventListener('pointerdown', e => {
      if (!v || (e.pointerType === 'mouse' && e.button !== 0)) return;
      // start from what you see (including the idle sway), so grabbing it never jumps
      const s = v.state;
      s.tyaw = s.yaw = s.yaw + s.idleYaw; s.tpitch = s.pitch = s.pitch + s.idlePitch;
      idleAmt = 0; s.idleYaw = s.idlePitch = 0; touched();
      drag = { x: e.clientX, y: e.clientY, yaw: s.tyaw, pitch: s.tpitch, moved: false };
      stage.setPointerCapture(e.pointerId); kick();
    });
    stage.addEventListener('pointermove', e => {
      if (!drag) return;
      const dx = e.clientX - drag.x, dy = e.clientY - drag.y;
      if (!drag.moved && Math.hypot(dx, dy) < 4) return;
      drag.moved = true; stage.classList.add('is-dragging');
      v.state.tyaw = clamp(drag.yaw + dx * 0.5, -170, 170);
      v.state.tpitch = clamp(drag.pitch + dy * 0.35, -10, 70);
      kick();
    });
    const end = () => { if (drag) touched(); drag = null; stage.classList.remove('is-dragging'); kick(); };
    // keyboard: arrows turn it, Space pauses / plays the screen, Home goes back to the start view
    stage.addEventListener('keydown', e => {
      const k = e.key;
      if (k === 'ArrowLeft' || k === 'ArrowRight') turn(k === 'ArrowLeft' ? -20 : 20, 0);
      else if (k === 'ArrowUp' || k === 'ArrowDown') turn(0, k === 'ArrowUp' ? 8 : -8);
      else if (k === ' ' || k === 'Enter') togglePlay();
      else if (k === 'Home') turn(VIEW.yaw, VIEW.pitch, true);
      else return;
      e.preventDefault();
    });
    stage.addEventListener('pointerup', end); stage.addEventListener('pointercancel', end);
    new ResizeObserver(kick).observe(canvas);

    new IntersectionObserver(es => {
      visible = es[0].isIntersecting;
      if (visible) { play(); kick(); start(); } else video.pause();
    }, { threshold: 0.1, rootMargin: '200px 0px' }).observe(stage);

    let started = false;
    function start() {
      if (started) return; started = true;
      loadModel(root.dataset.model, root.dataset.edition).then(model => {
        if (model.view) { VIEW.yaw = model.view.yaw; VIEW.pitch = model.view.pitch; }   // e.g. the handheld Spot Finder
        // intro: it turns in from the side and settles on the default view
        const intro = reducedMotion.matches ? VIEW : { yaw: VIEW.yaw - 70, pitch: VIEW.pitch - 14 };
        v = viewer(canvas, video, model, intro);
        v.state.tyaw = VIEW.yaw; v.state.tpitch = VIEW.pitch;
        root.classList.add('is-3d');
        kick();
      }).catch(() => root.classList.add('no-3d'));   // shows the recording flat
    }
  }

  // Home page tabs (Korvix Pulse / Korvix MarketPlay). A hidden tour stops its video and drawing.
  function setupTabs(list) {
    if (done.has(list)) return;
    done.add(list);
    const section = list.closest('section') || document;
    const tabs = [...list.querySelectorAll('[data-tour-tab]')];
    const select = (tab, focus) => {
      tabs.forEach(t => {
        const on = t === tab;
        t.setAttribute('aria-selected', String(on)); t.tabIndex = on ? 0 : -1;
        const panel = section.querySelector(`[data-tour-panel="${t.dataset.tourTab}"]`);
        if (panel) {
          panel.hidden = !on;
          if (!on) panel.querySelectorAll('video').forEach(vid => vid.pause());
        }
      });
      if (focus) tab.focus();
    };
    tabs.forEach((t, i) => {
      t.addEventListener('click', () => select(t));
      t.addEventListener('keydown', e => {
        const d = e.key === 'ArrowRight' ? 1 : e.key === 'ArrowLeft' ? -1 : 0;
        if (d) { e.preventDefault(); select(tabs[(i + d + tabs.length) % tabs.length], true); }
      });
    });
  }

  const setupAll = (r = document) => {
    r.querySelectorAll('[data-tour-tabs]').forEach(setupTabs);
    r.querySelectorAll('[data-korvix-tour]').forEach(setup);
  };
  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', () => setupAll(), { once: true });
  else setupAll();
  document.addEventListener('shopify:section:load', e => setupAll(e.target));
})();
