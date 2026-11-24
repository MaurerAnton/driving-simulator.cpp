// SPDX-License-Identifier: AGPL-3.0-only
// The materials of the map's tiles (src/render/materials.ts): ground, paint, terrain, buildings.
export async function captures({ load, THREE }) {
  const mats = await load('src/render/materials.ts');
  const tex = new THREE.DataTexture(new Uint8Array(4), 1, 1);
  return [
    ['ground', mats.makeGroundMaterial(false)],
    ['paint', mats.makeGroundMaterial(true)],
    ['terrain', mats.makeTerrainMaterial(tex)],
    ['building', mats.makeBuildingMaterial(false)],
    ['buildingPlain', mats.makeBuildingMaterial(true)],
  ];
}
