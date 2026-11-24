# Aerial photographs on the maps

`src/reality/aerial.{h,cpp}` translates 1.7.7's `src/reality/aerial.ts`: the Hessian orthophotos (DOP20)
over WMS, their tile grid in UTM 32N and the tile cache (four requests at a time, the most recently used
pictures kept). 2.0 extends the same layer to all of Germany and beyond.

The layer is switched on and off with the box on the big map's bottom bar ("Aerial photos"), and in the
settings (`settings.reality.aerial`), as in 1.7.7. 1.7.7 has it **on** by default (since 1.4.0,
`settings.ts`); the port keeps that default. The player's browser fetches every picture straight from
the service. Each service learns the IP address and the area looked at. The settings' note
(`mapAerialNote`) says this.

## How a picture is chosen

The map's world frame is ETRS89 / UTM 32N (EPSG:25832) for every region (origin E 528000 / N 5678000).
`drawAerial()` (called by the big map from `src/ui/bigmap_aerial.cpp`) draws in this order:

1. **Sentinel-2 cloudless 2016** everywhere.
   - Web Mercator tiles (256 px, zoom 0–14), chosen by `s2Zoom(px per metre, latitude)` and limited to about a
     screenful (≤ 40 tiles).
   - A tile 3 zooms coarser goes first, so something shows at once.
   - Each tile is laid onto the UTM frame by the affine map of its corners. Coarse tiles go on in 4×4 or 2×2
     pieces, each with its own corners, so the curvature between the two projections does not show.
   - Close up, when every part of the view lies in a state with pictures, only the coarse Sentinel ground is
     fetched.
2. **The states' orthophotos** over it.
   - The 1.7.7 grid of UTM tiles: 512 px, level 0 = 0.2 m/px … level 8 = 51.2 m/px, ≤ 30 tiles, a coarse
     level first.
   - Each tile is asked of the state it lies in (`aerialStateTiles`). The states' outlines are Natural Earth
     admin-1, simplified to about 3 km (`src/reality/aerialstates.cpp`, made by `tools/aerial-states.py`).
   - A tile's corners are pushed out by 3 km before the test. A tile that touches more than one state, or
     leaves the states, is asked of each state it touches, as PNG with `TRANSPARENT=TRUE`, so the outside
     stays clear.
   - The states' pictures fade in by zoom: full up to level 6 (12.8 m/px), half at level 7, none at level 8
     and beyond, where Sentinel alone is drawn.
3. The roads are drawn over the pictures, faint (as in 1.7.7).

With the world layer (zoomed out beyond the region), the pictures are drawn on the land, under the borders and
the regions' marks (`WorldLayer::draw`'s `underlay`).

The corner of the big map lists the credits of every source on the screen.

The corner map (`ui/minimap.cpp`) uses the same per-state tiles, so a region's corner map has pictures
anywhere in Germany.

## Sources

All services checked on 2026-11-24 with these tests:

- `GetCapabilities`;
- one 512×512 `GetMap` in EPSG:25832;
- whether the response carries `Access-Control-Allow-Origin` (the pictures are loaded with
  `crossOrigin="anonymous"`, so without it they cannot be used).

| Area | Service (WMS GetMap base) | Layer | Licence | Credit shown |
| --- | --- | --- | --- | --- |
| Everywhere (far out, abroad) | `https://tiles.maps.eox.at/wmts/1.0.0/s2cloudless_3857/default/g/{z}/{y}/{x}.jpg` (WMTS) | `s2cloudless_3857` (2016) | CC BY 4.0 | Sentinel-2 cloudless – https://s2maps.eu by EOX IT Services GmbH (Contains modified Copernicus Sentinel data 2016), CC BY 4.0 |
| Baden-Württemberg | `https://owsproxy.lgl-bw.de/owsproxy/ows/WMS_LGL-BW_ATKIS_DOP_20_C` | `IMAGES_DOP_20_RGB` | dl-de/by-2.0 | LGL-BW (2026) Datenlizenz Deutschland – Namensnennung – Version 2.0, www.lgl-bw.de |
| Bayern | `https://geoservices.bayern.de/od/wms/dop/v1/dop20` (WMS **1.1.1**) | `by_dop20c` | CC BY 4.0 | © Bayerische Vermessungsverwaltung – www.geodaten.bayern.de, CC BY 4.0 |
| Berlin | `https://gdi.berlin.de/services/wms/dop_2025_fruehjahr` | `dop_2025` | dl-de/zero-2.0 | Geoportal Berlin / Digitale farbige Orthophotos 2025 (DOP20RGBI), dl-de/zero-2.0 |
| Brandenburg | `https://isk.geobasis-bb.de/mapproxy/dop20c/service/wms` | `bebb_dop20c` | dl-de/by-2.0 | © GeoBasis-DE/LGB, dl-de/by-2-0 (Daten geändert) |
| Bremen | `https://geodienste.bremen.de/wms_dop20_2023` | `DOP20_2023_HB,DOP20_2023_BHV` | CC BY 4.0 | © GeoBasis-DE / Landesamt GeoInformation Bremen 2023, CC BY 4.0 |
| Hessen | `https://www.gds-srv.hessen.de/cgi-bin/lika-services/ogc-free-images.ows` | `he_dop_rgb` | § 24 HVGG: any use without restriction or condition | Luftbilder: © Hessische Verwaltung für Bodenmanagement und Geoinformation (as 1.7.7) |
| Mecklenburg-Vorpommern | `https://www.geodaten-mv.de/dienste/adv_dop` | `mv_dop` | CC BY 4.0 | © GeoBasis-DE/M-V 2026, CC BY 4.0 |
| Niedersachsen | `https://opendata.lgln.niedersachsen.de/doorman/noauth/dop_wms` | `ni_dop20` | CC BY 4.0 | © GeoBasis-DE / LGLN (2026), CC BY 4.0 |
| Nordrhein-Westfalen | `https://www.wms.nrw.de/geobasis/wms_nw_dop` | `nw_dop_rgb` | dl-de/zero-2.0 | Land NRW (2026), dl-de/zero-2.0 (a courtesy; none is required) |
| Rheinland-Pfalz | `https://geo4.service24.rlp.de/wms/rp_dop20.fcgi` | `rp_dop20` | dl-de/by-2.0 | ©GeoBasis-DE / LVermGeoRP (2026), dl-de/by-2-0, http://www.lvermgeo.rlp.de [Daten bearbeitet] |
| Saarland | `https://geoportal.saarland.de/freewms/dop` | `sl_dop` | dl-de/by-2.0 (capabilities: no fees, "noLimitations") | © GeoBasis DE/LVGL-SL (2026), dl-de/by-2-0 |
| Sachsen | `https://geodienste.sachsen.de/wms_geosn_dop-rgb/guest` | `sn_dop_020` | dl-de/by-2.0 (capabilities: "kostenfrei") | © GeoBasis-DE / GeoSN, dl-de/by-2-0 |
| Schleswig-Holstein | `https://dienste.gdi-sh.de/WMS_SH_DOP20col_OpenGBD` | `sh_dop20_rgb` | CC BY 4.0 | © GeoBasis-DE/LVermGeo SH/CC BY 4.0 |
| Thüringen | `https://www.geoproxy.geoportal-th.de/geoproxy/services/DOP` | `th_dop` | CC BY 4.0 | © GDI-Th, CC BY 4.0 |

