# Materialien bearbeiten und neu anlegen

Alle Materialien stehen in **einer JSON-Datei**: [`assets/materials.json`](../assets/materials.json). Dort steht für jedes
Material alles, was das Spiel darüber weiß: Name, Klasse, Textur und Farben, Dichte, Reibung, Festigkeit, Härte,
Bruch- und Spaltverhalten, Biegbarkeit, und die Wärmewerte (schmilzt es, wann, wird es weich, wann spröde, verkohlt es,
brennt es, wie stark, wie viel Rauch). Im Programm steht kein Materialwert mehr fest im Code.

Drei Wege, Materialien zu ändern oder neue anzulegen:

| Weg | Wie |
|---|---|
| Datei | `assets/materials.json` im Editor öffnen (VS Code prüft und färbt JSON), ändern, Spiel starten (oder `materials reload`) |
| Fenster | Menü *Ansicht → Materialien bearbeiten* (`window materials`): links die Liste, **Neu (Kopie)**, **Speichern**, **Datei neu laden**; rechts alle Werte. Änderungen gelten sofort für alle Teile |
| Befehle | `materials new pine spruce`, `materials set pine density 520`, `materials save` (siehe unten) |

## Die Datei

```json
{
  "_keys": { "density": "Mechanik: Dichte (kg/m³)", ... },
  "materials": [
    {
      "name":       "pine",
      "label":      "Kiefer",
      "aliases":    ["kiefer"],
      "class":      "wood",
      "texture":    "wood",
      "color":      [0.8, 0.6, 0.35],
      "density":    520,
      "melts_at":   null,
      "ignites_at": 280,
      "residue":    "charcoal"
    }
  ],
  "pairs": [
    {"a": "pine", "b": "steel", "friction": [0.45, 0.35]}
  ]
}
```

- `"materials"` ist eine Liste, ein Eintrag je Material. `"name"` ist der Befehlsname: klein, a–z, 0–9, `_`, `-`.
- Zahlen mit Punkt, mehrere Zahlen als Liste: `"color": [0.8, 0.6, 0.35]`, `"friction": [0.5, 0.35]`.
- Fehlende Werte nehmen den Standard (siehe Tabelle). Ein neues Material braucht also nur die Werte, die anders sind.
- `null` schaltet etwas ab: `"melts_at": null` (schmilzt nicht), `"ignites_at": null` (brennt nicht), `"bend_radius": null`
  (spröde, nicht biegbar), `"split_energy"`, `"brittle_below"`, `"chars_from"`, `"residue": null` (nur Asche).
  `"none"` geht auch.
- Schlüssel, die mit `_` beginnen, sind Erklärungen und werden übergangen. `"_keys"` beschreibt jeden Wert mit Einheit
  und erlaubten Wörtern; das Spiel schreibt es beim Speichern neu, passend zur Feldtabelle.
- Die Reihenfolge in der Datei ist die Reihenfolge in den Menüs.
- Szenen speichern das Material mit seinem Namen. Ein umbenanntes Material: alten Namen bei `"aliases"` eintragen, dann
  laden alte Szenen weiter.

Fehler werden mit Zeile und Spalte gemeldet (z. B. „line 12, column 5: ',' or '}' expected“ bei einem fehlenden Komma,
„unknown key“ bei einem Tippfehler im Schlüssel): auf der Konsole beim Start und bei `materials reload`. Ist die Datei
nicht lesbar, bleibt die alte Liste; fehlt sie ganz, nimmt das Spiel die beim Bauen eingebettete Kopie.

**Wo die Datei gesucht wird:** neben `Werkstatt.exe` (`materials.json`), dann `assets/materials.json` im aktuellen
Ordner, neben dem Programm und zwei Ordner darüber (aus `build/Release` heraus also die Datei im Projekt). `materials file`
zeigt, welche geladen ist. Eine alte `materials.txt` (Format `[material name]` / `key = value`) wird weiter gelesen,
gespeichert wird daneben als `materials.json`.

## Werte

