# Hauptmenü und Welten

Beim Start öffnet sich das **Hauptmenü**, nicht gleich eine Welt. Es ist mit **Qt Quick (QML)** gebaut und liegt in
`qml/`. Es ist noch ein Rohbau: Qt-Standardbausteine im Stil „Basic“, ohne eigene Farben und Gestaltung.

| Seite | Was |
|---|---|
| **Spielen** | Liste aller Welten mit Namen und „zuletzt geöffnet“, die zuletzt geöffnete oben |
| **Mehrspieler** | offene LAN-Spiele im Netz (werden gesucht, solange die Seite offen ist) oder Adresse eintippen, eigener Name |
| **Einstellungen** | Name im LAN, Mausempfindlichkeit, Sichtfeld, Schatten, Kanten, Raster, automatisch speichern, Ordner der Welten, **Grafik** (siehe unten) |
| **Beenden** | Programm beenden (wie das Schließen des Fensters) |

## Weltliste

- **Einmal klicken** wählt eine Welt aus. Danach gibt es:
  - **Spielen**: die Welt öffnen
  - **Im LAN öffnen**: öffnen und gleich als Gastgeber für andere freigeben
  - **Bearbeiten**: Namen ändern, Seed und Spielzeit ansehen, Ordner öffnen
  - **Löschen**: nach einer Rückfrage in den **Papierkorb** (lässt sich wiederherstellen)
- **Doppelklick** öffnet die Welt sofort.
- Tastatur: Pfeile wählen, **Enter** öffnet, **Entf** löscht.
- **Neue Welt**:
  - **Name**
  - **Seed**: steht beim Öffnen schon fest (Zufallszahl), lässt sich ändern (Zahl oder Text, leer = zufällig);
    **Zufällig** würfelt neu. Gleicher Seed bedeutet gleiche Welt.
  - **Welttyp**: *Normal* (Gelände mit Biomen) oder *Flach* (Ebene wie bisher)
  - **Biome** (nur bei *Normal*): *Alle Biome* oder *Nur …* ein einzelnes Biom
  - Mehr zum Gelände: [16](16-Weltgenerierung.md)
- In der Liste steht neben jeder Welt ihr Typ („normal, alle Biome“, „normal, nur Ebene“, „flach“).

Im Spiel (Menü *Datei*):

| Eintrag | Wirkung |
|---|---|
| **Welt speichern** (Strg+S) | sofort speichern |
| **Zurück zum Hauptmenü** | speichern, das Spielfenster schließen, das Hauptmenü öffnen |
| Szene speichern unter... / Laden... | wie bisher: eine Szene als einzelne `.werkstatt`-Datei (z. B. zum Weitergeben) |
| Beenden / Fenster schließen | speichern und das Programm beenden |

Die Welt wird außerdem **automatisch** gespeichert (Standard alle 5 Minuten, in den Einstellungen änderbar, 0 = nie).

Tritt man im Spiel einem LAN-Spiel bei, wird die eigene Welt vorher gespeichert. Was man danach beim Gastgeber baut,
landet nicht in der eigenen Welt.

## Ladebildschirm

Wechselt man aus dem Hauptmenü ins Spiel, gibt es keinen leeren Moment:

1. Das Menüfenster zeigt sofort eine **Ladeseite** („Welt wird geladen“ bzw. „LAN-Spiel beitreten“ mit Namen bzw.
   Adresse, `qml/LoadingPage.qml`). Es bleibt stehen, bis das Spielfenster sein erstes Bild zeigt, und schließt sich
   dann.
2. Das Spielfenster zeigt den **Ladebildschirm** mit Fortschrittsbalken und dem, was gerade passiert. Er deckt die
   Welt zu, Eingaben an die Welt sind gesperrt. Ist alles fertig, blendet er in 0,35 s aus.

| Anlass | Schritte (Text unter dem Balken) | fertig, wenn |
|---|---|---|
| Welt öffnen | Szene lesen → Teile anlegen: 120 / 800 → Kollisionsformen berechnen: noch N | die genauen Kollisionsformen (im Hintergrund) alle da sind, höchstens nach 20 s (bis dahin gelten grobe Formen, die Welt ist trotzdem spielbar) |
| LAN-Spiel beitreten | Verbinde mit ... → Warte, bis ... dich zulässt → Welt wird übertragen: N Teile | nach allen Teilen die erste Lage vom Gastgeber kommt. Knopf **Abbrechen** führt zurück ins Hauptmenü |
| Welt verlassen | Welt wird gespeichert | gespeichert (danach schließt das Fenster) |