All the states' services offer EPSG:25832, so their pictures need no reprojection. Bayern speaks WMS 1.1.1
only (`SRS=`); the others speak 1.3.0 (`CRS=`). In both, the BBOX is east, north, east, north.

### Sentinel-2: why the 2016 mosaic

EOX publishes Sentinel-2 cloudless for 2016 and 2018–2025. Only **2016** is under CC BY 4.0. The service's
capabilities and https://s2maps.eu give **CC BY-NC-SA 4.0** for 2018 and later. The game is AGPL and may be
used commercially, so a non-commercial licence does not fit. The capabilities also mark a "2017" layer CC BY
4.0, but s2maps.eu does not list it as a release, so it is not used. The credit is the text EOX asks for
("Sentinel-2 cloudless – https://s2maps.eu by EOX IT Services GmbH (Contains modified Copernicus Sentinel
data 2016)").

### Not used

The owner's rule for sources that are not free: do not use them silently. Give links and free alternatives
instead, and support local files. The following are not used; Sentinel-2 shows in their place:

- **Hamburg.** The LGV's orthophoto services are the time series `geodienste.hamburg.de/wms_dop_zeitreihe_unbelaubt`
  and `…_belaubt` (the former `HH_WMS_DOP` is gone). Their licence is dl-de/by-2.0, but the same capabilities
  list "Andere geschlossene Lizenz" for satellite pictures in the series ("© 2023 Maxar Technologies",
  "Pléiades © CNES (2022), Distribution Airbus DS"), and they do not say which years those are. Until a year
  is known to be the LGV's own aerial flight (then add `TIME=<year>` and the entry), the service is not used.
  Pictures: <https://geoportal-hamburg.de>.
- **Sachsen-Anhalt.** Free (dl-de/by-2.0, `https://www.geodatenportal.sachsen-anhalt.de/wss/service/ST_LVermGeo_DOP_WMS_OpenData/guest`,
  layer `lsa_lvermgeo_dop20_2`). However, when asked with an `Origin` header, the service answers with
  **two** `Access-Control-Allow-Origin` headers (`*` and the origin), and browsers refuse that. Its pictures
  cannot be drawn into the map's canvas. It needs the operator to fix the header, or a proxy.
  Pictures: <https://www.lvermgeo.sachsen-anhalt.de/de/gdp-open-data.html>.
- **The federal BKG** (`sgx.geodatenzentrum.de/wms_dop`): not public. It is for federal and state
  authorities only and answers 403.

Both lists are also in the code: `AERIAL_STATES()` and `AERIAL_NOT_FREE()` in `src/reality/aerial.cpp`.
`tests/aerial_test.cpp` checks them.

Wording still to confirm with the states' own pages. The licence is clear for each; only the exact credit
text is unconfirmed:

- Bayern's credit text (taken from a Bavarian guide, not the capabilities);
- Sachsen's and Saarland's credit text (taken from catalogue records);
- Bremen's credit text (taken from its 2024 terms of use).

## Not in 2.0

- 1.7.7's reality view (`ui/reality.ts`, "aerial" button → `openMap(true)`) is a later part of the port.
  `?map=aerial` opens the close-up map with the pictures on, as 1.7.7 does (`game_frame.cpp`).
- Local files: neither 1.7.7's `reality/local.ts` nor `reality/imagemeta.ts` is used by the aerial layer.
  Both are about street photos. Every source used here is free, so no local-file fallback is needed for it.