| Schlüssel | Bedeutung | Einheit | Standard |
|---|---|---|---|
| **Allgemein** | | | |
| `label` | Name in den Menüs | | der Befehlsname |
| `aliases` | weitere Namen (Befehle, alte Szenen) | | – |
| `class` | Verhalten: `wood` `metal` `stone` `rubber` `plastic` | | stone |
| **Aussehen** | | | |
| `texture` | Muster im Shader: `wood` `brick` `metal` `stone` `rubber` `concrete` `plastic` `plain` | | plain |
| `color`, `color2` | Grundfarbe, zweite Farbe (Maserung, Sprenkel), linear | r g b 0..1 | 0.6 / 0.5 grau |
| `roughness` | 0 spiegelglatt .. 1 matt | | 0.5 |
| `metallic` | 0 .. 1 | | 0 |
| `temper` | Anlauffarben nach dem Erhitzen: `none` `steel` (bunt) `copper` (dunkel) | | none |
| **Mechanik** | | | |
| `density` | Dichte | kg/m³ | 1000 |
| `friction` | Haft- und Gleitreibung | 2 Zahlen | 0.5 0.4 |
| `rolling` | Rollreibungszahl c_rr | | 0.005 |
| `restitution` | Stoßzahl 0 .. 1 | | 0.3 |
| `tensile`, `compressive` | Zug-, Druckfestigkeit (begrenzen Klebungen, Schweißnähte, Lochleibung) | MPa | 10 |
| `young` | E-Modul | GPa | 1 |
| `hardness` | Brinell-Härte: Abrieb, Dellen, wer beim Aufprall Werkzeug ist | MPa | 100 |
| `fracture_energy` | Bruchenergie beim Aufprall (spröde ~3000, Holz 40 000, Metall 5·10⁷) | J/m² | 10 000 |
| `split_energy` | Spaltenergie für einen Keil | J/m² oder null | null |
| `bend_radius` | kleinster Biegeradius innen, Vielfache der Dicke | × Dicke oder null | null |
| **Wärme** ([12](12-Waerme-und-Feuer.md)) | | | |
| `conductivity` | Wärmeleitfähigkeit | W/(m·K) | 1 |
| `specific_heat` | Wärmekapazität | J/(kg·K) | 1000 |
| `emissivity` | Emissionsgrad (Wärmestrahlung) | 0..1 | 0.9 |
| `melts_at` | Schmelzpunkt | °C oder null | null |
| `latent_heat` | Schmelzwärme | J/kg | 0 |
| `puddle_height` | Höhe einer Pfütze aus der Schmelze | mm | 10 |
| `soft_from`, `half_strength_at` | ab hier sinkt die Festigkeit / hier ist sie halb | °C | 300 / 600 |
| `brittle_below` | darunter spröde (Kälte) | °C oder null | null |
| `chars_from` | verkohlt / zersetzt sich ab | °C oder null | null |
| `ignites_at` | Zündtemperatur (mit Flamme daneben) | °C oder null | null |
| `heat_of_combustion` | Heizwert | J/kg | 0 |
| `burn_rate` | Abbrand an der Oberfläche | kg/(m²·s) | 0 |
| `smoke` | Rauch 0 .. 1 (Holz 0.15, Gummi 1 schwarz) | | 0 |
| `residue` | Material, das nach dem Abbrennen liegen bleibt (Holz: `charcoal`) | Name oder null | null (Asche) |
| `residue_share` | Anteil einer abgebrannten Stelle, der als Rest liegen bleibt | 0..1 | 0 |

**Klasse oder Textur?** Die Textur ist nur das Aussehen. Die Klasse bestimmt das Verhalten: Reibungspaare ohne eigenen
Eintrag, Verschleiß (Stein schleift), Nägel und Schrauben (Holz: Auszug nach Dichte, Stein: Dübel, Metall: Gewinde),
Lochleibung von Stiften, Holzfaser (spaltet entlang, Hirnholz härter, Werkzeuge bleiben stecken), Gummi federt zurück
(keine Delle).

## Reibungspaare

```json
"pairs": [
  {"a": "steel", "b": "steel", "friction": [0.74, 0.57]},
  {"a": "wood", "b": "metal", "friction": [0.45, 0.35]}
]
```

`a` und `b` sind Materialnamen oder Klassen. Für zwei Teile gilt zuerst ein Paar mit beiden Materialnamen, dann eines
mit beiden Klassen, sonst √(µ₁ · µ₂) der Werte bei `friction`.

## Befehle

| Befehl | Wirkung |
|---|---|
| `materials` | Liste mit den wichtigsten Werten und der geladenen Datei |
| `materials keys` | alle Schlüssel mit Bedeutung, Einheit und erlaubten Wörtern |
| `materials show <m>` | ein Material als JSON (wie in der Datei) |
| `materials set <m> <key> <value...>` | einen Wert ändern, gilt sofort für alle Teile (`materials set steel color 0.3 0.3 0.35`, `materials set oak ignites_at none`, `materials set pine name kiefer`) |
| `materials new <name> [copy_of]` | neues Material als Kopie (ohne Vorlage: Kopie des letzten) |
| `materials save [file]` | in die geladene Datei (oder die angegebene) schreiben |
| `materials reload [file]` | Datei neu einlesen. Teile behalten ihr Material über den Namen; fehlt es, bekommen sie das erste |
| `materials file` | welche Datei geladen ist |

Beispiel: eine Kiefer, etwas schwerer als Fichte und leichter entzündlich:

```
materials new pine spruce ; materials set pine label Kiefer ; materials set pine density 520
materials set pine color 0.8 0.6 0.35 ; materials set pine ignites_at 280 ; materials save
new box pine 80 10 2
```

## Hinweise

- Der Selbsttest benutzt immer die eingebettete Kopie, damit eigene Änderungen ihn nicht stören.
- Im LAN-Mehrspieler sollten alle dieselbe Datei haben (Teile werden mit der Nummer ihres Materials übertragen).
- Schmierstoffe, Kleber und Wärme-Werkzeuge stehen noch im Code (`Materials.cpp`, `Joints.cpp`).
