// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The rest of what the browser gives the game (platform/frame.h, sensors.h,
 * geolocation.h, and window.confirm and the bytes downloaded of ui/hud_parts.h).
 * Answers that come later go back to the C++ through _fsx_* functions by an id.
 */
addToLibrary({
  fsx_raf__deps: ['fsx_frame'],
  fsx_raf: function (id) {
    return requestAnimationFrame((now) => _fsx_frame(id, now));
  },
  fsx_caf: function (handle) {
    cancelAnimationFrame(handle);
  },
  fsx_debug: function (text) {
    console.debug(UTF8ToString(text));
  },
  fsx_orientation_known: function () {
    return typeof window.DeviceOrientationEvent !== 'undefined' ? 1 : 0;
  },
  fsx_orientation_asks: function () {
    return typeof window.DeviceOrientationEvent !== 'undefined' && typeof DeviceOrientationEvent.requestPermission === 'function' ? 1 : 0;
  },
  fsx_orientation_permission__deps: ['fsx_text_done', '$stringToNewUTF8', 'free'],
  fsx_orientation_permission: function (id) {
    const done = (text) => {
      const p = stringToNewUTF8(text);
      _fsx_text_done(id, p);
      _free(p);
    };
    try {
      DeviceOrientationEvent.requestPermission().then((r) => done(String(r)), () => done(''));
    } catch (e) {
      setTimeout(() => done(''));
    }
  },
  /** the pads as lines: index \t id \t mapping \t axes (comma) \t buttons (pressed:value, comma) */
  fsx_gamepads__deps: ['$stringToNewUTF8'],
  fsx_gamepads: function () {
    let text = '';
    const pads = navigator.getGamepads ? navigator.getGamepads() : [];
    for (const g of pads) {
      if (!g || !g.connected) continue;
      const clean = (s) => String(s).replace(/[\t\n]/g, ' ');
      text += g.index + '\t' + clean(g.id) + '\t' + clean(g.mapping) + '\t' + Array.from(g.axes).join(',') + '\t' +
        Array.from(g.buttons).map((b) => (b.pressed ? 1 : 0) + ':' + b.value).join(',') + '\n';
    }
    return stringToNewUTF8(text);
  },
  fsx_has_geolocation: function () {
    return navigator.geolocation ? 1 : 0;
  },
  fsx_geolocate__deps: ['fsx_geo_done'],
  fsx_geolocate: function (id, high, timeout, maximumAge) {
    const opts = { enableHighAccuracy: !!high, maximumAge: maximumAge };
    if (timeout < 1e15) opts.timeout = timeout;
    try {
      navigator.geolocation.getCurrentPosition(
        (p) => _fsx_geo_done(id, 0, p.coords.latitude, p.coords.longitude, p.coords.accuracy),
        (e) => _fsx_geo_done(id, (e && e.code) || 2, 0, 0, 0),
        opts,
      );
    } catch (e) {
      setTimeout(() => _fsx_geo_done(id, 2, 0, 0, 0));
    }
  },
  fsx_confirm: function (text) {
    try {
      return window.confirm(UTF8ToString(text)) ? 1 : 0;
    } catch (e) {
      return 0;
    }
  },
  /** what the page has fetched so far (bytes over the wire: the browser's resource timing) */
  fsx_downloaded: function () {
    let n = 0;
    try {
      for (const e of performance.getEntriesByType('navigation')) n += e.transferSize || 0;
      for (const e of performance.getEntriesByType('resource')) n += e.transferSize || 0;
    } catch (e) {}
    return n;
  },
});
