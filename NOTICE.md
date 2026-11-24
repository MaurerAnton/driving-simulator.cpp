# Rechtliche Hinweise / Notices

## Programm

Fahrsim Baunatal – Copyright (C) 2026 die Mitwirkenden des Projekts.

Dieses Programm ist freie Software: Sie können es unter den Bedingungen der **GNU Affero General Public
License, Version 3** (ausschließlich dieser Version, SPDX `AGPL-3.0-only`), wie von der Free Software
Foundation veröffentlicht, weitergeben und/oder verändern. Es wird in der Hoffnung verbreitet, dass es
nützlich ist, aber OHNE JEDE GEWÄHRLEISTUNG. Den vollständigen Text enthält die Datei [LICENSE](LICENSE).

Wer eine veränderte Fassung über ein Netzwerk zur Benutzung anbietet, muss den Benutzerinnen und Benutzern
den zugehörigen Quelltext zugänglich machen (§ 13 AGPL). Der Quelltext dieser Fassung (Fahrsim Baunatal 2, die
C++-Fassung): <https://github.com/MaurerAnton/driving-simulator.cpp>; die ursprüngliche Fassung (TypeScript), aus der
sie übersetzt ist: <https://github.com/MaurerAnton/driving-simulator>.

## Daten in `public/map`

Die Kartendaten sind eine aus OpenStreetMap abgeleitete Datenbank und stehen unter der **Open Database
License 1.0** – © OpenStreetMap-Mitwirkende, <https://www.openstreetmap.org/copyright>.

Geländehöhen (DGM1) und Gebäude (3D-Gebäudemodelle LoD2): Hessische Verwaltung für Bodenmanagement und
Geoinformation, offene Geobasisdaten nach § 18 HVGG („jede Nutzung ohne Einschränkung oder Bedingung“).

