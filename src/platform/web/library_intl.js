// SPDX-License-Identifier: AGPL-3.0-only
/**
 * platform/intl.h in the browser: the real Intl (numbers and quantities, dates,
 * lists) and the keyboard's layout.  The options arrive as the JSON of Intl's
 * options object; the answer is a text for the C++ (which frees it), or 0 with
 * the error's message left for fsi_error().
 */
addToLibrary({
  $FSI__deps: ['$UTF8ToString', '$stringToNewUTF8'],
  $FSI: {
    error: '',
    /** the formatters made so far, by kind + locale + options (making one is slow, using one is not) */
    made: new Map(),
    get(kind, locale, options) {
      const key = kind + '|' + locale + '|' + options;
      let f = FSI.made.get(key);
      if (!f) {
        const o = JSON.parse(options);
        f = kind === 'n' ? new Intl.NumberFormat(locale, o) : kind === 'd' ? new Intl.DateTimeFormat(locale, o) : new Intl.ListFormat(locale, o);
        if (FSI.made.size > 400) FSI.made.clear();
        FSI.made.set(key, f);
      }
      return f;
    },
    run(fn) {
      try {
        FSI.error = '';
        return stringToNewUTF8(fn());
      } catch (e) {
        FSI.error = String((e && e.message) || e);
        return 0;
      }
    },
  },
  fsi_error__deps: ['$FSI'],
  fsi_error: function () {
    return stringToNewUTF8(FSI.error);
  },
  fsi_number__deps: ['$FSI'],
  fsi_number: function (value, locale, options) {
    return FSI.run(() => FSI.get('n', UTF8ToString(locale), UTF8ToString(options)).format(value));
  },
  fsi_date__deps: ['$FSI'],
  fsi_date: function (ms, locale, options) {
    return FSI.run(() => FSI.get('d', UTF8ToString(locale), UTF8ToString(options)).format(new Date(ms)));
  },
  /** `items`: the texts joined with a line feed between them (none of them holds one) */
  fsi_list__deps: ['$FSI'],
  fsi_list: function (items, count, locale, options) {
    return FSI.run(() => FSI.get('l', UTF8ToString(locale), UTF8ToString(options)).format(count ? UTF8ToString(items).split('\n') : []));
  },
  /** asks for the keyboard's layout; _fsi_keyboard_done(id, text) gets "code\tkey\n…" – or 0 where there is none */
  fsi_keyboard__deps: ['$FSI', 'free'],
  fsi_keyboard: function (id) {
    const none = () => _fsi_keyboard_done(id, 0);
    try {
      if (!navigator.keyboard || !navigator.keyboard.getLayoutMap) return void setTimeout(none);
      navigator.keyboard.getLayoutMap().then((map) => {
        let text = '';
        map.forEach((key, code) => {
          text += code + '\t' + key + '\n';
        });
        const p = stringToNewUTF8(text);
        _fsi_keyboard_done(id, p);
        _free(p);
      }, none);
    } catch (e) {
      setTimeout(none);
    }
  },
});
