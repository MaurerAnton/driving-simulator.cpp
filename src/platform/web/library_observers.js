// SPDX-License-Identifier: AGPL-3.0-only
/** platform/observers.h in the browser: ResizeObserver, MutationObserver, offsetParent. */
addToLibrary({
  $FSO: { list: [null] },
  fso_create__deps: ['$FSO', 'fso_fire'],
  fso_create: function (kind, id) {
    const fire = () => _fso_fire(id);
    const o = kind === 0 ? new ResizeObserver(fire) : new MutationObserver(fire);
    FSO.list[id] = { o: o, kind: kind };
  },
  fso_observe__deps: ['$FSO', '$FSD'],
  fso_observe: function (id, h, filter) {
    const it = FSO.list[id];
    const el = FSD.el(h);
    if (!it || !el || el === window) return;
    if (it.kind === 0) it.o.observe(el);
    else {
      const names = UTF8ToString(filter);
      const opts = { attributes: true };
      if (names) opts.attributeFilter = names.split('\n');
      it.o.observe(el, opts);
    }
  },
  fso_disconnect__deps: ['$FSO'],
  fso_disconnect: function (id) {
    const it = FSO.list[id];
    if (!it) return;
    it.o.disconnect();
    FSO.list[id] = null;
  },
  fso_offset_parent__deps: ['$FSD'],
  fso_offset_parent: function (h) {
    const el = FSD.el(h);
    return el && el !== window && el.offsetParent ? FSD.handle(el.offsetParent) : 0;
  },
});