- Der Balken zeigt echten Fortschritt (Teile, Kollisionsformen). Beim Beitreten ist die Dauer unbekannt, dann läuft
  ein Stück hin und her.
- Während synchroner Arbeit (Teile anlegen, Speichern) zeichnet das Spiel zwischendurch bis zu 30 Ladebilder je
  Sekunde, damit das Fenster nicht „reagiert nicht“ zeigt.
- Per Befehl: `menu` zeigt im Spiel die Zeile `loading       world: Kollisionsformen berechnen: noch 3 (84 %)` bzw.
  `loading       join: Warte, bis ... dich zulässt ...`. Fehlt die Zeile, ist das Laden fertig.
- Nur aus dem Hauptmenü und mit `--world`. Mit `--shot`, `--script` usw. ohne Hauptmenü gibt es keinen Ladebildschirm,
  Fotos und Tests sehen gleich die Welt.

## Speichersystem

Jede Welt ist ein Ordner unter `%APPDATA%\Werkstatt\worlds\`. Der Ordnername kommt aus dem Namen der Welt; beim
Umbenennen bleibt er gleich.

```
%APPDATA%\Werkstatt\
  settings.json                 Einstellungen
  worlds\
    Meine Welt\
      world.json                Name, Seed, Generator, Zeiten, Ansicht, Umgebung
      world.bin                 Teile, Verbindungen, Gewässer, entfernte Bäume (binär, siehe 07-Engine-Aufbau.md);
                                ältere Welten: scene.werkstatt (Text), wird beim nächsten Speichern zu world.bin
      trees.json                Kopie der Baumarten, mit denen die Welt erzeugt wird (17-Baeume.md)
      generator.json            nur Welttyp normal: Kopie von biomes.json, mit der die Welt erzeugt wird
      regions\r.<x>.<z>.bin     nur Welttyp normal: geänderte Gelände-Chunks (16-Weltgenerierung.md)
      world.json.bak            Stand vor dem letzten Speichern
      world.bin.bak
