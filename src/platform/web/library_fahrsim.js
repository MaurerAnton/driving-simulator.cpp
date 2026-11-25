// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The browser side of the host layer (platform/plat.h, dom.h, canvas.h, glhost.h):
 * the only JavaScript of the game.  It holds no game logic – every function here
 * hands one browser call through to the C++ (elements, canvases and pictures are
 * named by small numbers) or one browser event back.
 *
 * An Emscripten "JS library" (--js-library): the C++ side is platform/web/*.cpp.
 */
addToLibrary({
  // ---------------------------------------------------------------------------
  // The page: elements by handle
  // ---------------------------------------------------------------------------
  $FSD__deps: ['$UTF8ToString', '$stringToUTF8', '$stringToNewUTF8', '$lengthBytesUTF8'],
  $FSD: {
    /** handle → element (0: none; 1 window, 2 document, 3 body, 4 documentElement – set by fsd_init) */
    els: [null],
    free: [],
    ids: null,
    /** where the C++ reads an event from: numbers (Float64) and four short texts */
    numPtr: 0,
    strPtr: 0,
    /** listener id → the function that was given to addEventListener */
    fns: {},
    /** context handle → CanvasRenderingContext2D; gradient handle → CanvasGradient; picture handle → ImageBitmap/HTMLImageElement */
    ctxs: [null],
    ctxOf: null,
    grads: [null],
    images: [null],

    handle(el) {
      if (!el) return 0;
      let h = FSD.ids.get(el);
      if (h) return h;
      h = FSD.free.length ? FSD.free.pop() : FSD.els.length;
      FSD.els[h] = el;
      FSD.ids.set(el, h);
      return h;
    },
    el(h) {
      return FSD.els[h];
    },
    str(ptr) {
      return UTF8ToString(ptr);
    },
    /** a text for the C++ (which frees it) */
    out(s) {
      return stringToNewUTF8(s == null ? '' : String(s));
    },
    /** tell the C++ about an event; do what its handler asked for */
    dispatch(id, e, current, once) {
      const n = FSD.numPtr >> 3;
      const f = HEAPF64;
      f[n] = e.clientX || 0;
      f[n + 1] = e.clientY || 0;
      f[n + 2] = e.offsetX || 0;
      f[n + 3] = e.offsetY || 0;
      f[n + 4] = e.movementX || 0;
      f[n + 5] = e.movementY || 0;
      f[n + 6] = e.pointerId || 0;
      f[n + 7] = e.button || 0;
      f[n + 8] = e.buttons || 0;
      f[n + 9] = e.isPrimary === false ? 0 : 1;
      f[n + 10] = e.deltaX || 0;
      f[n + 11] = e.deltaY || 0;
      f[n + 12] = e.deltaMode || 0;
      f[n + 13] = (e.ctrlKey ? 1 : 0) | (e.altKey ? 2 : 0) | (e.shiftKey ? 4 : 0) | (e.metaKey ? 8 : 0) | (e.repeat ? 16 : 0) | (e.isTrusted === false ? 32 : 0);
      const orient = typeof e.gamma === 'number' && e.gamma !== null;
      f[n + 14] = orient ? e.alpha || 0 : 0;
      f[n + 15] = orient ? e.beta || 0 : 0;
      f[n + 16] = orient ? e.gamma || 0 : 0;
      f[n + 17] = orient ? 1 : 0;
      f[n + 18] = e.gamepad ? e.gamepad.index : -1;
      const target = e.target && (e.target.nodeType === 1 || e.target === window || e.target === document) ? e.target : null;
      f[n + 19] = FSD.handle(target);
      f[n + 20] = FSD.handle(current);
      // fingers of a touch event: how many are down, how many this event is about, then id, x, y of each (at most 8 + 8)
      let k = n + 23;
      const put = (list) => {
        const c = list ? Math.min(list.length, 8) : 0;
        for (let i = 0; i < c; i++) {
          f[k++] = list[i].identifier;
          f[k++] = list[i].clientX;
          f[k++] = list[i].clientY;
        }
        return c;
      };
      f[n + 21] = put(e.touches);
      f[n + 22] = put(e.changedTouches);
      const s = FSD.strPtr;
      stringToUTF8(e.type || '', s, 48);
      stringToUTF8(e.key || '', s + 48, 48);
      stringToUTF8(e.code || '', s + 96, 48);
      stringToUTF8(e.pointerType || '', s + 144, 16);
      const flags = _fsd_event(id);
      if (flags & 1) e.preventDefault();
      if (flags & 2) e.stopPropagation();
      if (once) _fsd_listener_gone(id);
    },
    listener(id, once) {
      return function (e) {
        FSD.dispatch(id, e, this, once);
      };
    },
    /** fooBar → foo-bar for style.setProperty (custom properties and CSS names stay) */
    css(name) {
      return name.startsWith('--') || name.includes('-') ? name : name.replace(/[A-Z]/g, (c) => '-' + c.toLowerCase());
    },
  },

  fsd_init__deps: ['$FSD'],
  fsd_init: function (numPtr, strPtr) {
    FSD.ids = new WeakMap();
    FSD.ctxOf = new WeakMap();
    FSD.numPtr = numPtr;
    FSD.strPtr = strPtr;
    for (const el of [window, document, document.body, document.documentElement]) FSD.handle(el);
  },
  fsd_free__deps: ['$FSD'],
  fsd_free: function (h) {
    const el = FSD.els[h];
    if (!el || h <= 4) return;
    FSD.ids.delete(el);
    FSD.els[h] = null;
    FSD.free.push(h);
    // a canvas the program has let go of and that is not in the page: its 2D context (and its pixels) go too
    const c = FSD.ctxOf.get(el);
    if (c && !el.isConnected) {
      FSD.ctxs[c] = null;
      FSD.ctxOf.delete(el);
    }
  },
  fsd_create__deps: ['$FSD'],
  fsd_create: function (tag) {
    return FSD.handle(document.createElement(FSD.str(tag)));
  },
  fsd_by_id__deps: ['$FSD'],
  fsd_by_id: function (id) {
    return FSD.handle(document.getElementById(FSD.str(id)));
  },
  fsd_query__deps: ['$FSD'],
  fsd_query: function (h, sel) {
    const el = FSD.el(h);
    try {
      return FSD.handle((el === window ? document : el).querySelector(FSD.str(sel)));
    } catch (e) {
      return 0;
    }
  },
  /** writes up to `max` handles to `out` (Int32), returns how many there are */
  fsd_query_all__deps: ['$FSD'],
  fsd_query_all: function (h, sel, out, max) {
    const el = FSD.el(h);
    let list;
    try {
      list = (el === window ? document : el).querySelectorAll(FSD.str(sel));
    } catch (e) {
      return 0;
    }
    const n = Math.min(list.length, max);
    for (let i = 0; i < n; i++) HEAP32[(out >> 2) + i] = FSD.handle(list[i]);
    return list.length;
  },
  fsd_append__deps: ['$FSD'],
  fsd_append: function (p, c) {
    FSD.el(p).append(FSD.el(c));
  },
  fsd_append_text__deps: ['$FSD'],
  fsd_append_text: function (p, text) {
    FSD.el(p).append(FSD.str(text));
  },
  fsd_prepend__deps: ['$FSD'],
  fsd_prepend: function (p, c) {
    FSD.el(p).prepend(FSD.el(c));
  },
  fsd_insert_before__deps: ['$FSD'],
  fsd_insert_before: function (p, c, ref) {
    FSD.el(p).insertBefore(FSD.el(c), ref ? FSD.el(ref) : null);
  },
  fsd_after__deps: ['$FSD'],
  fsd_after: function (h, o) {
    FSD.el(h).after(FSD.el(o));
  },
  fsd_before__deps: ['$FSD'],
  fsd_before: function (h, o) {
    FSD.el(h).before(FSD.el(o));
  },
  fsd_remove__deps: ['$FSD'],
  fsd_remove: function (h) {
    FSD.el(h).remove();
  },
  fsd_clear__deps: ['$FSD'],
  fsd_clear: function (h) {
    FSD.el(h).replaceChildren();
  },
  fsd_replace_with__deps: ['$FSD'],
  fsd_replace_with: function (h, o) {
    FSD.el(h).replaceWith(FSD.el(o));
  },
  fsd_set_str__deps: ['$FSD'],
  fsd_set_str: function (h, prop, v) {
    FSD.el(h)[FSD.str(prop)] = FSD.str(v);
  },
  fsd_set_num__deps: ['$FSD'],
  fsd_set_num: function (h, prop, v) {
    FSD.el(h)[FSD.str(prop)] = v;
  },
  fsd_set_bool__deps: ['$FSD'],
  fsd_set_bool: function (h, prop, v) {
    FSD.el(h)[FSD.str(prop)] = !!v;
  },
  fsd_get_str__deps: ['$FSD'],
  fsd_get_str: function (h, prop) {
    const el = FSD.el(h);
    return FSD.out(el ? el[FSD.str(prop)] : '');
  },
  fsd_get_num__deps: ['$FSD'],
  fsd_get_num: function (h, prop) {
    const el = FSD.el(h);
    const v = el ? Number(el[FSD.str(prop)]) : 0;
    return v === v ? v : 0;
  },
  fsd_get_bool__deps: ['$FSD'],
  fsd_get_bool: function (h, prop) {
    const el = FSD.el(h);
    return el && el[FSD.str(prop)] ? 1 : 0;
  },
  fsd_set_attr__deps: ['$FSD'],
  fsd_set_attr: function (h, name, v) {
    FSD.el(h).setAttribute(FSD.str(name), FSD.str(v));
  },
  fsd_has_attr__deps: ['$FSD'],
  fsd_has_attr: function (h, name) {
    return FSD.el(h).hasAttribute(FSD.str(name)) ? 1 : 0;
  },
  fsd_get_attr__deps: ['$FSD'],
  fsd_get_attr: function (h, name) {
    return FSD.out(FSD.el(h).getAttribute(FSD.str(name)));
  },
  fsd_remove_attr__deps: ['$FSD'],
  fsd_remove_attr: function (h, name) {
    FSD.el(h).removeAttribute(FSD.str(name));
  },
  fsd_set_style__deps: ['$FSD'],
  fsd_set_style: function (h, prop, v) {
    const value = FSD.str(v);
    const name = FSD.css(FSD.str(prop));
    if (value) FSD.el(h).style.setProperty(name, value);
    else FSD.el(h).style.removeProperty(name);
  },
  fsd_get_style__deps: ['$FSD'],
  fsd_get_style: function (h, prop) {
    return FSD.out(FSD.el(h).style.getPropertyValue(FSD.css(FSD.str(prop))));
  },
  fsd_computed_style__deps: ['$FSD'],
  fsd_computed_style: function (h, prop) {
    return FSD.out(getComputedStyle(FSD.el(h)).getPropertyValue(FSD.css(FSD.str(prop))));
  },
  fsd_class_op__deps: ['$FSD'],
  fsd_class_op: function (h, name, op) {
    const list = FSD.el(h).classList;
    const n = FSD.str(name);
    if (op === 0) list.remove(n);
    else if (op === 1) list.add(n);
    else if (op === 2) list.toggle(n);
    return list.contains(n) ? 1 : 0;
  },
  fsd_listen__deps: ['$FSD'],
  fsd_listen: function (h, type, id, flags) {
    const fn = FSD.listener(id, !!(flags & 4));
    FSD.fns[id] = fn;
    FSD.el(h).addEventListener(FSD.str(type), fn, { capture: !!(flags & 1), passive: !!(flags & 2), once: !!(flags & 4) });
  },
  fsd_unlisten__deps: ['$FSD'],
  fsd_unlisten: function (h, type, id, flags) {
    const fn = FSD.fns[id];
    if (!fn) return;
    FSD.el(h).removeEventListener(FSD.str(type), fn, { capture: !!(flags & 1) });
    delete FSD.fns[id];
  },
  fsd_set_handler__deps: ['$FSD'],
  fsd_set_handler: function (h, type, id) {
    const el = FSD.el(h);
    const prop = 'on' + FSD.str(type);
    const was = el[prop] && el[prop].fsId ? el[prop].fsId : 0;
    if (id) {
      const fn = FSD.listener(id, false);
      fn.fsId = id;
      el[prop] = fn;
    } else el[prop] = null;
    return was;
  },
  fsd_rect__deps: ['$FSD'],
  fsd_rect: function (h, out) {
    const el = FSD.el(h);
    const r = el === window ? { left: 0, top: 0, width: innerWidth, height: innerHeight } : el.getBoundingClientRect();
    const o = out >> 3;
    HEAPF64[o] = r.left;
    HEAPF64[o + 1] = r.top;
    HEAPF64[o + 2] = r.width;
    HEAPF64[o + 3] = r.height;
  },
  fsd_parent__deps: ['$FSD'],
  fsd_parent: function (h) {
    return FSD.handle(FSD.el(h).parentElement);
  },
  fsd_child__deps: ['$FSD'],
  fsd_child: function (h, i) {
    return FSD.handle(FSD.el(h).children[i]);
  },
  fsd_child_count__deps: ['$FSD'],
  fsd_child_count: function (h) {
    return FSD.el(h).childElementCount || 0;
  },
  fsd_sibling__deps: ['$FSD'],
  fsd_sibling: function (h, dir) {
    return FSD.handle(dir > 0 ? FSD.el(h).nextElementSibling : FSD.el(h).previousElementSibling);
  },
  fsd_contains__deps: ['$FSD'],
  fsd_contains: function (h, o) {
    return FSD.el(h).contains(FSD.el(o)) ? 1 : 0;
  },
  fsd_closest__deps: ['$FSD'],
  fsd_closest: function (h, sel) {
    const el = FSD.el(h);
    return el.closest ? FSD.handle(el.closest(FSD.str(sel))) : 0;
  },
  fsd_matches__deps: ['$FSD'],
  fsd_matches: function (h, sel) {
    const el = FSD.el(h);
    return el.matches && el.matches(FSD.str(sel)) ? 1 : 0;
  },
  fsd_is_connected__deps: ['$FSD'],
  fsd_is_connected: function (h) {
    const el = FSD.el(h);
    return el === window || el === document || el.isConnected ? 1 : 0;
  },
  fsd_call__deps: ['$FSD'],
  fsd_call: function (h, method) {
    const el = FSD.el(h);
    const m = FSD.str(method);
    try {
      const r = el[m]();
      // (requestFullscreen, play … answer with a promise that may be refused: that is not an error of the game's)
      if (r && typeof r.catch === 'function') r.catch(() => {});
    } catch (e) {
      /* a method the browser does not have, or refuses now */
    }
  },
  fsd_pointer_capture__deps: ['$FSD'],
  fsd_pointer_capture: function (h, id, on) {
    try {
      if (on) FSD.el(h).setPointerCapture(id);
      else FSD.el(h).releasePointerCapture(id);
    } catch (e) {
      /* the pointer is gone */
    }
  },
  fsd_has_pointer_capture__deps: ['$FSD'],
  fsd_has_pointer_capture: function (h, id) {
    const el = FSD.el(h);
    return el.hasPointerCapture && el.hasPointerCapture(id) ? 1 : 0;
  },
  fsd_document_element__deps: ['$FSD'],
  fsd_document_element: function (which) {
    return FSD.handle(document[FSD.str(which)]);
  },
  fsd_exit_fullscreen: function () {
    if (document.fullscreenElement && document.exitFullscreen) document.exitFullscreen().catch(() => {});
  },
  fsd_handle_limit__deps: ['$FSD'],
  fsd_handle_limit: function () {
    return FSD.els.length;
  },
  fsd_forget_listener__deps: ['$FSD'],
  fsd_forget_listener: function (id) {
    delete FSD.fns[id];
  },
  /** which handles are in the page now: writes a byte (1/0) for each handle up to `n` */
  fsd_connected_map__deps: ['$FSD'],
  fsd_connected_map: function (out, n) {
    for (let h = 0; h < n; h++) {
      const el = FSD.els[h];
      HEAPU8[out + h] = el && (h <= 4 || el.isConnected) ? 1 : 0;
    }
  },
  /** for the handles that are not in the page: a number that is the same for those hanging in the same detached tree */
  fsd_tree_map__deps: ['$FSD'],
  fsd_tree_map: function (out, n) {
    const roots = new Map();
    for (let h = 0; h < n; h++) {
      const el = FSD.els[h];
      let id = 0;
      if (el && h > 4 && !el.isConnected) {
        let root = el;
        while (root.parentNode) root = root.parentNode;
        id = roots.get(root);
        if (!id) {
          id = roots.size + 1;
          roots.set(root, id);
        }
      }
      HEAP32[(out >> 2) + h] = id;
    }
  },

  // ---------------------------------------------------------------------------
  // The 2D canvas (the numbers of the operations: platform/canvas.h canvasBackend::Op)
  // ---------------------------------------------------------------------------
  fsc_context__deps: ['$FSD'],
  fsc_context: function (canvas) {
    const el = FSD.el(canvas);
    if (!el || !el.getContext) return 0;
    let h = FSD.ctxOf.get(el);
    if (h) return h;
    const ctx = el.getContext('2d');
    if (!ctx) return 0;
    h = FSD.ctxs.length;
    FSD.ctxs.push(ctx);
    FSD.ctxOf.set(el, h);
    return h;
  },
  fsc_num__deps: ['$FSD'],
  fsc_num: function (c, op, a, b, d, e, f, g, h, i) {
    const x = FSD.ctxs[c];
    switch (op) {
      case 1: x.lineWidth = a; break;
      case 2: x.miterLimit = a; break;
      case 3: x.lineDashOffset = a; break;
      case 4: x.globalAlpha = a; break;
      case 5: x.shadowBlur = a; break;
      case 6: x.shadowOffsetX = a; break;
      case 7: x.shadowOffsetY = a; break;
      case 8: x.imageSmoothingEnabled = !!a; break;
      case 9: x.save(); break;
      case 10: x.restore(); break;
      case 11: x.translate(a, b); break;
      case 12: x.rotate(a); break;
      case 13: x.scale(a, b); break;
      case 14: x.transform(a, b, d, e, f, g); break;
      case 15: x.setTransform(a, b, d, e, f, g); break;
      case 16: x.resetTransform(); break;
      case 17: x.beginPath(); break;
      case 18: x.closePath(); break;
      case 19: x.moveTo(a, b); break;
      case 20: x.lineTo(a, b); break;
      case 21: x.arc(a, b, d, e, f, !!g); break;
      case 22: x.arcTo(a, b, d, e, f); break;
      case 23: x.ellipse(a, b, d, e, f, g, h, !!i); break;
      case 24: x.rect(a, b, d, e); break;
      case 25:
        if (x.roundRect) x.roundRect(a, b, d, e, f);
        else x.rect(a, b, d, e);
        break;
      case 26: x.quadraticCurveTo(a, b, d, e); break;
      case 27: x.bezierCurveTo(a, b, d, e, f, g); break;
      case 28: x.fill(); break;
      case 29: x.fill('evenodd'); break;
      case 30: x.stroke(); break;
      case 31: x.clip(); break;
      case 32: x.fillRect(a, b, d, e); break;
      case 33: x.strokeRect(a, b, d, e); break;
      case 34: x.clearRect(a, b, d, e); break;
      case 35: x.fillStyle = FSD.grads[a]; break;
      case 36: x.strokeStyle = FSD.grads[a]; break;
    }
  },
  fsc_str__deps: ['$FSD'],
  fsc_str: function (c, op, sp, a, b, d) {
    const s = FSD.str(sp);
    if (op === 115) {
      FSD.grads[c].addColorStop(a, s);
      return;
    }
    const x = FSD.ctxs[c];
    switch (op) {
      case 100: x.fillStyle = s; break;
      case 101: x.strokeStyle = s; break;
      case 102: x.lineCap = s; break;
      case 103: x.lineJoin = s; break;
      case 104: x.font = s; break;
      case 105: x.textAlign = s; break;
      case 106: x.textBaseline = s; break;
      case 107: x.direction = s; break;
      case 108: x.letterSpacing = s; break;
      case 109: x.globalCompositeOperation = s; break;
      case 110: x.shadowColor = s; break;
      case 111: x.filter = s; break;
      case 112: x.fillText(s, a, b); break;
      case 113: x.fillText(s, a, b, d); break;
      case 114: x.strokeText(s, a, b); break;
    }
  },
  fsc_line_dash__deps: ['$FSD'],
  fsc_line_dash: function (c, ptr, n) {
    const list = [];
    for (let i = 0; i < n; i++) list.push(HEAPF64[(ptr >> 3) + i]);
    FSD.ctxs[c].setLineDash(list);
  },
  fsc_polyline__deps: ['$FSD'],
  fsc_polyline: function (c, ptr, n, close) {
    const x = FSD.ctxs[c];
    const p = ptr >> 3;
    const f = HEAPF64;
    x.moveTo(f[p], f[p + 1]);
    for (let i = 1; i < n; i++) x.lineTo(f[p + 2 * i], f[p + 2 * i + 1]);
    if (close) x.closePath();
  },
  fsc_draw_image__deps: ['$FSD'],
  fsc_draw_image: function (c, fromCanvas, source, ptr, n) {
    const src = fromCanvas ? FSD.el(source) : FSD.images[source];
    if (!src) return;
    const p = ptr >> 3;
    const f = HEAPF64;
    const x = FSD.ctxs[c];
    try {
      if (n === 2) x.drawImage(src, f[p], f[p + 1]);
      else if (n === 4) x.drawImage(src, f[p], f[p + 1], f[p + 2], f[p + 3]);
      else x.drawImage(src, f[p], f[p + 1], f[p + 2], f[p + 3], f[p + 4], f[p + 5], f[p + 6], f[p + 7]);
    } catch (e) {
      /* a picture that is not ready, a canvas without size */
    }
  },
  fsc_measure__deps: ['$FSD'],
  fsc_measure: function (c, text, out) {
    const m = FSD.ctxs[c].measureText(FSD.str(text));
    const o = out >> 3;
    HEAPF64[o] = m.width;
    HEAPF64[o + 1] = m.actualBoundingBoxAscent || 0;
    HEAPF64[o + 2] = m.actualBoundingBoxDescent || 0;
    HEAPF64[o + 3] = m.actualBoundingBoxLeft || 0;
    HEAPF64[o + 4] = m.actualBoundingBoxRight || 0;
  },
  fsc_gradient__deps: ['$FSD'],
  fsc_gradient: function (c, radial, a, b, d, e, f, g) {
    const x = FSD.ctxs[c];
    const grad = radial ? x.createRadialGradient(a, b, d, e, f, g) : x.createLinearGradient(a, b, d, e);
    // (a slot given back is used again: gradients made every frame do not pile up)
    if (!FSD.gradFree) FSD.gradFree = [];
    const at = FSD.gradFree.length ? FSD.gradFree.pop() : FSD.grads.push(null) - 1;
    FSD.grads[at] = grad;
    return at;
  },
  fsc_gradient_free__deps: ['$FSD'],
  fsc_gradient_free: function (g) {
    if (g <= 0 || !FSD.grads[g]) return;
    FSD.grads[g] = null;
    if (!FSD.gradFree) FSD.gradFree = [];
    FSD.gradFree.push(g);
  },
  fsc_get_image_data__deps: ['$FSD'],
  fsc_get_image_data: function (c, x, y, w, h, out) {
    try {
      HEAPU8.set(FSD.ctxs[c].getImageData(x, y, w, h).data, out);
    } catch (e) {
      /* a tainted canvas */
    }
  },
  fsc_put_image_data__deps: ['$FSD'],
  fsc_put_image_data: function (c, ptr, w, h, x, y) {
    const d = new ImageData(new Uint8ClampedArray(HEAPU8.buffer, ptr, w * h * 4).slice(), w, h);
    FSD.ctxs[c].putImageData(d, x, y);
  },
  fsc_to_data_url__deps: ['$FSD'],
  fsc_to_data_url: function (canvas, type, quality) {
    try {
      return FSD.out(FSD.el(canvas).toDataURL(FSD.str(type), quality));
    } catch (e) {
      return FSD.out('');
    }
  },

  // pictures: an address or bytes become something drawImage and texImage2D take (handle → FSD.images)
  fsc_image_load__deps: ['$FSD'],
  fsc_image_load: function (id, url) {
    const img = new Image();
    img.crossOrigin = 'anonymous';
    // nothing but the picture's address is sent: no cookies, no referrer (the aerial photographs' servers – reality/aerial.ts)
    img.referrerPolicy = 'no-referrer';
    img.decoding = 'async';
    img.onload = () => {
      FSD.images.push(img);
      _fsc_image_done(id, FSD.images.length - 1, img.naturalWidth, img.naturalHeight);
    };
    img.onerror = () => _fsc_image_done(id, 0, 0, 0);
    img.src = FSD.str(url);
  },
  fsc_image_decode__deps: ['$FSD'],
  fsc_image_decode: function (id, ptr, n, mime) {
    const blob = new Blob([HEAPU8.slice(ptr, ptr + n)], { type: FSD.str(mime) });
    createImageBitmap(blob).then(
      (bmp) => {
        FSD.images.push(bmp);
        _fsc_image_done(id, FSD.images.length - 1, bmp.width, bmp.height);
      },
      () => _fsc_image_done(id, 0, 0, 0),
    );
  },
  fsc_image_free__deps: ['$FSD'],
  fsc_image_free: function (h) {
    const img = FSD.images[h];
    if (img && img.close) img.close();
    FSD.images[h] = null;
  },

  // ---------------------------------------------------------------------------
  // OpenGL: what only the page has (a canvas's pixels, a decoded picture)
  // ---------------------------------------------------------------------------
  fsg_tex_image__deps: ['$FSD', '$GL'],
  fsg_tex_image: function (fromCanvas, source, flipY, srgb, sizeOut) {
    const src = fromCanvas ? FSD.el(source) : FSD.images[source];
    const gl = GL.currentContext && GL.currentContext.GLctx;
    if (!src || !gl || !src.width || !src.height) return 0;
    gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, !!flipY);
    gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, false);
    gl.pixelStorei(gl.UNPACK_COLORSPACE_CONVERSION_WEBGL, gl.NONE);
    try {
      gl.texImage2D(gl.TEXTURE_2D, 0, srgb ? gl.SRGB8_ALPHA8 : gl.RGBA8, gl.RGBA, gl.UNSIGNED_BYTE, src);
    } catch (e) {
      gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, false);
      return 0;
    }
    gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, false);
    HEAP32[sizeOut >> 2] = src.width;
    HEAP32[(sizeOut >> 2) + 1] = src.height;
    return 1;
  },
  fsg_renderer_name__deps: ['$FSD', '$GL'],
  fsg_renderer_name: function () {
    const gl = GL.currentContext && GL.currentContext.GLctx;
    if (!gl) return FSD.out('none');
    const ext = gl.getExtension('WEBGL_debug_renderer_info');
    return FSD.out(ext ? gl.getParameter(ext.UNMASKED_RENDERER_WEBGL) : gl.getParameter(gl.RENDERER));
  },

  // ---------------------------------------------------------------------------
  // plat.h: files, the store, the clock, the address, the device
  // ---------------------------------------------------------------------------
  fsp_fetch__deps: ['$FSD', 'free'],
  fsp_fetch: function (id, urlPtr, methodPtr, headersPtr, bodyPtr, bodyLen, cachePtr, wantProgress) {
    const init = { method: FSD.str(methodPtr) };
    const cache = FSD.str(cachePtr);
    if (cache) init.cache = cache;
    const headers = FSD.str(headersPtr);
    if (headers) {
      // "Name: value" lines
      init.headers = {};
      for (const line of headers.split('\n')) {
        const i = line.indexOf(':');
        if (i > 0) init.headers[line.slice(0, i)] = line.slice(i + 1).trim();
      }
    }
    if (bodyLen > 0) init.body = HEAPU8.slice(bodyPtr, bodyPtr + bodyLen);
    const deliver = (status, bytes, type) => {
      const n = bytes ? bytes.length : 0;
      const ptr = _fsp_fetch_room(id, n);
      if (n) HEAPU8.set(bytes, ptr);
      const t = FSD.out(type || '');
      _fsp_fetch_done(id, status, t);
      _free(t);
    };
    fetch(FSD.str(urlPtr), init)
      .then(async (res) => {
        if (wantProgress && res.body) {
          const total = Number(res.headers.get('content-length') || '') || -1;
          const reader = res.body.getReader();
          const parts = [];
          let got = 0;
          for (;;) {
            const { done, value } = await reader.read();
            if (done) break;
            parts.push(value);
            got += value.length;
            _fsp_fetch_progress(id, got, total);
          }
          const all = new Uint8Array(got);
          let at = 0;
          for (const p of parts) {
            all.set(p, at);
            at += p.length;
          }
          deliver(res.status, all, res.headers.get('content-type'));
        } else deliver(res.status, new Uint8Array(await res.arrayBuffer()), res.headers.get('content-type'));
      })
      .catch(() => deliver(0, null, ''));
  },
  /** which: 0 localStorage, 1 sessionStorage; returns 0 when there is nothing under the key */
  fsp_storage_get__deps: ['$FSD'],
  fsp_storage_get: function (which, key) {
    try {
      const v = (which ? sessionStorage : localStorage).getItem(FSD.str(key));
      return v === null ? 0 : FSD.out(v);
    } catch (e) {
      return 0;
    }
  },
  fsp_storage_set__deps: ['$FSD'],
  fsp_storage_set: function (which, key, value) {
    try {
      (which ? sessionStorage : localStorage).setItem(FSD.str(key), FSD.str(value));
      return 1;
    } catch (e) {
      return 0;
    }
  },
  fsp_storage_remove__deps: ['$FSD'],
  fsp_storage_remove: function (which, key) {
    try {
      (which ? sessionStorage : localStorage).removeItem(FSD.str(key));
    } catch (e) {
      /* the store is shut */
    }
  },
  fsp_now: function () {
    return performance.now();
  },
  fsp_date: function () {
    return Date.now();
  },
  fsp_timezone_offset: function (ms) {
    return new Date(ms).getTimezoneOffset();
  },
  fsp_timeout: function (id, ms) {
    setTimeout(() => _fsp_timer(id), ms);
  },
  fsp_soon: function (id) {
    queueMicrotask(() => _fsp_timer(id));
  },
  /** level: 0 log, 1 warn, 2 error */
  fsp_console__deps: ['$FSD'],
  fsp_console: function (level, text) {
    const s = FSD.str(text);
    if (level === 2) console.error(s);
    else if (level === 1) console.warn(s);
    else console.log(s);
  },
  /** what: 0 location.search, 1 the address without query and fragment, 2 navigator.languages joined with commas, 3 the user agent */
  fsp_text__deps: ['$FSD'],
  fsp_text: function (what) {
    if (what === 0) return FSD.out(location.search);
    if (what === 1) return FSD.out(location.href.replace(/[?#].*$/, '').replace(/[^/]*$/, ''));
    if (what === 2) return FSD.out((navigator.languages && navigator.languages.length ? navigator.languages : [navigator.language || 'en']).join(','));
    return FSD.out(navigator.userAgent);
  },
  fsp_replace_search__deps: ['$FSD'],
  fsp_replace_search: function (search) {
    try {
      history.replaceState(null, '', location.pathname + FSD.str(search) + location.hash);
    } catch (e) {
      /* a page opened from a file */
    }
  },
  fsp_reload: function () {
    location.reload();
  },
  fsp_open_tab__deps: ['$FSD'],
  fsp_open_tab: function (url) {
    window.open(FSD.str(url), '_blank', 'noopener');
  },
  fsp_copy_text__deps: ['$FSD'],
  fsp_copy_text: function (text) {
    try {
      navigator.clipboard.writeText(FSD.str(text)).catch(() => {});
    } catch (e) {
      /* no clipboard here */
    }
  },
  fsp_match_media__deps: ['$FSD'],
  fsp_match_media: function (query) {
    return window.matchMedia && matchMedia(FSD.str(query)).matches ? 1 : 0;
  },
  /** what: 0 hardwareConcurrency, 1 deviceMemory, 2 devicePixelRatio, 3 innerWidth, 4 innerHeight, 5 onLine */
  fsp_number: function (what) {
    switch (what) {
      case 0: return navigator.hardwareConcurrency || 8;
      case 1: return navigator.deviceMemory || 8;
      case 2: return window.devicePixelRatio || 1;
      case 3: return window.innerWidth;
      case 4: return window.innerHeight;
      case 5: return navigator.onLine === false ? 0 : 1;
    }
    return 0;
  },
  fsp_vibrate: function (ms) {
    if (navigator.vibrate) navigator.vibrate(ms);
  },
  /**
   * The gamepads: writes for each connected pad its slot, the number of axes and of buttons, then the axes and how far
   * each button is pressed, into `out` (Float64, at most `max` numbers); returns how many numbers were written.
   */
  fsp_gamepads: function (out, max) {
    const pads = navigator.getGamepads ? navigator.getGamepads() : [];
    let k = 0;
    const o = out >> 3;
    for (const p of pads) {
      if (!p || !p.connected) continue;
      if (k + 3 + p.axes.length + p.buttons.length > max) break;
      HEAPF64[o + k++] = p.index;
      HEAPF64[o + k++] = p.axes.length;
      HEAPF64[o + k++] = p.buttons.length;
      for (const a of p.axes) HEAPF64[o + k++] = a;
      for (const b of p.buttons) HEAPF64[o + k++] = b.value;
    }
    return k;
  },
  fsp_gamepad_id__deps: ['$FSD'],
  fsp_gamepad_id: function (index) {
    const pads = navigator.getGamepads ? navigator.getGamepads() : [];
    return FSD.out(pads[index] ? pads[index].id : '');
  },
});
