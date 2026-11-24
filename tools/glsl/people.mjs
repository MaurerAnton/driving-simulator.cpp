// SPDX-License-Identifier: AGPL-3.0-only
// The material of the people on foot (src/render/people.ts): the colours of each piece by its slot, the
// pieces of the variants a person does not wear folded away.
export async function captures({ load }) {
  const { PeopleRenderer } = await load('src/render/people.ts');
  const r = new PeopleRenderer(1);
  // (BODY is not exported: every part of the body is drawn with it)
  return [['people', r.group.children[0].material]];
}
