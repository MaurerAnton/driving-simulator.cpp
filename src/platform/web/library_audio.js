// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The browser side of platform/audio.h: the Web Audio API and the speech synthesis,
 * handed through to the C++ (platform/web/audio_web.cpp).  It holds no game logic.
 *
 * Contexts, nodes, buffers and waves are named by small numbers.  The C++ counts who
 * names each and says when the table may let go of one (fsa_free): a node that still
 * sounds is then kept by the browser's audio graph alone, until it has ended – as a
 * node is that JavaScript holds no variable for any more.
 *
 * A call the browser refuses (a ramp to zero, a source started twice …) is not an
 * error of the game's: it is passed over, and said once on the console.
 *
 * An Emscripten "JS library" (--js-library; every library_*.js of this folder is linked).
 */
addToLibrary({
  $FSA__deps: ['$UTF8ToString', '$stringToNewUTF8'],
  $FSA: {
    /** handle → AudioContext | AudioNode | AudioBuffer | PeriodicWave (0: none) */
    objs: [null],
    free: [],
    ids: null,
    /** the settings of nodes by number (platform/audio.h, audioBackend::Param – keep the two lists alike) */
    PARAMS: ['gain', 'frequency', 'detune', 'Q', 'pan', 'delayTime', 'threshold', 'knee', 'ratio', 'attack', 'release', 'playbackRate'],
    told: false,
    /** utterance id → SpeechSynthesisUtterance while it is being read (Chrome forgets one that nothing holds, and never ends it) */
    utter: {},
    listening: false,

    handle(o) {
      if (!o) return 0;
      if (!FSA.ids) FSA.ids = new WeakMap();
      let h = FSA.ids.get(o);
      if (h) return h;
      h = FSA.free.length ? FSA.free.pop() : FSA.objs.length;
      FSA.objs[h] = o;
      FSA.ids.set(o, h);
      return h;
    },
    /** the setting number `which` of the node `h` (undefined: there is no such node, or it has no such setting) */
    param(h, which) {
      const node = FSA.objs[h];
      return node ? node[FSA.PARAMS[which]] : undefined;
    },
    str(ptr) {
      return UTF8ToString(ptr);
    },
    /** a text for the C++ (which frees it) */
    out(s) {
      return stringToNewUTF8(s == null ? '' : String(s));
    },
    /** the browser would not: said once */
    refused(e) {
      if (FSA.told) return;
      FSA.told = true;
      console.warn('audio: ' + e);
    },
    speech() {
      return 'speechSynthesis' in window ? window.speechSynthesis : null;
    },
  },

  // ---------------------------------------------------------------------------
  // The context
  // ---------------------------------------------------------------------------
  fsa_supported: function () {
    return window.AudioContext || window.webkitAudioContext ? 1 : 0;
  },
  /** new AudioContext() – it only runs when this is called from a tap or a key (else it waits, 'suspended', for resume() from one) */
  fsa_context_new__deps: ['$FSA'],
  fsa_context_new: function () {
    const AC = window.AudioContext || window.webkitAudioContext;
    if (!AC) return 0;
    try {
      return FSA.handle(new AC());
    } catch (e) {
      FSA.refused(e);
      return 0;
    }
  },
  /** what: 0 currentTime, 1 sampleRate, 2 state (0 suspended – or interrupted, as Safari says –, 1 running, 2 closed) */
  fsa_context_num__deps: ['$FSA'],
  fsa_context_num: function (c, what) {
    const ctx = FSA.objs[c];
    if (!ctx) return what === 2 ? 2 : 0;
    if (what === 0) return ctx.currentTime;
    if (what === 1) return ctx.sampleRate;
    return ctx.state === 'running' ? 1 : ctx.state === 'closed' ? 2 : 0;
  },
  /** what: 0 resume(), 1 suspend() */
  fsa_context_call__deps: ['$FSA'],
  fsa_context_call: function (c, what) {
    const ctx = FSA.objs[c];
    if (!ctx) return;
    try {
      const r = what === 0 ? ctx.resume() : ctx.suspend();
      // (the promise is refused where the browser does not let the sound start yet: not an error of the game's)
      if (r && typeof r.catch === 'function') r.catch(() => {});
    } catch (e) {
      FSA.refused(e);
    }
  },
  fsa_destination__deps: ['$FSA'],
  fsa_destination: function (c) {
    const ctx = FSA.objs[c];
    return ctx ? FSA.handle(ctx.destination) : 0;
  },

  // ---------------------------------------------------------------------------
  // Nodes, buffers, waves
  // ---------------------------------------------------------------------------
  /** kind: 0 gain, 1 biquad filter, 2 oscillator, 3 buffer source, 4 dynamics compressor, 5 delay (`a`: its longest, s), 6 stereo panner */
  fsa_create__deps: ['$FSA'],
  fsa_create: function (c, kind, a) {
    const ctx = FSA.objs[c];
    if (!ctx) return 0;
    try {
      switch (kind) {
        case 0: return FSA.handle(ctx.createGain());
        case 1: return FSA.handle(ctx.createBiquadFilter());
        case 2: return FSA.handle(ctx.createOscillator());
        case 3: return FSA.handle(ctx.createBufferSource());
        case 4: return FSA.handle(ctx.createDynamicsCompressor());
        case 5: return FSA.handle(ctx.createDelay(a > 0 ? a : 1));
        case 6: return typeof ctx.createStereoPanner === 'function' ? FSA.handle(ctx.createStereoPanner()) : 0;
      }
    } catch (e) {
      FSA.refused(e);
    }
    return 0;
  },
  fsa_has_panner__deps: ['$FSA'],
  fsa_has_panner: function (c) {
    const ctx = FSA.objs[c];
    return ctx && typeof ctx.createStereoPanner === 'function' ? 1 : 0;
  },
  fsa_buffer__deps: ['$FSA'],
  fsa_buffer: function (c, channels, length, sampleRate) {
    const ctx = FSA.objs[c];
    if (!ctx) return 0;
    try {
      return FSA.handle(ctx.createBuffer(channels, length, sampleRate));
    } catch (e) {
      FSA.refused(e);
      return 0;
    }
  },
  /** `n` samples (Float32) from `ptr` into a channel, from its sample `start` on */
  fsa_buffer_write__deps: ['$FSA'],
  fsa_buffer_write: function (b, channel, ptr, n, start) {
    const buf = FSA.objs[b];
    if (!buf) return;
    try {
      buf.getChannelData(channel).set(HEAPF32.subarray(ptr >>> 2, (ptr >>> 2) + n), start);
    } catch (e) {
      FSA.refused(e);
    }
  },
  /** the first `n` samples of a channel to `ptr` */
  fsa_buffer_read__deps: ['$FSA'],
  fsa_buffer_read: function (b, channel, ptr, n) {
    const buf = FSA.objs[b];
    if (!buf) return;
    try {
      const d = buf.getChannelData(channel);
      HEAPF32.set(n < d.length ? d.subarray(0, n) : d, ptr >>> 2);
    } catch (e) {
      FSA.refused(e);
    }
  },
  /** createPeriodicWave: `n` cosine and `n` sine amplitudes (Float32) */
  fsa_wave__deps: ['$FSA'],
  fsa_wave: function (c, realPtr, imagPtr, n, disableNormalization) {
    const ctx = FSA.objs[c];
    if (!ctx) return 0;
    try {
      // (copies: the wave must not look into the program's memory, which moves when it grows)
      const real = HEAPF32.slice(realPtr >>> 2, (realPtr >>> 2) + n);
      const imag = HEAPF32.slice(imagPtr >>> 2, (imagPtr >>> 2) + n);
      return FSA.handle(ctx.createPeriodicWave(real, imag, { disableNormalization: !!disableNormalization }));
    } catch (e) {
      FSA.refused(e);
      return 0;
    }
  },
  /** param below 0: into the node `d`; else into that setting of it */
  fsa_connect__deps: ['$FSA'],
  fsa_connect: function (n, d, param) {
    const node = FSA.objs[n];
    const dest = param < 0 ? FSA.objs[d] : FSA.param(d, param);
    if (!node || !dest) return;
    try {
      node.connect(dest);
    } catch (e) {
      FSA.refused(e);
    }
  },
  /** d 0: from everything it feeds */
  fsa_disconnect__deps: ['$FSA'],
  fsa_disconnect: function (n, d, param) {
    const node = FSA.objs[n];
    if (!node) return;
    try {
      if (!d) node.disconnect();
      else {
        const dest = param < 0 ? FSA.objs[d] : FSA.param(d, param);
        if (dest) node.disconnect(dest);
      }
    } catch (e) {
      /* it was not connected (any more) */
    }
  },
  /** op: 0 value =, 1 setValueAtTime, 2 linearRampToValueAtTime, 3 exponentialRampToValueAtTime, 4 setTargetAtTime, 5 cancelScheduledValues */
  fsa_param__deps: ['$FSA'],
  fsa_param: function (n, which, op, v, t, tc) {
    const p = FSA.param(n, which);
    if (!p) return;
    try {
      switch (op) {
        case 0: p.value = v; break;
        case 1: p.setValueAtTime(v, t); break;
        case 2: p.linearRampToValueAtTime(v, t); break;
        case 3: p.exponentialRampToValueAtTime(v, t); break;
        case 4: p.setTargetAtTime(v, t, tc); break;
        case 5: p.cancelScheduledValues(t); break;
      }
    } catch (e) {
      FSA.refused(e);
    }
  },
  fsa_param_get__deps: ['$FSA'],
  fsa_param_get: function (n, which) {
    const p = FSA.param(n, which);
    return p ? p.value : 0;
  },
  fsa_set_type__deps: ['$FSA'],
  fsa_set_type: function (n, type) {
    const node = FSA.objs[n];
    if (!node) return;
    try {
      node.type = FSA.str(type);
    } catch (e) {
      FSA.refused(e);
    }
  },
  fsa_set_wave__deps: ['$FSA'],
  fsa_set_wave: function (n, w) {
    const node = FSA.objs[n];
    const wave = FSA.objs[w];
    if (!node || !wave) return;
    try {
      node.setPeriodicWave(wave);
    } catch (e) {
      FSA.refused(e);
    }
  },
  fsa_set_buffer__deps: ['$FSA'],
  fsa_set_buffer: function (n, b) {
    const node = FSA.objs[n];
    if (!node) return;
    try {
      node.buffer = FSA.objs[b] || null;
    } catch (e) {
      FSA.refused(e);
    }
  },
  fsa_set_loop__deps: ['$FSA'],
  fsa_set_loop: function (n, on) {
    const node = FSA.objs[n];
    if (node) node.loop = !!on;
  },
  /** duration below 0: none given (a buffer source then plays to its end, or round and round) */
  fsa_start__deps: ['$FSA'],
  fsa_start: function (n, when, offset, duration) {
    const node = FSA.objs[n];
    if (!node) return;
    try {
      if (duration >= 0) node.start(when, offset, duration);
      else if (offset > 0) node.start(when, offset);
      else node.start(when);
    } catch (e) {
      FSA.refused(e);
    }
  },
  fsa_stop__deps: ['$FSA'],
  fsa_stop: function (n, when) {
    const node = FSA.objs[n];
    if (!node) return;
    try {
      node.stop(when);
    } catch (e) {
      /* it was never started, or has stopped already */
    }
  },
  /** node.onended = the C++ handler `id` (0: null); returns the handler that was there (0: none) */
  fsa_on_ended__deps: ['$FSA'],
  fsa_on_ended: function (n, id) {
    const node = FSA.objs[n];
    if (!node) return id;
    const was = node.onended && node.onended.fsId ? node.onended.fsId : 0;
    if (id) {
      const fn = function () {
        // (a source ends once: the C++ forgets the handler as it runs it)
        if (node.onended === fn) node.onended = null;
        _fsa_ended(id);
      };
      fn.fsId = id;
      node.onended = fn;
    } else node.onended = null;
    return was;
  },
  /** nothing in the C++ names it any more */
  fsa_free__deps: ['$FSA'],
  fsa_free: function (h) {
    const o = FSA.objs[h];
    if (!o) return;
    FSA.ids.delete(o);
    FSA.objs[h] = null;
    FSA.free.push(h);
  },

  // ---------------------------------------------------------------------------
  // Speech
  // ---------------------------------------------------------------------------
  fss_supported: function () {
    return 'speechSynthesis' in window ? 1 : 0;
  },
  /** from now on the C++ hears when the list of voices changes */
  fss_init__deps: ['$FSA'],
  fss_init: function () {
    const synth = FSA.speech();
    if (FSA.listening || !synth) return;
    FSA.listening = true;
    // (in older Safaris speechSynthesis is no event target: the game asks for the list again and again there)
    if (synth.addEventListener) {
      synth.addEventListener('voiceschanged', function () {
        _fss_voices_changed();
      });
    }
  },
  /** the voices as one text: each a record ended by U+001E, its fields parted by U+001F – name, lang, voiceURI, flags (1 local, 2 default) */
  fss_voices__deps: ['$FSA'],
  fss_voices: function () {
    let list = [];
    try {
      const synth = FSA.speech();
      list = (synth && synth.getVoices()) || [];
    } catch (e) {
      list = [];
    }
    const clean = (s) => String(s == null ? '' : s).replace(/[\x1e\x1f]/g, ' ');
    let s = '';
    for (const v of list) s += clean(v.name) + '\x1f' + clean(v.lang) + '\x1f' + clean(v.voiceURI) + '\x1f' + ((v.localService ? 1 : 0) | (v.default ? 2 : 0)) + '\x1e';
    return FSA.out(s);
  },
  /** speak a new utterance; its end (kind 0) or its failure (kind 1, with the event's word for why) goes to fss_event(id, kind, why) */
  fss_speak__deps: ['$FSA', 'free'],
  fss_speak: function (id, text, lang, hasVoice, voiceURI, voiceName, voiceLang, rate, pitch, volume) {
    try {
      const synth = FSA.speech();
      if (!synth) return;
      const u = new SpeechSynthesisUtterance(FSA.str(text));
      u.lang = FSA.str(lang);
      if (hasVoice) {
        // the voice itself, found again in the browser's list
        const uri = FSA.str(voiceURI);
        const name = FSA.str(voiceName);
        const vlang = FSA.str(voiceLang);
        for (const v of synth.getVoices() || []) {
          if (v.voiceURI === uri && v.name === name && v.lang === vlang) {
            u.voice = v;
            break;
          }
        }
      }
      u.rate = rate;
      u.pitch = pitch;
      u.volume = volume;
      const done = function (kind, why) {
        // (once: an utterance ends, or fails)
        if (FSA.utter[id] !== u) return;
        delete FSA.utter[id];
        const p = FSA.out(why || '');
        _fss_event(id, kind, p);
        _free(p);
      };
      u.onend = function () {
        done(0, '');
      };
      u.onerror = function (e) {
        done(1, e && e.error);
      };
      FSA.utter[id] = u;
      synth.speak(u);
    } catch (e) {
      FSA.refused(e);
    }
  },
  fss_cancel__deps: ['$FSA'],
  fss_cancel: function () {
    try {
      const synth = FSA.speech();
      if (synth) synth.cancel();
    } catch (e) {
      /* nothing was being read */
    }
  },
  fss_resume__deps: ['$FSA'],
  fss_resume: function () {
    try {
      const synth = FSA.speech();
      if (synth) synth.resume();
    } catch (e) {
      /* it was not paused */
    }
  },
  /** 1 paused, 2 speaking, 4 pending */
  fss_flags__deps: ['$FSA'],
  fss_flags: function () {
    const synth = FSA.speech();
    if (!synth) return 0;
    return (synth.paused ? 1 : 0) | (synth.speaking ? 2 : 0) | (synth.pending ? 4 : 0);
  },
});
