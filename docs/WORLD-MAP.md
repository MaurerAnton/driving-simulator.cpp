# One map for all regions (new in 2.0)

In 1.7.7 the big map shows the one region that is loaded; another region is chosen in the
settings. In 2.0 the big map – opened from the display, the mini map or the M key, as before –
shows every region there is, and the player picks one right there.

## What the player sees

- Zooming out (wheel, pinch, the − button, as before) no longer stops at the loaded
  region: the map goes out as far as Europe on a phone (`WorldLayer::MIN_SCALE`). Beneath the
  map's own roads lie land and sea, the borders and every region as a box with its name (a dot
  where a box would be too small). Below `WorldLayer::WORLD_SCALE` the world is the whole picture
  and a card says "Tap a region to choose it", with "Back to <name>".
- The globe in the map's bar ("All regions"; the Home key) shows every region at once; pressed
  again it returns to the view before.
- A tap on another region selects it and shows its card: its name, its country, how much there is
  to download (where known) and "Open this map". A tap on the loaded region, "Back to <name>" or
  the sea (which only unselects) bring the player back. The card stands in the map's column, clear
  of the bars; its buttons are 44 px or more, and the open button takes the focus (Enter opens).
- The search field also finds regions, by their name or their country, listed after the places
  of the loaded map ("Wien, Austria · Region"); choosing one shows it on the world and selects it.

## What BigMap gained

All of it is marked `// 2.0:` in `src/ui/bigmap.{h,cpp}`; without a layer the map is 1.7.7's.

- `BigMap(BigMapHost host, std::shared_ptr<WorldLayer> world = nullptr)`, and `setWorld(layer)`
  for a layer that comes later. A layer still loading is waited for; the map redraws when it is
  ready. It is used once `ready()` and `hasRegions()`.
- `showAllRegions()` (the globe), `showRegion(id)` (select and show one; the loaded one: back
  into it), `chosenRegion()`.
- `BigMapHost::region` – the id of the loaded region (`""`, `WorldLayer::SITE`: the site's own
  map) – and `BigMapHost::onOpenRegion(const Region&)`.
- `searchRegions` in `src/ui/placesearch.h`; `WorldLayer::setDownloadBytes` for the card's size.
- Texts: the `world…` keys in `i18n-extra/<lang>.json` (28 languages); style: `web/css/world.css`.

## The callback

"Open this map" calls `host.onOpenRegion(region)` with a copy of the region. The game saves its id
in the settings (`Settings.region`, as the settings' region choice does) and loads it; the map may be
closed and destroyed during the call. The game makes one `WorldLayer`, calls
`load(siteBase, siteMapBox, siteName)` (or `set(...)` with the index it has read itself) and hands
the same layer to each BigMap it opens.
