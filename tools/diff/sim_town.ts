// SPDX-License-Identifier: AGPL-3.0-only
/**
 * The test town of the original's tests (tests/game/town.ts) as a file, and its lane graph as
 * numbers – for seeing whether the map layer of the port gives the simulation the same roads:
 *
 *   node tools/diff/sim_town.ts <path of the original> <out: world.json> [town] > a.txt
 *   build/sim_town_dump <world.json> > b.txt
 *   tools/diff/compare.py a.txt b.txt
 *
 * `town`: plain | lights | wide (tools/diff/sim_towns.ts).
 */
import { writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { pathToFileURL } from 'node:url';
import { strictMath } from './sim_math.ts';
import { townOptions } from './sim_towns.ts';

strictMath();

const ROOT = process.argv[2] ?? process.cwd();
const OUT = process.argv[3] ?? 'world.json';
const WHICH = process.argv[4] ?? 'plain';
const at = (p: string): string => pathToFileURL(join(ROOT, p)).href;
const { makeTown } = await import(at('tests/game/town.ts'));

/* eslint-disable @typescript-eslint/no-explicit-any */
const town = makeTown(townOptions(WHICH));
writeFileSync(OUT, JSON.stringify(town.json));

const out: string[] = [];
const g: any = town.lanes;
g.resolveAll();
for (const l of g.lanes) {
  if (!l) continue;
  out.push(
    `lane ${l.id} ${l.edge.id} ${l.dir} ${l.idx} ${l.count} ${l.off} ${l.width} ${l.s0} ${l.s1} ${l.len} ${l.from.id} ${l.to.id} ${Number.isFinite(l.limit) ? l.limit : -1} ${l.stopAt} out ${l.out.map((c: any) => c.id).join(' ')} in ${l.in.map((c: any) => c.id).join(' ')}`,
  );
}
for (const c of g.connectors) {
  if (!c) continue;
  out.push(`conn ${c.id} ${c.node.id} ${c.from.id} ${c.to.id} ${c.turn} ${c.len} pts ${Array.from(c.pl.pts).join(' ')}`);
  for (const k of c.conflicts) out.push(`conflict ${c.id} ${k.other.id} ${k.sSelf} ${k.sOther} ${k.merge ? 1 : 0} ${k.yield ? 1 : 0}`);
  for (const s of c.siblings) out.push(`sibling ${c.id} ${s.other.id} ${s.until} ${s.untilOther}`);
}
for (const e of town.world.edges) {
  if (!e) continue;
  out.push(`edge ${e.id} ${e.a.id} ${e.b.id} ${e.len} ${e.cutA} ${e.cutB} ${e.width} ${e.tier} ${e.blendA} ${e.blendB}`);
}
for (const n of town.world.nodes) {
  if (!n) continue;
  out.push(`node ${n.id} ${n.ctrl} ${n.sc} arms ${n.arms.map((a: any) => `${a.edge.id}/${a.role}/${a.sg}/${a.stopS}/${a.cut}/${a.dx}/${a.dz}/${a.isApproach ? 1 : 0}/${a.isExit ? 1 : 0}`).join(' ')}`);
}
process.stdout.write(out.join('\n') + '\n');
