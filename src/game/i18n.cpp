// SPDX-License-Identifier: AGPL-3.0-only
/**
 * Texts (German / English) and the catalogue of rule violations with their legal basis.
 *
 * Origin: src/ui/i18n.ts (MaurerAnton/driving-simulator @ 5b6ddc7, v0.1.0).
 * Strings are carried over verbatim; {param} placeholders behave like JS replace().
 */
#include "game/i18n.h"

namespace fs {

const std::vector<std::pair<std::string, Pair>> UI_ENTRIES = {
  {"title", {"Fahrsim Baunatal", "Fahrsim Baunatal"}},
  {"subtitle", {"Freier 3D-Fahrschulsimulator – deutsche Verkehrsregeln im echten Straßennetz", "Free 3D driving-school simulator – German traffic rules on the real road network"}},
  {"loading", {"Lade Karte …", "Loading map …"}},
  {"loadingTiles", {"Lade Umgebung …", "Loading surroundings …"}},
  {"start", {"Losfahren", "Start driving"}},
  {"free", {"Freie Fahrt", "Free drive"}},
  {"freeDesc", {"Fahre frei durch Baunatal. Der Fahrlehrer meldet sich bei Fehlern.", "Drive freely through Baunatal. The instructor speaks up when you make a mistake."}},
  {"lessons", {"Fahrstunden", "Lessons"}},
  {"exam", {"Prüfungsfahrt", "Driving test"}},
  {"examDesc", {"Etwa 20 bis 25 Minuten nach Ansage des Prüfers: Wohngebiet, Ampeln, Kreisverkehr, Vorfahrt, Landstraße. Schwere Fehler beenden die Prüfung.", "About 20 to 25 minutes following the examiner: residential streets, traffic lights, roundabout, priority, country road. Serious faults end the test."}},
  {"settings", {"Einstellungen", "Settings"}},
  {"controls", {"Steuerung", "Controls"}},
  {"about", {"Über das Projekt", "About"}},
  {"resume", {"Weiterfahren", "Resume"}},
  {"restart", {"Neu starten", "Restart"}},
  {"menu", {"Hauptmenü", "Main menu"}},
  {"paused", {"Pause", "Paused"}},
  {"reality", {"Realansicht", "Reality view"}},
  {"realityButton", {"Realansicht \u2013 echte Fotos (F)", "Reality view \u2013 real photos (F)"}},
  {"realitySub", {"So sieht es hier in Wirklichkeit aus", "What this place really looks like"}},
  {"realityClose", {"Schlie\u00dfen", "Close"}},
  {"realitySources", {"Freie Bilddienste", "Free imagery services"}},
  {"realityFree", {"frei lizenziert, ohne Anmeldung", "freely licensed, no account"}},
  {"realityToken", {"frei lizenziert, kostenloser Zugangsschl\u00fcssel n\u00f6tig",
                    "freely licensed, needs a free access token"}},
  {"realityTokenHelp", {"Schl\u00fcssel erstellen", "Create a token"}},
  {"realityTokenStored", {"wird nur in diesem Browser gespeichert", "stored in this browser only"}},
  {"realityNeedToken", {"Zugangsschl\u00fcssel fehlt", "access token missing"}},
  {"realityPrivacy",
   {"Die Fotos kommen direkt vom gew\u00e4hlten Dienst. Er erf\u00e4hrt dabei deine IP-Adresse und die "
    "Stelle auf der Karte, sonst nichts. Ohne H\u00e4ckchen wird nichts abgerufen.",
    "The pictures come straight from the service you tick. It learns your IP address and the place "
    "on the map, nothing else. Nothing is requested without a tick."}},
  {"realityOwn", {"Eigene Bilder", "Your own pictures"}},
  {"realityOwnText",
   {"Panoramen und Fotos von deinem Ger\u00e4t, zum Beispiel mit einem Street-View-Downloader "
    "gespeicherte 360\u00b0-Bilder (JPEG, PNG, WebP). Sie werden nirgends hochgeladen. Die Position "
    "stammt aus den Metadaten (EXIF/XMP), einer beiliegenden Liste (JSON, GeoJSON, CSV) oder dem "
    "Dateinamen. Du kannst sie auch einfach auf dieses Fenster ziehen.",
    "Panoramas and photos from your device, such as 360\u00b0 pictures saved with a street-view "
    "download tool (JPEG, PNG, WebP). They are not uploaded anywhere. The position comes from the "
    "metadata (EXIF/XMP), a list lying next to them (JSON, GeoJSON, CSV) or the file name. You can "
    "also simply drop them on this window."}},
  {"realityPickFiles", {"Bilder w\u00e4hlen \u2026", "Choose pictures \u2026"}},
  {"realityPickFolder", {"Ordner w\u00e4hlen \u2026", "Choose a folder \u2026"}},
  {"realityImported", {"Geladen: {n} \u00b7 davon mit Position: {p}",
                       "Loaded: {n} \u00b7 with a position: {p}"}},
  {"realityNearby", {"Bilder in der N\u00e4he", "Pictures nearby"}},
  {"realityUnplaced", {"Ohne Position ({n})", "Without a position ({n})"}},
  {"realityPinHere", {"An dieser Stelle ablegen", "Pin to this place"}},
  {"realityOwnPicture", {"Eigenes Bild", "Own picture"}},
  {"realityNone",
   {"W\u00e4hle rechts einen Bilddienst aus oder lade eigene Bilder \u2013 dann erscheinen hier "
    "echte Fotos dieser Stelle.",
    "Tick an imagery service on the right or load your own pictures \u2013 real photos of this "
    "place then appear here."}},
  {"realityNoneFound",
   {"Im Umkreis von {r} m gibt es keine Bilder. Unten l\u00e4sst sich die Stelle bei den Diensten "
    "im Browser \u00f6ffnen.",
    "There are no pictures within {r} m. The links below open the place at the services in your "
    "browser."}},
  {"realityLoading", {"l\u00e4dt \u2026", "loading \u2026"}},
  {"realityCount", {"Bilder: {n}", "pictures: {n}"}},
  {"realityError", {"Fehler: {e}", "Error: {e}"}},
  {"realityContributors", {"\u00a9 {s}-Mitwirkende", "\u00a9 {s} contributors"}},
  {"realityOriginal", {"Original ansehen", "View original"}},
  {"realityDragHint", {"Ziehen: umsehen \u00b7 Mausrad: zoomen", "Drag to look around \u00b7 wheel to zoom"}},
  {"realityOpenWeb", {"Diese Stelle im Browser \u00f6ffnen", "Open this place in the browser"}},
  {"startPoint", {"Startpunkt", "Start point"}},
  {"transmission", {"Getriebe", "Transmission"}},
  {"trAssist", {"Schaltgetriebe mit Kupplungshilfe", "Manual with clutch assist"}},
  {"trManual", {"Schaltgetriebe (Kupplung selbst)", "Manual (own clutch)"}},
  {"trAutomatic", {"Automatik", "Automatic"}},
  {"quality", {"Grafik", "Graphics"}},
  {"qLow", {"Niedrig", "Low"}},
  {"qMedium", {"Mittel", "Medium"}},
  {"qHigh", {"Hoch", "High"}},
  {"mirrors", {"Spiegel", "Mirrors"}},
  {"mOff", {"Aus", "Off"}},
  {"mAlt", {"Abwechselnd (schnell)", "Alternating (fast)"}},
  {"mFull", {"Jedes Bild", "Every frame"}},
  {"hudRules", {"Regel-Hilfe einblenden (Tempolimit, Vorfahrt)", "Show rule assistance (speed limit, right of way)"}},
  {"hudGear", {"Ganganzeige im Kombiinstrument", "Gear indicator in the cluster"}},
  {"voice", {"Gesprochene Ansagen", "Spoken instructions"}},
  {"volume", {"Lautstärke", "Volume"}},
  {"traffic", {"Verkehrsdichte", "Traffic density"}},
  {"language", {"Sprache", "Language"}},
  {"back", {"Zurück", "Back"}},
  {"result", {"Auswertung", "Result"}},
  {"passed", {"Bestanden", "Passed"}},
  {"failed", {"Nicht bestanden", "Failed"}},
  {"noFaults", {"Keine Fehler – sehr gut!", "No faults – well done!"}},
  {"faults", {"Fehler", "Faults"}},
  {"points", {"Fehlerpunkte", "Fault points"}},
  {"distance", {"Strecke", "Distance"}},
  {"duration", {"Dauer", "Time"}},
  {"hint", {"Hinweis", "Hint"}},
  {"minor", {"Leichter Fehler", "Minor fault"}},
  {"major", {"Schwerer Fehler", "Serious fault"}},
  {"fail", {"Gefährdung", "Dangerous fault"}},
  {"limitNone", {"frei", "none"}},
  {"walk", {"Schritt", "walk"}},
  {"startEngine", {"Motor starten: Enter halten", "Start the engine: hold Enter"}},
  {"releaseHandbrake", {"Handbremse lösen: Leertaste", "Release the handbrake: Space"}},
  {"fastenBelt", {"Anschnallen: B", "Fasten your seat belt: B"}},
  {"selectGear", {"Gang einlegen: E oder 1 (R = rückwärts)", "Select a gear: E or 1 (R = reverse)"}},
  {"selectDrive", {"Wählhebel auf D: E drücken (Q zurück zu N, R, P)", "Move the selector to D: press E (Q back to N, R, P)"}},
  {"pressClutch", {"Kupplung treten (Umschalt), dann schalten", "Press the clutch (Shift), then change gear"}},
  {"engineStalled", {"Motor abgewürgt – Enter zum Starten", "Engine stalled – Enter to restart"}},
  {"offMap", {"Ende der Karte – bitte wenden", "End of the map – please turn round"}},
  {"resetHint", {"T setzt das Auto zurück auf die Straße", "T puts the car back on the road"}},
  {"rolledOver", {"Überschlagen – das Auto steht wieder auf der Straße. Angepasste Geschwindigkeit!", "Rolled over – the car is back on the road. Mind your speed!"}},
  {"routeDone", {"Ziel erreicht", "Destination reached"}},
  {"navStraight", {"geradeaus", "straight on"}},
  {"navLeft", {"links abbiegen", "turn left"}},
  {"navRight", {"rechts abbiegen", "turn right"}},
  {"navUturn", {"wenden", "turn round"}},
  {"navIn", {"In {d} m {what}", "In {d} m {what}"}},
  {"navNow", {"Jetzt {what}", "Now {what}"}},
  {"navRoundabout", {"im Kreisverkehr die {n}. Ausfahrt nehmen", "take the {n}. exit at the roundabout"}},
  {"navFollow", {"Dem Straßenverlauf folgen", "Follow the road"}},
  {"laneLeft", {"links einordnen", "get in the left lane"}},
  {"laneRight", {"rechts einordnen", "get in the right lane"}},
  {"navMotorway", {"auf die Autobahn auffahren", "join the motorway"}},
  {"credits", {"Kartendaten", "Map data"}},
  {"licence", {"Lizenz", "Licence"}},
  {"source", {"Quellcode", "Source code"}},
  {"help", {"Hilfe (F1)", "Help (F1)"}},
  {"gamepadFound", {"Controller erkannt", "Controller detected"}},
  {"webglMissing", {"Dieser Browser unterstützt kein WebGL 2.", "This browser does not support WebGL 2."}},
};

const std::vector<std::pair<std::string, ViolationText>> VIOLATION_ENTRIES = {
  {"speed", {{"Zu schnell: {speed} bei erlaubten {limit} km/h", "Too fast: {speed} where {limit} km/h is allowed"}, {"Die zulässige Höchstgeschwindigkeit gilt ab dem Schild. Innerorts sind ohne Schild 50 km/h erlaubt, außerorts 100 km/h.", "The limit applies from the sign onwards. Without a sign: 50 km/h in built-up areas, 100 km/h outside."}, "§ 3 StVO, Zeichen 274"}},
  {"speedWalk", {{"Schrittgeschwindigkeit überschritten ({speed} km/h)", "Walking pace exceeded ({speed} km/h)"}, {"Im verkehrsberuhigten Bereich (Zeichen 325.1) gilt Schrittgeschwindigkeit; Fußgänger dürfen die ganze Straße benutzen.", "In a traffic-calmed area (sign 325.1) you must drive at walking pace; pedestrians may use the whole street."}, "Anlage 3 StVO, Zeichen 325.1"}},
  {"redLight", {{"Rote Ampel überfahren", "Ran a red light"}, {"Rot bedeutet: Halt vor der Kreuzung, an der Haltlinie warten.", "Red means: stop before the junction and wait at the stop line."}, "§ 37 Abs. 2 StVO"}},
  {"stopLine", {{"Haltlinie bei Rot überfahren", "Over the stop line on red"}, {"Die Haltlinie (Zeichen 294) ordnet an: hier halten. Wer erst dahinter zum Stehen kommt, steht im Weg von Fußgängern und Querverkehr – rechtzeitig und gleichmäßig bremsen.", "The stop line (sign 294) orders you to stop here. Coming to rest beyond it puts you in the way of pedestrians and crossing traffic – brake earlier and more evenly."}, "Anlage 2 StVO, Zeichen 294; § 37 Abs. 2 StVO"}},
  {"amberLight", {{"Bei Gelb noch eingefahren", "Entered on amber"}, {"Gelb ordnet an: vor der Kreuzung auf das nächste Zeichen warten. Nur wer nicht mehr gefahrlos anhalten kann, fährt weiter.", "Amber orders you to wait for the next signal. Carry on only if you can no longer stop safely."}, "§ 37 Abs. 2 StVO"}},
  {"stopSign", {{"Am Stoppschild nicht angehalten", "Did not stop at the stop sign"}, {"Zeichen 206: An der Haltlinie muss das Fahrzeug vollständig stehen, auch wenn frei ist. Dann Vorfahrt gewähren.", "Sign 206: the vehicle must come to a complete stop at the line even if the road is clear, then give way."}, "Anlage 2 StVO, Zeichen 206"}},
  {"prioritySign", {{"Vorfahrt missachtet", "Failed to give way"}, {"Zeichen 205 „Vorfahrt gewähren“: Der Verkehr auf der anderen Straße darf weder gefährdet noch wesentlich behindert werden.", "Sign 205 “give way”: traffic on the other road must neither be endangered nor substantially hindered."}, "§ 8 Abs. 2 StVO, Zeichen 205/206"}},
  {"rightBeforeLeft", {{"Rechts vor links missachtet", "Ignored “right before left”"}, {"An Kreuzungen und Einmündungen ohne Schilder hat Vorfahrt, wer von rechts kommt – typisch in Wohngebieten und Tempo-30-Zonen.", "At junctions without signs, whoever comes from the right has priority – typical in residential areas and 30 km/h zones."}, "§ 8 Abs. 1 StVO"}},
  {"priorityRoundabout", {{"Vorfahrt im Kreisverkehr missachtet", "Failed to give way at the roundabout"}, {"Mit Zeichen 215 und 205 hat der Verkehr auf der Kreisfahrbahn Vorfahrt. Beim Einfahren wird nicht geblinkt, beim Ausfahren rechts.", "With signs 215 and 205, traffic on the roundabout has priority. Do not indicate when entering; indicate right when leaving."}, "§ 8 Abs. 1a StVO"}},
  {"priorityDriveway", {{"Beim Einfahren auf die Straße andere gefährdet", "Endangered others when pulling onto the road"}, {"Wer aus einem Grundstück, Parkplatz, verkehrsberuhigten Bereich oder Feldweg auf die Straße fährt, muss jede Gefährdung ausschließen.", "Coming out of a property, car park, traffic-calmed area or farm track you must rule out any danger to others."}, "§ 10 StVO"}},
  {"leftTurnOncoming", {{"Beim Abbiegen den Gegenverkehr nicht durchgelassen", "Did not let oncoming traffic pass when turning"}, {"Wer links abbiegen will, muss entgegenkommende Fahrzeuge durchfahren lassen – auch bei grüner Ampel ohne Pfeil.", "Turning left you must let oncoming vehicles pass – also on a green light without an arrow."}, "§ 9 Abs. 3 StVO"}},
  {"cutOff", {{"Anderen geschnitten oder ausgebremst", "Cut in front of another vehicle"}, {"Beim Anfahren, Einfädeln und Fahrstreifenwechsel darf niemand gefährdet werden: Spiegel, Schulterblick, Blinker, Lücke abwarten.", "When pulling out, merging or changing lanes nobody may be endangered: mirrors, shoulder check, indicator, wait for a gap."}, "§ 1 Abs. 2, § 7 Abs. 5, § 10 StVO"}},
  {"zebra", {{"Fußgänger am Zebrastreifen nicht beachtet", "Ignored a pedestrian at the zebra crossing"}, {"Wer den Überweg erkennbar benutzen will, hat Vorrang. Mit mäßiger Geschwindigkeit heranfahren und wenn nötig warten.", "Anyone visibly wanting to cross has priority. Approach at moderate speed and wait if necessary."}, "§ 26 Abs. 1 StVO, Zeichen 350"}},
  {"zebraSpeed", {{"Zu schnell an den Fußgängerüberweg herangefahren", "Approached the pedestrian crossing too fast"}, {"An Fußgängerüberwegen nur mit mäßiger Geschwindigkeit heranfahren, wenn jemand queren könnte.", "Approach pedestrian crossings at moderate speed when someone might cross."}, "§ 26 Abs. 1 StVO"}},
  {"pedestrian", {{"Fußgänger gefährdet", "Endangered a pedestrian"}, {"Fußgänger sind die schwächsten Verkehrsteilnehmer. Wer sie gefährdet, besteht keine Prüfung.", "Pedestrians are the most vulnerable road users. Endangering them fails any test."}, "§ 1 Abs. 2, § 3 Abs. 2a StVO"}},
  {"noIndicator", {{"Abbiegen ohne Blinker", "Turned without indicating"}, {"Wer abbiegen will, muss dies rechtzeitig und deutlich ankündigen – der Blinker bleibt an, bis der Vorgang beendet ist.", "Announce a turn clearly and in good time – keep the indicator on until the manoeuvre is finished."}, "§ 9 Abs. 1 StVO"}},
  {"wrongIndicator", {{"Falsch geblinkt", "Wrong indicator"}, {"Der Blinker muss zur tatsächlichen Fahrtrichtung passen; falsches Blinken täuscht andere.", "The indicator must match where you actually go; a wrong signal misleads others."}, "§ 9 Abs. 1 StVO"}},
  {"indicatorLeftOn", {{"Blinker vergessen auszuschalten", "Indicator left on"}, {"Ein dauerhaft laufender Blinker kündigt ein Abbiegen an, das nicht kommt.", "A forgotten indicator announces a turn that never happens."}, "§ 9 Abs. 1 StVO"}},
  {"noShoulderCheck", {{"Schulterblick fehlt", "No shoulder check"}, {"Vor dem Abbiegen nochmals auf den nachfolgenden Verkehr achten: Radfahrer im toten Winkel sieht man nur mit Blick über die Schulter (J / K oder Maus).", "Check following traffic again before turning: cyclists in the blind spot are only seen with a look over the shoulder (J / K or mouse)."}, "§ 9 Abs. 1, Abs. 3 StVO"}},
  {"roundaboutEntryIndicator", {{"Beim Einfahren in den Kreisverkehr geblinkt", "Indicated when entering the roundabout"}, {"Bei der Einfahrt in den Kreisverkehr wird nicht geblinkt – nur beim Verlassen rechts.", "Do not indicate when entering a roundabout – only right when leaving."}, "§ 8 Abs. 1a StVO"}},
  {"roundaboutExitIndicator", {{"Kreisverkehr ohne Blinker verlassen", "Left the roundabout without indicating"}, {"Das Ausfahren aus dem Kreisverkehr ist ein Abbiegen und muss rechts angezeigt werden.", "Leaving a roundabout is a turn and must be indicated to the right."}, "§ 8 Abs. 1a, § 9 Abs. 1 StVO"}},
  {"turnBan", {{"Verbotene Fahrtrichtung genommen", "Took a prohibited direction"}, {"Vorgeschriebene Fahrtrichtungen (blaue Pfeilschilder) und Abbiegeverbote sind verbindlich.", "Mandatory directions (blue arrow signs) and turn bans are binding."}, "Anlage 2 StVO, Zeichen 209–214"}},
  {"wrongWay", {{"Entgegen der Fahrtrichtung gefahren", "Drove against the direction of traffic"}, {"Zeichen 267 verbietet die Einfahrt; Einbahnstraßen dürfen nur in Pfeilrichtung befahren werden.", "Sign 267 forbids entry; one-way streets may only be used in the direction of the arrow."}, "Anlage 2 StVO, Zeichen 220, 267"}},
  {"keepRight", {{"Rechtsfahrgebot missachtet", "Not keeping to the right"}, {"Es ist möglichst weit rechts zu fahren, nicht nur bei Gegenverkehr, beim Überholtwerden oder an Kuppen und in Kurven.", "Keep as far right as possible – not only with oncoming traffic, when being overtaken, on crests or in bends."}, "§ 2 Abs. 2 StVO"}},
  {"sidewalk", {{"Gehweg befahren", "Drove on the pavement"}, {"Fahrzeuge müssen die Fahrbahn benutzen. Der Gehweg gehört den Fußgängern.", "Vehicles must use the carriageway. The pavement belongs to pedestrians."}, "§ 2 Abs. 1 StVO"}},
  {"offRoad", {{"Fahrbahn verlassen", "Left the road"}, {"Bleib auf der Fahrbahn; Seitenstreifen und Grünflächen sind kein Fahrstreifen.", "Stay on the carriageway; verges and green areas are not lanes."}, "§ 2 Abs. 1 StVO"}},
  {"accessPrivate", {{"Verbotene Zufahrt", "No entry for vehicles"}, {"Zeichen 250 verbietet Fahrzeuge aller Art; private Zufahrten und Werksgelände sind tabu.", "Sign 250 prohibits all vehicles; private drives and company premises are off limits."}, "Anlage 2 StVO, Zeichen 250"}},
  {"accessAgricultural", {{"Wirtschaftsweg befahren", "Used a farm track"}, {"Feld- und Waldwege sind meist nur für land- und forstwirtschaftlichen Verkehr frei (Zeichen 260 mit Zusatzzeichen).", "Farm and forest tracks are usually open to agricultural and forestry traffic only (sign 260 with plate)."}, "Anlage 2 StVO, Zeichen 250/260"}},
  {"accessDestination", {{"Nur für Anlieger frei", "Residents and visitors only"}, {"„Anlieger frei“ erlaubt die Durchfahrt nur, wer dort ein Ziel hat.", "“Anlieger frei” allows entry only if your destination is there."}, "Zusatzzeichen 1020-30"}},
  {"distance", {{"Zu dicht aufgefahren ({gap} m)", "Following too closely ({gap} m)"}, {"Faustregel außerorts: halber Tacho in Metern. Innerorts mindestens eine Sekunde Abstand.", "Rule of thumb outside towns: half the speedometer reading in metres. In town at least one second."}, "§ 4 Abs. 1 StVO"}},
  {"crash", {{"Zusammenstoß mit einem anderen Fahrzeug", "Collision with another vehicle"}, {"Ein Unfall beendet jede Prüfungsfahrt. Vorausschauend fahren und bremsbereit sein.", "A collision ends every driving test. Look ahead and be ready to brake."}, "§ 1 Abs. 2 StVO"}},
  {"crashObject", {{"Gegen ein Hindernis gefahren ({speed} km/h)", "Hit an obstacle ({speed} km/h)"}, {"Geschwindigkeit und Abstand so wählen, dass das Fahrzeug jederzeit beherrscht wird.", "Choose speed and distance so that the vehicle is under control at all times."}, "§ 3 Abs. 1 StVO"}},
  {"stall", {{"Motor abgewürgt", "Engine stalled"}, {"Kupplung langsam bis zum Schleifpunkt kommen lassen, etwas Gas geben, dann ganz lösen.", "Let the clutch come up slowly to the biting point, add a little throttle, then release fully."}, "Fahrzeugbedienung"}},
  {"seatbelt", {{"Nicht angeschnallt", "Seat belt not fastened"}, {"Vorgeschriebene Sicherheitsgurte müssen während der Fahrt angelegt sein.", "Prescribed seat belts must be worn while driving."}, "§ 21a Abs. 1 StVO"}},
  {"handbrakeOn", {{"Mit angezogener Handbremse gefahren", "Driving with the handbrake on"}, {"Vor dem Anfahren die Feststellbremse lösen (Kontrollleuchte beachten).", "Release the parking brake before driving off (watch the warning lamp)."}, "Fahrzeugbedienung"}},
};

/** JS String.replace('{k}', v): first occurrence only, same as the original. */
static std::string replaceFirst(std::string s, const std::string& needle, const std::string& value) {
  auto pos = s.find(needle);
  if (pos != std::string::npos) s.replace(pos, needle.size(), value);
  return s;
}

static std::string pick(const Pair& p, Lang lang) { return lang == Lang::De ? p.first : p.second; }

std::string tr(const std::string& key, Lang lang) {
  for (const auto& e : UI_ENTRIES)
    if (e.first == key) return pick(e.second, lang);
  return key;
}

std::string tr(const std::string& key, Lang lang,
               const std::vector<std::pair<std::string, std::string>>& params) {
  std::string s = tr(key, lang);
  for (const auto& kv : params) s = replaceFirst(s, "{" + kv.first + "}", kv.second);
  return s;
}

std::string violationTitle(const std::string& id, Lang lang,
                           const std::vector<std::pair<std::string, std::string>>& data) {
  for (const auto& e : VIOLATION_ENTRIES) {
    if (e.first != id) continue;
    std::string s = pick(e.second.title, lang);
    for (const auto& kv : data) s = replaceFirst(s, "{" + kv.first + "}", kv.second);
    return s;
  }
  return id;
}

std::string violationWhy(const std::string& id, Lang lang) {
  for (const auto& e : VIOLATION_ENTRIES)
    if (e.first == id) return pick(e.second.why, lang);
  return "";
}

}  // namespace fs
