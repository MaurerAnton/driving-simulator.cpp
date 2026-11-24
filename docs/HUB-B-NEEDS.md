# What hub-b needs of `src/game/game.h`

(hub-b: `src/game/game_trips.cpp`, `src/game/game_map.cpp`.  One line each: name, the type as it is needed, game.ts line.
Kept current; a line goes when the header has it.)

## Open

- `parkedAt` – `std::optional<Place> parkedAt;` – game.ts:4867 (takeOverCar sets it at 3305 – where the car taken over stands, as it stands; `begin` reads it at 2771 and clears it). Listed in game.h under "later" with spawnCars; takeOverCar needs the field itself. (`intoParked` stays out: it only keeps a drive from beginning on foot.)
- `worldLayer` – `std::shared_ptr<WorldLayer> worldLayer;` (and `class WorldLayer;` beside `class AddressIndex;`) – 2.0, no game.ts line: openMap makes the layer of all regions once and hands it to the BigMap (`setWorld`, docs/WORLD-MAP.md of w/maps); `onOpenRegion` stays where hub-a makes the map (game.cpp)
