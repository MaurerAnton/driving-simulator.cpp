# Translating Fahrsim Baunatal

All texts of the simulator live in `src/i18n/`, one file per language. English (`en.ts`) is the reference,
German (`de.ts`) is the language the texts were written in – look there when the English wording leaves a
doubt, especially about traffic law.

## Adding or improving a language

1. Copy `src/i18n/en.ts` to `src/i18n/<code>.ts` (`<code>` = ISO 639-1 code, e.g. `fr`).
2. Translate the **values**. Keep every key, the order and the section comments.
3. Run `node tools/i18n-check.ts <code>` until it prints `ok`.
4. Add the language to `LANGS` in `src/i18n/index.ts` (name in the language itself, locale tag, loader).
   `npm test` then checks it together with all the others.

## What the texts are for

A browser-based driving simulator set in the German town of Baunatal. It teaches **German traffic rules** to
learner drivers – many of whom are not native German speakers. An instructor comments on mistakes, there are
lessons and a simulated driving test.

Write the way a good driving instructor talks: short, clear, friendly, never bureaucratic. Use the form of
address that mainstream apps in your language use for instructions, and keep it throughout.

## Rules

- **Placeholders** such as `{speed}`, `{n}`, `{what}` are filled in by the program. Keep each one exactly as it
  is; put it where the sentence needs it.
- **Key tokens** such as `[[KeyB]]`, `[[Enter]]`, `[[Space]]` stand for a key of the keyboard. The program
  replaces them with the letter printed on the player's own keyboard (or, on a touch screen, with the name of
  the on-screen button). Keep each token exactly once, never write a key letter yourself, and never translate
  what is inside the brackets.
- **Do not translate** names: Fahrsim Baunatal, Baunatal, street names (`Marktplatz 5`), A 49, StVO, UrhG,
  OpenStreetMap, Panoramax, KartaView, Mapillary, Wikimedia Commons, three.js, product names of steering
  wheels, licence names (AGPL-3.0-only, ODbL, CC BY-SA 4.0, MIT), file formats (JPEG, EXIF, GeoJSON …), the
  gamepad buttons (RT, LT, RB/LB, A, X/B, Y) and the selector positions P, R, N, D.
- **Sign numbers stay**: “sign 205” – translate the word “sign”, keep the number of the German catalogue.
  Keep `§ 8 StVO` as it is. `“Anlieger frei”` is the wording on a German sign: keep it in quotes and, if it
  helps, add what it means.
- **Numbers and units**: Western digits (0–9). Write the units the way your language writes them on the road
  and in the highway code (km/h, км/ч, χλμ/ώ, كم/س …; m, м …). “30 km/h zone” may become whatever your
  language calls it (German: *Tempo-30-Zone*, French: *zone 30*) as long as the number stays.
- **Names of keys** (`key.Enter`, `key.Shift`, `key.Escape`): what is printed on the keys of keyboards sold
  for your language – often the English word. `key.Space` is what your language calls the space bar.
- **Symbols** that refer to buttons on the screen stay: `▲ ▼ ⋯ P`. Keep `·` where it separates two things,
  and use the dash, quotation marks and ellipsis that are correct in your language.
- **No HTML**, no line breaks, no leading or trailing spaces, no invisible direction marks (right-to-left
  languages are laid out by the program).
- **A sentence stays a sentence, a label stays a label**: where the English text ends with a full stop, end
  yours the way your language ends a sentence; where it has none (buttons, titles), add none. Texts that
  start with a small letter in English (`navLeft`: “turn left”) are put behind other words (“In 120 m turn
  left”) – write them so that they fit there.
- **Screen positions** (“at the top left”, “on the right”) are the same in every language, also in those
  written from right to left: the controls keep their places.
- Texts are shown in tight places. The checker enforces the limits; the tightest are the pedal labels
  (`tbGas`, `tbBrake`: 9 characters), the speed-limit roundel (`limitNone`, `walk`: one or two short words –
  no word longer than 9 letters, or 4 characters in Chinese, Japanese and Korean), the key reminder (`kr.*`:
  one or two words) and the buttons of the on-screen controls (`tb*`).
- Refer to things the way the interface names them: if you translate `trManual` as X, write X where
  `ctl.clutch` quotes it; the same for “reality view” (`reality`), “free drive” (`free`), “settings”.

## Terms

Use the established term of your language's own highway code or driving-school vocabulary, not a literal
translation. What the German terms mean:

| German | Meaning |
| --- | --- |
| rechts vor links | at junctions without signs, the vehicle coming from the right goes first (priority to the right) |
| Vorfahrt gewähren (Zeichen 205) | give way / yield |
| Stoppschild (Zeichen 206), Haltlinie | stop sign; the line at which one must stop |
| Vorfahrtstraße (Zeichen 306) | priority road |
| Kreisverkehr | roundabout |
| Fußgängerüberweg / Zebrastreifen | zebra crossing |
| Tempo-30-Zone | a zone with a 30 km/h limit |
| verkehrsberuhigter Bereich (Zeichen 325.1) | traffic-calmed area where vehicles drive at walking pace and pedestrians may use the whole street |
| Anlieger frei | entry only for residents and people whose destination is in that street |
| Blinker, Warnblinker | direction indicator (turn signal); hazard warning lights |
| Schulterblick | looking over one's shoulder into the blind spot before turning or changing lanes |
| toter Winkel | blind spot |
| Abblendlicht, Fernlicht | dipped beam (low beam); main beam (high beam) |
| Kupplung, Schleifpunkt, abwürgen | clutch; biting point; to stall the engine |
| Schaltgetriebe, Automatik, Wählhebel | manual gearbox; automatic; the selector lever of an automatic (P R N D) |
| Handbremse / Feststellbremse | handbrake / parking brake |
| Fahrbahn, Gehweg | carriageway (where vehicles drive); pavement / sidewalk |
| einordnen | to get into the correct lane before a junction |
| Fahrstunde, Fahrlehrer, Prüfungsfahrt, Prüfer | driving lesson; instructor; driving test; examiner |
| leichter Fehler, schwerer Fehler, Gefährdung | minor fault; serious fault; a fault that endangers others and fails the test |
| Fehlerpunkte | fault points (penalty points counted in the test) |
| Realansicht | “reality view”: real street-level photographs of the place where the car is |
| Regel-Hilfe | on-screen hints showing the current speed limit and who has right of way |

## Checking

```bash
node tools/i18n-check.ts          # all languages
node tools/i18n-check.ts fr       # one
```

The checker reports missing or extra keys, changed placeholders or key tokens, names and numbers that must
stay, texts that are too long for their place, digits other than 0–9, and (for languages with their own
script) texts that were left in Latin letters. Warnings – a sentence without its full stop, a label with one,
a capital letter where a text continues a sentence – are hints: look at them, they need not be wrong.

Quantities the program writes itself (“5.1 km”, “12 min”, the month of a photograph) follow the language
through the browser's own formatting (`formatUnit`, `formatMonth` in `src/i18n/index.ts`).
