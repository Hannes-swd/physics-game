# Dreitafelprojektion (technische Zeichnung)

Für ein Teil oder mehrere markierte Teile (Baugruppe) entsteht eine technische Zeichnung nach
DIN ISO 5456-2, **Projektionsmethode 1** (in Europa üblich).

## Öffnen

| Weg | |
|---|---|
| **F4** | Zeichnung der Auswahl (Spiel-Modus: des angeschauten Teils) |
| Menü *Ansicht → Dreitafelprojektion* | |
| Eigenschaften-Fenster | Knopf „Zeichnung (F4)“ bzw. „Dreitafelprojektion aller“ bei Mehrfachauswahl |
| Befehl | `drawing [id ...] [file.svg]` (ohne Nummern: Auswahl; anderer Name: `threeview`) |

Im Fenster: **Aktualisieren** (nach Änderungen), **Auswahl zeichnen** (andere Teile übernehmen),
**Als SVG speichern…**, **Details** (Zusammenfassung als Text).

Beispiel (in einer leeren Werkstatt):

```
new box spruce 40 10 2 ; drill last top 0 0 10 ; drawing last brett.svg
```

```
three-view drawing: Board
scale 1:5 on A4 landscape, first-angle projection
overall size W x H x D: 400 x 20 x 100 mm
material: spruce, mass 0.3753 kg
front view: 4 visible, 2 hidden line segments, 1 center lines
top view: 24 visible, 0 hidden line segments, 2 center lines, holes/cylinders: Ø10
left side view: 4 visible, 2 hidden line segments, 1 center lines
SVG saved: brett.svg
```

Die Bohrung erscheint in Vorder- und Seitenansicht gestrichelt (verdeckt), in der Draufsicht als Kreis mit Mittelkreuz
und Maß Ø10. Die Textausgabe ist englisch wie alle Ausgaben, das Schriftfeld auf dem Blatt deutsch
(„Werkstoff: Fichte“, „Maßstab 1:5“).

## Anordnung (Projektionsmethode 1)

```
+-----------------------+   +-------------+
|   Vorderansicht       |   |  Seiten-    |
|   (Blick von vorne,   |   |  ansicht    |
|    entlang -Z)        |   |  von links  |
+-----------------------+   +-------------+
+-----------------------+
|   Draufsicht          |
|   (Blick von oben)    |
+-----------------------+          [Schriftfeld]
```

- **Vorderansicht** oben links: x nach rechts, y nach oben.
- **Draufsicht** darunter: x nach rechts, die Vorderseite (+z) liegt unten.
- **Seitenansicht von links** rechts neben der Vorderansicht: z nach rechts, y nach oben.
- Ansichten sind höhen- bzw. breitengleich ausgerichtet.
- Neben dem Schriftfeld steht das Symbol für Projektionsmethode 1 (Kegelstumpf mit Kreisen).

## Linienarten

| Linie | Darstellung | Bedeutung |
|---|---|---|
| breite Volllinie | 0,5 mm | sichtbare Kanten und Umrisse |
| schmale Strichlinie | 0,25 mm, gestrichelt | verdeckte Kanten (z. B. Bohrungen im Inneren) |
| schmale Strich-Punkt-Linie | 0,25 mm | Mittellinien von Bohrungen und Zylindern |
| schmale Volllinie | 0,25 mm | Maß- und Hilfslinien |

Sichtbare Linien haben Vorrang: Liegt eine verdeckte Kante genau unter einer sichtbaren, wird sie weggelassen.

## Was gezeichnet wird

- **Körperkanten**: Kanten, an denen die Oberfläche um mehr als 25° abknickt.
- **Umrisse gekrümmter Flächen** (Zylinder, Bohrungen, Kugeln): für jede Blickrichtung berechnet.
- **Verdeckung**: Jede Kante wird in kurzen Abständen geprüft: Liegt zwischen Kante und Betrachter Material eines der
  gezeichneten Teile, ist dieser Abschnitt verdeckt (gestrichelt).
- **Mittellinien**: Kreuz bei Bohrungen/Zylindern, die man von vorne sieht, Achslinie bei seitlich gesehenen.
- **Bemaßung** in mm: Gesamtbreite und -höhe in der Vorderansicht, Gesamttiefe in der Draufsicht,
  Durchmesser `Ø` an Bohrungen und Zylindern mit Hinweislinie.
- **Schriftfeld**: Benennung (Teilename bzw. „n Teile (Baugruppe)“), Werkstoff, Masse, Maßstab, Projektionsmethode.

## Maßstab und Blatt

Das Blatt ist **A4 quer** (297 × 210 mm) mit Rahmen. Der Maßstab wird automatisch aus der Normreihe gewählt,
sodass alles auf das Blatt passt:

`10:1  5:1  2:1  1:1  1:2  1:5  1:10  1:20  1:50  1:100  1:200  1:500  1:1000`

Maßzahlen geben immer die **wahre Größe** in mm an, unabhängig vom Maßstab.

## SVG-Export

Die SVG-Datei hat echte Millimeter-Maße (`width="297mm"`), kann also im Maßstab ausgedruckt werden
(z. B. im Browser oder in Inkscape öffnen, Druck ohne „an Seite anpassen“).

## Grenzen

- Bemaßt werden die Gesamtmaße und Durchmesser, keine Einzelmaße (Lochabstände usw.).
- Keine Schnittdarstellungen.
- Importierte Modelle werden mit allen Netzkanten über 30° Knick gezeichnet.
