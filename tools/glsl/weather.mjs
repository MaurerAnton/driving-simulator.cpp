// SPDX-License-Identifier: AGPL-3.0-only
// The weather and the seasons in the materials (src/render/season.ts SeasonLook.patch, src/render/weather.ts
// WeatherRenderer.addWetness, src/render/dirt.ts DirtLook.apply): each material the game teaches them, as the
// original's patches leave it.  The name of each is the program key the C++ puts together – the material's own key
// (its patch's; '' for one without), then the season's, then the wet's – so that render/season.cpp,
// render/weather.cpp and render/dirt.cpp find them.  (A material without a key of its own is given '' before it is
// patched: the original's key would be the text of three.js's empty onBeforeCompile; the shaders are the same.)
export async function captures({ load, THREE }) {
  const mats = await load('src/render/materials.ts');
  const { SeasonLook, SEASON_PRESETS } = await load('src/render/season.ts');
  const { WeatherRenderer } = await load('src/render/weather.ts');
  const { FarTerrain } = await load('src/render/far.ts');
  const { DirtLook } = await load('src/render/dirt.ts');
  const tex = new THREE.DataTexture(new Uint8Array(4), 1, 1);
  const look = new SeasonLook();
  // (the bare crowns' see-through variant: what a look in winter compiles)
  const thin = new SeasonLook();
  thin.set(SEASON_PRESETS.winter);
  const wet = new WeatherRenderer(1);
  const plain = () => {
    const m = new THREE.MeshLambertMaterial({ vertexColors: true });
    m.customProgramCacheKey = () => '';
    return m;
  };
  const out = [];
  const season = (m, target, l = look) => {
    l.patch(m, target);
    out.push([m.customProgramCacheKey(), m]);
    return m;
  };
  const wetted = (m, target) => {
    look.patch(m, target);
    wet.addWetness(m);
    out.push([m.customProgramCacheKey(), m]);
  };
  season(mats.makeTerrainMaterial(tex), 'terrain');
  // (`material` is private in the TypeScript, an ordinary field when it runs)
  season(new FarTerrain().material, 'far');
  season(mats.makeGroundMaterial(false), 'ground');
  wetted(mats.makeGroundMaterial(false), 'ground');
  season(mats.makeGroundMaterial(true), 'paint');
  wetted(mats.makeGroundMaterial(true), 'paint');
  season(mats.makeBuildingMaterial(false), 'building');
  season(mats.makeBuildingMaterial(true), 'building');
  season(plain(), 'struct');
  for (const kind of ['deciduous', 'orchard', 'evergreen']) season(plain(), kind);
  for (const kind of ['deciduous', 'orchard']) season(plain(), kind, thin);
  // the dirt on the player's vehicle: the outside's Lambert and Phong materials (none of them has a patch of its own)
  const lambert = new THREE.MeshLambertMaterial({ color: 0x808080 });
  const phong = new THREE.MeshPhongMaterial({ color: 0x808080 });
  for (const m of [lambert, phong]) m.customProgramCacheKey = () => '';
  const root = new THREE.Group();
  root.add(new THREE.Mesh(new THREE.BufferGeometry(), lambert), new THREE.Mesh(new THREE.BufferGeometry(), phong));
  new DirtLook().apply({ root });
  out.push(['lambert|dirt', lambert], ['phong|dirt', phong]);
  return out;
}
