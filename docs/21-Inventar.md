# Inventar

Im **Spiel-Modus** kann die Figur Teile einstecken. Eingesteckte Teile sind **nicht mehr in der Welt**, bis man sie wieder
herausnimmt. Grenze ist das Gewicht: zusammen höchstens **30 kg**.

## Steuerung

| Eingabe | Wirkung |
|---|---|
| **E** (nichts in der Hand) | Inventar öffnen; im Fenster **E** oder **ESC**: schließen. Schaut man auf ein Rezept-Hologramm, wird es stattdessen aus dem Inventar gefüllt ([22](22-Herstellen.md)) |
| **E** (Teil in der Hand) | Teil ins Inventar. Schaut man dabei auf ein Hologramm, wird es stattdessen eingesetzt ([20](20-Bauvorlagen.md)) |
| **F** | das Teil, auf das man schaut, direkt ins Inventar, auch Kiesel und Stöcke am Boden |
| Rechtsklick auf ein Kästchen (oder mit rechts aus dem Fenster ziehen und loslassen) | Teil herausnehmen: es hängt vor der Figur |
| dann **R** / **Shift+R**, Mausrad | drehen (15°) / kippen, näher oder weiter weg |
| dann **Linksklick** | absetzen: ab jetzt wieder ein normales Physik-Teil |
| dann **E** | doch wieder ins Inventar |

Zu schwere Teile (oder wenn das Gewicht überschritten würde) bleiben in der Hand, eine Meldung sagt, wie viel noch frei ist.
Feste Teile (stehende Bäume, Stümpfe) lassen sich nicht einstecken.

## Die Fläche

Das Fenster zeigt ein Rechteck aus 16 × 10 Zellen; die ganze Fläche sind die 30 kg. Jedes Teil belegt so viele Zellen,
wie es wiegt (eine Zelle ≈ 0,19 kg, aufgerundet), und das Kästchen hat ungefähr die Form des Teils: Das Verhältnis
der Seiten ist das der längsten zur zweitlängsten Seite des Teils. Ein Stock wird so ein langer, flacher Streifen,
ein Stein fast ein Quadrat. Man sieht also direkt, wie viel vom Platz schon verbraucht ist; oben steht es zusätzlich
in kg und Prozent.

Neue Teile kommen an die erste freie Stelle von oben links (wenn nötig hochkant). Passt eines trotz freiem Gewicht nicht
in eine Lücke, wird alles neu eingeräumt (die großen zuerst).

## Speichern, LAN

Das Inventar gehört zur Welt und wird mit ihr gespeichert (`inventory.bin` im Ordner der Welt, auch Temperatur und
Bearbeitungen der Teile bleiben erhalten). Als Mitspieler im LAN gibt es noch kein Inventar (E nimmt dort wie früher auf
und legt ab).

## Befehle

| Befehl | Wirkung |
|---|---|
| `inventory` | Inhalt: Nummer, Name, Gewicht, Lage im Raster |
| `inventory open` / `close` | Fenster öffnen / schließen |
| `inventory store <Teil>` | Teil (Nummer aus `list`) einstecken |
| `inventory take <Nummer>` | herausnehmen (vor die Figur, in die Hand) |
| `inventory shot <datei.png>` | Bild des Fensters |

Das Inventar ist wie das Pausenmenü ein Rohbau in Qt Quick (`qml/InventoryWindow.qml`), ohne eigene Gestaltung: kein
eigenes Fenster mit Rahmen, sondern fest in der Mitte über dem leicht grau abgedunkelten Spiel. Jedes Kästchen zeigt eine
kleine Zeichnung des Teils (schräg von oben, in Materialfarbe), darunter den Namen; Material und Gewicht im Tooltip.
Ohne Hauptmenü (`--world`, `--script`) erscheint ein einfaches Fenster im Spiel.
