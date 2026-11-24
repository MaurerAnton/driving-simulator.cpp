# What hub-b needs of `src/game/game.h`

(hub-b: `src/game/game_trips.cpp`, `src/game/game_map.cpp`.  One line each: name, the type as it is needed, game.ts line.
Kept current; a line goes when the header has it.)

## Open

- `AddressHit` – game.h declares `struct AddressHit;`, but game/address.h has `using AddressHit = StartAddress;` (game/settings.h): the two collide in every file that includes both. Needed: `using AddressHit = StartAddress;` in game.h (it includes game/settings.h already) – game.ts:73
- `footGround` – `double footGround(double x, double z, double yRef);` – game.ts:5156 (teleportHere calls it at 5322; hub-a's signsBesideRoad at 5150)
- `parkedAt` – `std::optional<Place> parkedAt;` – game.ts:4867 (takeOverCar sets it at 3305 – where the car taken over stands, as it stands; `begin` reads it at 2771 and clears it). Listed in game.h under "later" with spawnCars; takeOverCar needs the field itself. (`intoParked` stays out: it only keeps a drive from beginning on foot.)
