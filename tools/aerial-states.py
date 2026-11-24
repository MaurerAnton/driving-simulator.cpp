import json, sys, math
d = json.load(open(sys.argv[1]))
TOL = float(sys.argv[2]) if len(sys.argv) > 2 else 0.04
def dp(pts, tol):
    if len(pts) < 3: return pts
    a, b = pts[0], pts[-1]
    best, bi = -1, 0
    ax, ay = a; bx, by = b
    L = math.hypot(bx-ax, by-ay) or 1e-12
    for i in range(1, len(pts)-1):
        px, py = pts[i]
        dd = abs((bx-ax)*(ay-py)-(ax-px)*(by-ay))/L if L > 1e-12 else math.hypot(px-ax, py-ay)
        if dd > best: best, bi = dd, i
    if best <= tol: return [a, b]
    return dp(pts[:bi+1], tol)[:-1] + dp(pts[bi:], tol)
out = []
for f in d['features']:
    p = f['properties']
    if p.get('adm0_a3') != 'DEU': continue
    g = f['geometry']
    polys = g['coordinates'] if g['type'] == 'MultiPolygon' else [g['coordinates']]
    rings = []
    for poly in polys:
        ring = poly[0]
        # area in deg^2*cos
        area = abs(sum(ring[i][0]*ring[i+1][1]-ring[i+1][0]*ring[i][1] for i in range(len(ring)-1)))/2
        if area < 0.0008: continue  # small islands: left to the neighbours' margin
        # split ring in two halves for DP of closed ring
        n = len(ring)
        s = dp(ring[:n//2+1], TOL)[:-1] + dp(ring[n//2:], TOL)[:-1]
        rings.append(s)
    out.append((p.get('name'), p.get('iso_3166_2'), rings))
for name, iso, rings in sorted(out, key=lambda r: r[1]):
    print(iso, name, [len(r) for r in rings], file=sys.stderr)
    body = ', '.join('{' + ', '.join(f'{x:.3f}, {y:.3f}' for x, y in r) + '}' for r in rings)
    print(f'  {{"{iso}", {{{body}}}}},')
