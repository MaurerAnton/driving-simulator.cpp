// SPDX-License-Identifier: AGPL-3.0-only
// One headless browser at a time on this machine (each takes about a gigabyte with the graphics chip, and several
// agents take pictures at once: the tablet ran out of memory).  `await takeBrowserLock()` waits its turn; the lock
// goes when the process ends (or after 15 minutes, should one have died without letting go).
import { openSync, closeSync, unlinkSync, statSync, writeSync } from 'node:fs';

const LOCK = '/tmp/fahrsim-browser.lock';

export async function takeBrowserLock() {
  for (let waited = 0; ; waited++) {
    try {
      const fd = openSync(LOCK, 'wx');
      writeSync(fd, String(process.pid));
      closeSync(fd);
      const free = () => {
        try {
          unlinkSync(LOCK);
        } catch {}
      };
      process.on('exit', free);
      for (const s of ['SIGINT', 'SIGTERM']) process.on(s, () => process.exit(1));
      return;
    } catch {
      try {
        if (Date.now() - statSync(LOCK).mtimeMs > 15 * 60 * 1000) unlinkSync(LOCK);
      } catch {}
      if (waited === 0) console.error('[browser] waiting for another headless browser on this machine to finish …');
      await new Promise((r) => setTimeout(r, 2000));
    }
  }
}