```

**`world.json`:**

| Feld | Bedeutung |
|---|---|
| `format` | Aufbau der Datei (1) |
| `name` | angezeigter Name |
| `seed`, `seedText` | Seed als Text (64 Bit passen in keine JSON-Zahl) und wie er eingegeben wurde (leer: zufällig gewählt) |
| `generator`, `generatorVersion` | `normal` (Gelände mit Biomen) oder `flat` (Ebene), Version 1. Ändert sich später der Generator, bleiben alte Welten bei ihrer Version und sehen gleich aus |
| `biome` | `all` oder die `id` eines Bioms, wenn die Welt nur aus diesem besteht |
| `created`, `lastPlayed` | Unix-Zeit in Sekunden (`lastPlayed`: beim Öffnen und bei jedem Speichern) |
| `playTime` | gespielte Zeit in Sekunden |
| `view` | Kamera im Bau-Modus, Ansicht, Spiel-Modus mit Lage und Blick der Spielfigur |
| `environment` | Luftdichte, Wind, Bruch an/aus, Anstoßen im Bau-Modus, Wärme an/aus, Lufttemperatur, Wärme-Zeitraffer |

**Sicher speichern:**
- Jede Datei wird erst als `.tmp` geschrieben. Erst wenn das geklappt hat, wird die alte Datei zur `.bak` und die neue
  nimmt ihren Platz ein. Ein Absturz oder ein voller Datenträger beim Speichern zerstört also nichts.
- Fehlt beim Öffnen die Szene oder ist sie kaputt, wird die `.bak` geladen (mit Meldung).
- Lässt sich keine von beiden laden, öffnet sich die Welt leer, wird aber **nicht gespeichert**. So bleiben die Dateien
  für eine Rettung unverändert.
- Eine Welt mit kaputter `world.json` erscheint in der Liste als „(beschädigt)“ und kann nur gelöscht werden.

**Später:** Mit dem Weltgenerator kommt pro Welt ein Ordner `regions\` dazu. Gespeichert werden dann nur Chunks, die
man verändert hat; der Rest entsteht aus dem Seed jedes Mal gleich neu.

## Einstellungen (`settings.json`)

| Schlüssel | Standard | Bedeutung |
|---|---|---|
| `playerName` | leer (= Windows-Benutzer) | Name im LAN |
| `mouseSensitivity` | 1 | Faktor auf die Mausbewegung im Spiel-Modus (0,2–3) |
| `fov` | 55 | Sichtfeld in Grad (40–100) |
| `shadows`, `edges`, `grid` | an | Schatten, Kanten, Raster (auch im Spiel unter *Ansicht*, das wird beim Verlassen übernommen) |
| `biomeBorders` | aus | Linien an den Biomgrenzen (Gelände; auch im Spiel unter *Ansicht → Biomgrenzen*). Ältere Dateien (ohne `version` 2) hatten hier noch den alten Standard „an“, der wird nicht mehr übernommen |
| `autosaveMinutes` | 5 | automatisch speichern (0 = nie) |
| `graphics` | Stufe „Hoch“ | Grafik, siehe unten |

Die Einstellungen gelten, wenn eine Welt geöffnet wird. Im Spiel stellt man sie im **Pausenfenster** um (siehe unten), dort
gelten sie sofort.

### Grafik

Die **Qualität** setzt alle Werte auf einmal; ändert man danach einen einzelnen, steht dort „Eigene“.

| Wert (`graphics.*`) | Niedrig | Mittel | Hoch | Ultra | Bedeutung |
|---|---|---|---|---|---|
| Kantenglättung (`msaa`) | aus | 2× | 4× | 8× | Abtastungen je Pixel: glatte statt treppiger Kanten. Mehr, als die Grafikkarte kann, wird auf ihr Höchstes begrenzt |
| Supersampling (`supersampling`) | aus | aus | aus | an | jede Abtastung wird ganz berechnet: feine Muster (Holz, Rinde, Gelände, Gras) scharf statt flimmernd. Kostet am meisten |
| Schattenqualität (`shadowQuality` 0–3) | 2048 px, 10 m | 2048 px, 14 m | 4096 px, 18 m | 8192 px, 36 m | Auflösung der Schattenkarte und wie weit um die Spielfigur Schatten fallen |
| Sichtweite (`viewDistance`) | 60 % | 80 % | 100 % | 150 % | wie weit Gelände, Bäume und Büsche reichen, wie früh das Gelände feiner wird, Nebel |
| Gras und Blumen (`plants`) | 40 % | 70 % | 100 % | 150 % | Dichte und Reichweite (0 = kein Gras, siehe [18](18-Bewuchs.md)) |
| VSync (`vsync`) | – | – | – | – | eigener Schalter: Bildrate an den Bildschirm binden (aus: so schnell wie möglich, kann zerreißen) |

## Pausenfenster im Spiel

Im **Spiel-Modus** öffnet **Esc** das Pausenfenster: **Weiterspielen**, **Modus wechseln** (in den Bau-Modus),
**Einstellungen** (dieselbe Seite wie im Hauptmenü, alles gilt sofort, auch die Grafik) und **Beenden** (zurück ins
Hauptmenü oder das Programm beenden; die Welt wird dabei gespeichert). Es gehört zum Menü (`qml/PauseWindow.qml`, gleicher
Rohbau-Stil). Es ist kein eigenes Fenster mit Rahmen: es liegt über dem ganzen Spiel, das leicht grau abgedunkelt durchscheint, und steht fest in der Mitte. Solange es offen ist, steht die Welt still (im
LAN läuft sie für die anderen weiter). Esc im Pausenfenster: eine Seite zurück, auf der ersten weiterspielen.

Per Befehl: `pause [on|off|settings|quit]`, `pause set <key> <value>` (wie `menu set`, gilt sofort),
`pause shot <bild.png>` (Bild des Pausenfensters). Ohne Hauptmenü (Start mit `--world`, `--script`) zeigt das Spiel ein
einfaches Ersatzfenster.

## Kommandozeile und Befehle

| Option | Wirkung |
|---|---|
| *(keine)* | Hauptmenü |
| `--world <Name oder Ordner>` | diese Welt direkt öffnen (ohne Hauptmenü) |
| `--no-menu` | wie früher: direkt in die Beispiel-Werkstatt |
| `--menu-shot <bild.png> [main\|worlds\|new-world\|multiplayer\|settings\|loading]` | Hauptmenü auf dieser Seite öffnen (`new-world`: Weltliste mit offenem Dialog „Neue Welt“, `loading`: die Ladeseite), Bild speichern, beenden |

`--script`, `--shot`, `--play`, `--empty` und `--view` starten wie bisher direkt ohne Hauptmenü (Tests, Fernsteuerung).

| Befehl | Wirkung |
|---|---|
| `world` | offene Welt: Name, Ordner, Seed, Generator, Spielzeit, nächstes automatisches Speichern |
| `world save` | jetzt speichern |
| `world menu` | zurück zum Hauptmenü (nur wenn aus dem Hauptmenü gestartet) |

## Menü per Befehl (Fernsteuerung, KI)

Das Hauptmenü lässt sich komplett mit Befehlen bedienen, ohne Bildschirmfotos und ohne Maus. Jeder Befehl wirkt
**sofort**: Seitenwechsel ohne Animation, die Antwort kommt in wenigen Millisekunden. Die Fernsteuerung (Port 7777,
`Werkstatt.exe --send`, siehe [04](04-Fernsteuerung.md)) läuft die ganze Zeit, im Menü und im Spiel. Die Verbindung
bleibt beim Wechsel offen. Befehle, die mitten im Wechsel ankommen, bearbeitet die nächste Seite (Menü bzw. Spiel).

| Befehl | Wirkung |
|---|---|
| `menu` | **wo man ist**: im Hauptmenü die Seite (`main`, `worlds`, `multiplayer`, `settings`), ein offener Dialog (`new world`, `edit world`, `delete world`), die Meldung, alle Welten nummeriert (`>` = ausgewählt), gefundene LAN-Spiele, Einstellungen. Im Spiel: `screen game`, die Welt und der Modus |
| `menu main` / `worlds` / `multiplayer` / `settings` | auf diese Seite wechseln (auch `menu page <name>`, `menu back`, deutsch `menu spielen` ...) |
| `menu select <n\|Name>` | Welt auswählen (n = Nummer aus `menu`) |
| `menu play [n\|Name]` | Welt öffnen (ohne Angabe: die ausgewählte) |
| `menu lan [n\|Name]` | Welt öffnen und gleich im LAN freigeben |
| `menu new <Name> [Seed] [normal\|flat] [all\|<Biom>]` | Welt anlegen und auswählen (nicht öffnen). Nach dem Namen in beliebiger Reihenfolge: Seed, Welttyp (Standard `normal`), Biome (Standard `all`, sonst die `id` eines Bioms). Ohne Seed: zufällig |
| `menu rename <n\|Name> <neuer Name>` | umbenennen |
| `menu delete <n\|Name>` | in den Papierkorb |
| `menu games` | LAN-Spiele im Netz (wechselt auf `multiplayer`, dort wird gesucht) |
| `menu join <Adresse\|n\|Name>` | einem LAN-Spiel beitreten. Klappt es nicht, ist man wieder im Menü, und `menu` zeigt die Meldung |
| `menu set <Schlüssel> <Wert>` | `name`, `mouse` (0,2–3), `fov` (40–100), `shadows`/`edges`/`grid`/`borders` `on\|off`, `autosave` (Minuten, 0 = nie) |
| `menu close` | offenen Dialog schließen |
| `menu quit` | Programm beenden |
| `menu help` | diese Liste |

- Namen mit Leerzeichen in Anführungszeichen: `menu new "Meine Welt" 4711`.
- Im Hauptmenü gehen nur `menu ...`, `status` (= `menu`) und `help`. Alle anderen Befehle melden, dass zuerst eine Welt
  geöffnet werden muss.
- **Im Spiel** zeigt `menu`, dass man im Spiel ist. `menu <...>` speichert die Welt, geht zurück ins Hauptmenü und
  führt den Befehl dort aus; die Antwort kommt dann vom Menü. Beispiele: `menu worlds`, `menu play 2`, `menu settings`.
- Wartet ein Befehl im Spiel noch (z. B. `wait 5`), während die Welt geschlossen wird, bekommt er die Antwort
  `Error: the world was closed before the command finished`.
- Eine Zeile mit BOM am Anfang (z. B. aus einer PowerShell-Pipe) wird richtig gelesen.

Beispiel: ein ganzer Ablauf in einem Aufruf

```
Werkstatt.exe --send "menu new Testwelt 4711" "menu play Testwelt" "new box oak 40 40 40" "world save" "world menu" "menu"
```

```
> menu new Testwelt 4711
created world 1 "Testwelt", seed 4711, normal, alle Biome, selected. 'menu play' opens it.
> menu play Testwelt
opening world "Testwelt". The game window opens; then all game commands work ('world', 'status', 'help'). 'world menu' comes back here.
> new box oak 40 40 40
Created: #1  Block  [oak]  40.0 x 40.0 x 40.0 cm  46.080 kg  pos -0.0 20.0 -50.0 cm  free
> world save
world saved: ...\worlds\Testwelt\world.bin
> world menu
back to the main menu
> menu
screen        main menu, page main
dialog        none
worlds        1
 >  1  "Testwelt"                  last opened heute, 14:49, played 0 min, seed 4711