Fahrpläne der Straßenbahnen, RegioTrams und Busse (`timetable.json.gz`): gtfs.de – GTFS für Deutschland
(<https://gtfs.de>), Daten bereitgestellt von DELFI e.V. (<https://www.delfi.de>), mit Beiträgen von
OpenStreetMap-Mitwirkenden; Lizenz **CC BY 4.0** (<https://creativecommons.org/licenses/by/4.0/>) – für das
Spiel auf die Straßenbahnen und RegioTrams in Kassel und die Busse in Nordhessen gekürzt und umgerechnet.

Die Herleitung beschreibt [docs/MAP_DATA.md](docs/MAP_DATA.md).

## Verkehrszeichen in `public/signs`

Die Verkehrszeichen der StVO sind amtliche Werke und nach § 5 Abs. 1 UrhG gemeinfrei. Die Vektorgrafiken
stammen von Wikimedia Commons; Datei, Quelle und Lizenzangabe jedes Zeichens stehen in
`public/signs/manifest.json`.

## Straßenfotos in der Realansicht

Das Projekt enthält und verteilt keine Straßenfotos. Die Realansicht lädt sie auf Wunsch der Spielerin oder
des Spielers direkt im Browser von

- [Panoramax](https://panoramax.fr) (föderiert, u. a. OpenStreetMap France und IGN) – CC BY-SA 4.0 bzw.
  Licence Ouverte 2.0, je nach Instanz;
- [KartaView](https://kartaview.org) – CC BY-SA 4.0;
- [Mapillary](https://www.mapillary.com) (Meta) – CC BY-SA 4.0, Abruf mit eigenem Zugangsschlüssel

und zeigt sie mit Urheber, Lizenz und Link zum Original an. Eigene Bilder der Nutzer (etwa selbst
gespeicherte Street-View-Panoramen) werden nur lokal im Browser gelesen; für deren Rechte sind die Nutzer
selbst verantwortlich. Der Ordner `public/panos/` ist deshalb von der Versionsverwaltung ausgenommen.

## Luftbilder auf den Karten

Das Projekt enthält und verteilt keine Luftbilder. Die große Karte, die runde Karte in der Ecke und der
Bildschirm im Auto zeigen sie (seit 1.4.0 von Anfang an, abschaltbar in den Einstellungen und auf der großen
Karte); sie werden dann direkt im eigenen Browser vom WMS-Dienst der Hessischen Verwaltung für
Bodenmanagement und Geoinformation geladen (Digitale Orthophotos DOP20, <https://hvbg.hessen.de>): offene
Geobasisdaten, „jede Nutzung … ohne Einschränkung oder Bedingung erlaubt“ (§ 24 HVGG). Der Server erfährt
dabei die IP-Adresse und den gezeigten Ausschnitt – das sagen auch die Einstellungen. Die große Karte nennt
die Herkunft der Bilder.

Seit 2.0 zeigt die große Karte Luftbilder für ganz Deutschland und darüber hinaus, ebenfalls nur im eigenen
Browser von den Diensten geladen; jeder Dienst erfährt die IP-Adresse und den gezeigten Ausschnitt. Darum sind
sie in 2.0 ausgeschaltet, bis man sie im Ebenen-Feld der Karte einschaltet. Die Karte
nennt in ihrer Ecke die Herkunft der Bilder, die gerade zu sehen sind. Quellen, Lizenzen und Adressen im
Einzelnen: `docs/AERIAL.md`.

- Weit herausgezoomt und außerhalb Deutschlands: EOxCloudless https://cloudless.eox.at by EOX IT Services
  GmbH (Contains modified Copernicus Sentinel data 2016), CC BY 4.0
  (<https://creativecommons.org/licenses/by/4.0/>). Verwendet wird nur das Mosaik von 2016; die späteren
  Jahrgänge stehen unter CC BY-NC-SA 4.0 und werden nicht verwendet.
- Nah herangezoomt die Digitalen Orthophotos der Länder (WMS, EPSG:25832):
  - Baden-Württemberg: LGL-BW (2026) Datenlizenz Deutschland – Namensnennung – Version 2.0, www.lgl-bw.de
  - Bayern: © Bayerische Vermessungsverwaltung – www.geodaten.bayern.de, CC BY 4.0
  - Berlin: Geoportal Berlin / Digitale farbige Orthophotos 2025 (DOP20RGBI), dl-de/zero-2.0
  - Brandenburg: © GeoBasis-DE/LGB, dl-de/by-2-0 (Daten geändert)
  - Bremen: © GeoBasis-DE / Landesamt GeoInformation Bremen 2023, CC BY 4.0
  - Hessen: © Hessische Verwaltung für Bodenmanagement und Geoinformation (§ 24 HVGG)
  - Mecklenburg-Vorpommern: © GeoBasis-DE/M-V 2026, CC BY 4.0
  - Niedersachsen: © GeoBasis-DE / LGLN (2026), CC BY 4.0
  - Nordrhein-Westfalen: Land NRW (2026), dl-de/zero-2.0
  - Rheinland-Pfalz: ©GeoBasis-DE / LVermGeoRP (2026), dl-de/by-2-0, http://www.lvermgeo.rlp.de [Daten bearbeitet]
  - Saarland: © GeoBasis DE/LVGL-SL (2026), dl-de/by-2-0
  - Sachsen: GeoSN, dl-de/by-2-0
  - Schleswig-Holstein: © GeoBasis-DE/LVermGeo SH/CC BY 4.0
  - Thüringen: © GDI-Th, CC BY 4.0

  „Daten geändert“ / „Daten bearbeitet“: die Bilder werden auf der Karte verkleinert, zugeschnitten und mit
  dem Straßennetz überlagert. Datenlizenz Deutschland: <https://www.govdata.de/dl-de/by-2-0>,
  <https://www.govdata.de/dl-de/zero-2-0>.
- Nicht verwendet: Hamburg (die Zeitreihen des LGV enthalten neben den offenen Luftbildern auch
  Satellitenbilder unter geschlossener Lizenz, ohne dass erkennbar wäre, welche Jahrgänge) und Sachsen-Anhalt
  (offen, dl-de/by-2.0, aber sein Dienst lässt sich vom Browser nicht in eine Karte laden). Dort zeigt die
  Karte Sentinel-2. Die Bilder beider Länder sind im jeweiligen Geoportal zu sehen:
  <https://geoportal-hamburg.de>, <https://www.lvermgeo.sachsen-anhalt.de/de/gdp-open-data.html>.
- Die groben Umrisse der Länder, nach denen ein Bild einem Land zugeordnet wird: Natural Earth 1:10m
  „Admin 1 – States, Provinces“, gemeinfrei, vereinfacht (`src/reality/aerialstates.cpp`,
  `tools/aerial-states.py`).

## Wetter

Das Projekt enthält und verteilt keine Wetterdaten. Mit der Einstellung „Wetter: echt“ (Voreinstellung)
fragt das Spiel im Browser der Spielerin oder des Spielers bei Open-Meteo nach dem Wetter des Ortes, an dem
das Fahrzeug ist (auf 0,1° gerundet, also etwa 10 km), und – wenn im Arcade-Modus eine andere Zeit gewählt
ist – nach der Vorhersage für diese Stunde: **Weather data by Open-Meteo.com** (<https://open-meteo.com/>),
Lizenz **CC BY 4.0** (<https://creativecommons.org/licenses/by/4.0/>) – umgesetzt in Regen, Schnee, Nebel,
Wolken, Wind, nasse Straßen und Licht des Simulators. Der Dienst erfährt dabei die IP-Adresse; die
Einstellungen sagen das und nennen die Quelle. Mit einem anderen Wetter als „echt“ wird nichts abgefragt.

## Kraftstoffpreise

Ohne weiteres zeigen die Tankstellen typische Preise. Wer in den Einstellungen einen eigenen (kostenlosen)
Schlüssel von Tankerkönig einträgt, bekommt die heutigen Preise der Tankstellen im Umkreis von 25 km:
**Tankerkönig** (<https://creativecommons.tankerkoenig.de>), Daten der Markttransparenzstelle für
Kraftstoffe (MTS-K), Lizenz **CC BY 4.0**. Der Dienst erfährt dabei die IP-Adresse, den Schlüssel und das
Gebiet; ohne Schlüssel wird nichts abgefragt.

## Mein Standort

„Mein Standort“ (Hauptmenü, große Karte) fragt den Browser nach der eigenen Position – der Browser fragt
vorher um Erlaubnis. Die Position bleibt im Browser und wird nirgendwohin geschickt.

## Musik und Klänge

Alle Klänge (Motoren, Straßenbahn, Flugzeug, Hupen, Türen, Explosion) und die Musik werden im Browser
erzeugt (Web Audio) – es gibt keine Aufnahmen. Die Musikstücke entstehen beim Abspielen aus Regeln des
Programms (`src/audio/music.ts`) und stehen wie das Programm unter der AGPL-3.0.

## Verweise auf Rechtsvorschriften

Das Fehlerprotokoll verlinkt die zitierten Paragrafen der Straßenverkehrs-Ordnung auf
<https://www.gesetze-im-internet.de/stvo_2013/> und – bei den Aufgaben der Klasse CE – die Anlage 7 der
Fahrerlaubnis-Verordnung auf <https://www.gesetze-im-internet.de/fev_2010/> (Bundesministerium der Justiz,
Bundesamt für Justiz). Der Text der Vorschriften ist nicht Teil des Projekts.

## Umrisse der Länder (Weltkarte)

Umrisse der Länder, Küsten und Seen auf der Karte aller Regionen (`web/data/outline.bin`): Natural Earth 1:10m,
Version 5.1.2, gemeinfrei – „Made with Natural Earth. Free vector and raster map data @ naturalearthdata.com."
Für das Spiel umgerechnet, beschnitten und vereinfacht (`tools/build-outline.mjs`).

## Fremde Software

| Paket | Verwendung | Lizenz |
| --- | --- | --- |
| [three.js](https://threejs.org) | 3D-Darstellung im Browser | MIT |
| [Vite](https://vite.dev), [TypeScript](https://www.typescriptlang.org) | Bauen (nur Entwicklung) | MIT, Apache-2.0 |
| [saxes](https://github.com/lddubeau/saxes), [earcut](https://github.com/mapbox/earcut) | Kartenpipeline (nur Entwicklung) | ISC, ISC |
| [Pillow](https://python-pillow.github.io), [NumPy](https://numpy.org) | Geländedaten laden und verkleinern (`tools/fetch-geodata.py`, nur beim Kartenbau; nicht mitgeliefert) | MIT-CMU, BSD-3-Clause |
| [Playwright](https://playwright.dev) | Bildschirmfotos beim Entwickeln | Apache-2.0 |

Das ausgelieferte Spiel enthält keine Schriften, Texturen, 3D-Modelle oder Tonaufnahmen Dritter: Flächen,
Fahrzeuge, Gebäudefassaden und alle Geräusche werden zur Laufzeit erzeugt.

## Haftungsausschluss

Der Simulator dient dem Üben und ersetzt weder die Ausbildung in einer Fahrschule noch die amtliche
Prüfung. Die dargestellte Verkehrsregelung kann von der Wirklichkeit abweichen; maßgeblich sind immer die
Schilder und Zeichen vor Ort und die geltende Straßenverkehrs-Ordnung.

## three.js (Shader)

Die Shader der Materialien in `src/engine/three_glsl.cpp` und der Aufbau der Programme in `src/engine/shaders.cpp`,
`src/engine/renderer.cpp` sowie die Mathematik, die Geometrien und der Szenengraph unter `src/engine/` stammen aus
bzw. folgen **three.js** r187 (<https://threejs.org>), MIT-Lizenz:

> Copyright © 2010-2026 three.js authors
>
> Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
> documentation files (the "Software"), to deal in the Software without restriction, including without limitation
> the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and
> to permit persons to whom the Software is furnished to do so, subject to the following conditions:
>
> The above copyright notice and this permission notice shall be included in all copies or substantial portions of
> the Software.
>
> THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
> THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
> AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
> CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
> IN THE SOFTWARE.
