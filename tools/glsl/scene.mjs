// SPDX-License-Identifier: AGPL-3.0-only
// The materials of what stands in the scene: the back of the sign plates (src/render/signs.ts) and the
// far ground (src/render/far.ts).
export async function captures({ load, THREE }) {
  // the sign renderer makes the canvas of its atlas when it is constructed: one that takes the calls and paints nothing
  const ctx = new Proxy({}, { get: () => () => {}, set: () => true });
  const had = Object.getOwnPropertyDescriptor(globalThis, 'document');
  globalThis.document = { createElement: () => ({ width: 0, height: 0, getContext: () => ctx }) };
  try {
    const { SignRenderer } = await load('src/render/signs.ts');
    const { FarTerrain } = await load('src/render/far.ts');
    const signs = new SignRenderer('', 1);
    const far = new FarTerrain();
    return [
      ['signBack', signs.backMaterial],
      // (`material` is private in the TypeScript, an ordinary field when it runs)
      ['far', far.material],
    ];
  } finally {
    if (had) Object.defineProperty(globalThis, 'document', had);
    else delete globalThis.document;
  }
}