selected      1 "Testwelt"
settings      name "hanne (default)", mouse 1, fov 55, shadows on, edges on, grid on, autosave 5 min
commands      'menu help'
```

## Für Entwickler

| Datei | Aufgabe |
|---|---|
| `src/Worlds.*` | Weltordner, `world.json`, sicheres Ersetzen von Dateien, Papierkorb. Ohne Qt |
| `src/Settings.*` | `settings.json`. Ohne Qt |
| `src/Menu.*` | Qt-Teil: `MainMenu::show()` zeigt das Menü, bis eine Welt gewählt ist. Das Objekt `menu` in QML (Welten, Einstellungen, LAN-Suche) und die Befehle `menu ...` (`MenuBackend::command`). QML meldet Seite und Dialog (`menu.page`, `menu.dialog`), die Auswahl liegt in `menu.selectedDir` |
| `qml/*.qml` | die Oberfläche: `Main.qml` (Fenster, Seitenstapel, `openPage`, `showLoading`), `MainPage`, `WorldsPage`, `MultiplayerPage`, `SettingsPage`, `LoadingPage` |
| `src/AppWorld.cpp` | im Spiel: Welt öffnen, speichern, automatisch speichern, Einstellungen anwenden, zurück ins Menü, Befehle `world` und `menu`, Ladebildschirm (`beginLoading`, `updateLoading`, `drawLoading`, `renderLoadingFrame`) |
| `src/main.cpp` | Ablauf: Menü → Spiel → (Zurück zum Hauptmenü) → Menü ... Jede Sitzung bekommt ihr eigenes Spielfenster. Ein `RemoteServer` für die ganze Laufzeit (`AppOptions::remote`); `AppOptions::onFirstFrame` schließt die Ladeseite des Menüs (`MainMenu::hide`), sobald das Spielfenster ein Bild hat |
| `src/Remote.*` | Fernsteuerung; `hold()` gibt eine Zeile an die nächste Phase weiter (kam sie während des Wechsels Menü ↔ Spiel an) |

- Das Menü ist eine eigene Bibliothek (`WerkstattMenu`), weil Qt eigene Compiler-Schalter mitbringt. Die QML-Dateien
  sind eingebettet (`qrc:/menu/`). Nach einer Änderung an `qml/` reicht ein Neubau.
- Gestalten: Der Stil „Basic“ ist die schlichteste Grundform der Qt Quick Controls und lässt sich vollständig selbst
  gestalten (eigene `background`/`contentItem` je Steuerelement, oder ein eigener Stil). Gesetzt wird er in `Menu.cpp`
  (`QQuickStyle::setStyle`).
- Qt wird beim Konfigurieren unter `C:/Qt/6.*/msvc2022_64` gesucht (oder `-DCMAKE_PREFIX_PATH=...`). Nach dem Bauen
  legt `windeployqt` die Qt-DLLs und QML-Module neben `Werkstatt.exe`. Ohne Qt (oder mit `-DWERKSTATT_MENU=OFF`) wird
  das Spiel ohne Hauptmenü gebaut und startet wie früher.
- **vcpkg** ist für dieses Projekt abgeschaltet (`VcpkgEnabled=false`). Ein in Visual Studio integriertes vcpkg mit
  eigenem Qt würde sonst nach dem Bauen seine Qt-DLLs (andere Version) neben das Programm kopieren. Qt startet dann nicht
  („no Qt platform plugin could be initialized“).
- Während das Spiel läuft, gibt es kein Qt-Fenster: Das Menü wird geschlossen, bevor das Spielfenster (GLFW, Vulkan)
  aufgeht, und danach neu aufgebaut.
