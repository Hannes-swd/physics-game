# Fernsteuerung, Skripte und Betrieb ohne Fenster

## Das laufende Spiel von außen steuern

Sobald das Spiel läuft, wartet es auf dem eigenen Rechner auf Befehle (TCP-Port **7777**, nur `127.0.0.1`).
Andere Programme, Skripte oder ein KI-Assistent können so **live** alles tun, was auch in der Konsole geht:
Modus wechseln, Teile erstellen, verschieben, sägen, abrunden, die Spielfigur steuern, Werte auslesen, Fotos machen.

Das gilt schon im **Hauptmenü**: Mit `menu` liest man aus, wo man ist (Seite, Dialog, Weltliste), mit
`menu worlds`, `menu new <Name>`, `menu play <Welt>`, `menu join <Adresse>` usw. bedient man es, sofort und ohne Maus.
Die Verbindung bleibt beim Wechsel zwischen Menü und Spiel offen. Alle Menübefehle:
[15-Hauptmenue-und-Welten.md](15-Hauptmenue-und-Welten.md#menü-per-befehl-fernsteuerung-ki).

### Mit `--send`

```
Werkstatt.exe --send "status"
Werkstatt.exe --send "new sphere steel 5" "list"
Werkstatt.exe --send "mode play ; wait 2 ; print 17.x, 17.y"
```

- Jedes Argument nach `--send` ist eine Zeile (ein Befehl oder eine ganze Befehlsfolge).
- Die Zeilen werden nacheinander ausgeführt. Die nächste startet erst, wenn die vorige fertig ist (auch nach `wait`).
- Die Antworten erscheinen als Text, davor jeweils `> befehl`.
- Ohne Argumente liest `--send` die Befehle zeilenweise von der Eingabe:

```
type commands.txt | Werkstatt.exe --send               (cmd.exe)
Get-Content commands.txt | Werkstatt.exe --send        (PowerShell)
```

| Rückgabewert (Exit-Code) | Bedeutung |
|---|---|
| 0 | alles ausgeführt |
| 1 | mindestens eine Antwortzeile begann mit `Error` |
| 2 | keine Verbindung (läuft das Spiel?) |

**Anführungszeichen:** PowerShell gibt `"` innerhalb von Argumenten nicht zuverlässig weiter.
Für Text in Befehlen daher einfache Anführungszeichen benutzen:

```
Werkstatt.exe --send "if 17.y < 5 then print 'on the floor'"
```

### Was man im Spiel sieht

- Jeder Fernbefehl wird oben in der Bildmitte eingeblendet, darunter sein Ergebnis (rot bei Fehlern).
- In der Konsole (F1) stehen alle Fernbefehle mit `[remote]` davor.
- Auch bei minimiertem Fenster werden Befehle ausgeführt.

### Einem Assistenten (z. B. Claude) Zugriff geben

1. Spiel normal starten (`Werkstatt.exe`). Es reicht das Hauptmenü.
2. Dem Assistenten sagen, dass es läuft.
3. Er schickt Befehle mit `Werkstatt.exe --send "..."` und liest die Antworten: zuerst `menu` (wo bin ich?), dann z. B.
   `menu play 1` und danach Spielbefehle. Mit `screenshot bild.png` kann er sich im Spiel auch das Bild ansehen,
   nötig ist das nicht.

Beispiel einer kompletten Live-Sitzung:

```
Werkstatt.exe --send "mode play ; player position -230 0 -160 ; player look 17 ; wait 1.5 ; print 17.x, 17.y, 17.v"
Werkstatt.exe --send "player walk forward 0.4 ; wait 0.8 ; player grab 17 ; wait 0.6 ; print player.holding"
Werkstatt.exe --send "player turn 30 10 ; player throw ; wait until 17.resting max 6 ; print 17.x, 17.y, 17.z"
Werkstatt.exe --send "mode build ; screenshot result.png"
```

### Protokoll (für eigene Programme)

| | |
|---|---|
| Verbindung | TCP zu `127.0.0.1:7777` (anderer Port: Spiel mit `--port N` starten) |
| Anfrage | eine Zeile UTF-8-Text, abgeschlossen mit `\n` |
| Antwort | beliebig viele Zeilen, danach eine Zeile `<<end>>` |
| Reihenfolge | Pro Verbindung wird eine Zeile nach der anderen ausgeführt. Weitere Zeilen werden gepuffert. |
| Dauer | Die Antwort kommt erst, wenn die Befehlsfolge fertig ist (z. B. nach `wait 5`) |

PowerShell:

```powershell
$c = [System.Net.Sockets.TcpClient]::new("127.0.0.1", 7777)
$s = $c.GetStream(); $w = [System.IO.StreamWriter]::new($s); $r = [System.IO.StreamReader]::new($s)
$w.WriteLine("new box oak 30 10 3 ; print last.mass"); $w.Flush()
while (($line = $r.ReadLine()) -ne "<<end>>") { $line }
$c.Close()
```

Python:

```python
import socket
s = socket.create_connection(("127.0.0.1", 7777))
f = s.makefile("rw", encoding="utf-8", newline="\n")
f.write("mode play ; wait 1 ; print 17.v\n"); f.flush()
for line in f:
    if line.strip() == "<<end>>": break
    print(line, end="")
```

**Sicherheit:** Der Port ist nur vom eigenen PC aus erreichbar (nicht aus dem Netzwerk). Es gibt kein Passwort:
Jedes Programm auf diesem PC kann Befehle schicken, solange das Spiel läuft.

## Skripte beim Start

```
Werkstatt.exe --script commands.txt
```

- Jede Zeile ist ein Befehl oder eine Befehlsfolge. Zeilen mit `#` am Anfang sind Kommentare.
- Die Zeilen laufen nacheinander **im laufenden Spiel**: `wait` in einer Zeile verzögert auch die folgenden Zeilen.
- Die Ausgaben erscheinen im Terminal und in der Konsole.

Beispiel `commands.txt`:

```
# Versuchsaufbau
scene empty
new box oak 100 35 2 0 30 0
rotation last 0 0 30
fix last
new box steel 6 6 6 20 50 0 ; rotation last 0 0 30
mode play
wait until last.resting max 10
print last.x, last.y
screenshot ramp.png
```

## Ohne Fenster (`--headless`)

```
Werkstatt.exe --headless --script commands.txt
Werkstatt.exe --headless --demo                     (Befehle von der Tastatur, Ende mit Strg+Z, Enter)
type commands.txt | Werkstatt.exe --headless
```

- Keine Grafik, kein Fenster: Geometrie, Bearbeitung, Physik, Zeichnungen (SVG) und Speichern funktionieren.
- Die Welt ist im Spiel-Zustand: **Zeit vergeht nur bei `wait` und `sim`**, dann läuft die Physik genau so lange (sehr schnell, nicht in Echtzeit).
- Befehle, die ein Fenster brauchen (`view`, `screenshot`, `camera`, `player`, `window`, `tool`, `display`), melden das.
- Exit-Code 1, wenn ein Befehl fehlschlug.
- `--demo` lädt die Beispiel-Werkstatt, sonst ist die Welt leer.

## Alle Kommandozeilen-Optionen

| Option | Bedeutung |
|---|---|
| *(keine)* | Hauptmenü (Weltliste, Mehrspieler, Einstellungen), siehe [15](15-Hauptmenue-und-Welten.md) |
| `--world <Name oder Ordner>` | diese Welt direkt öffnen |
| `--no-menu` | ohne Hauptmenü: Spiel mit Fenster und Beispiel-Werkstatt (wie früher) |
| `--menu-shot <bild.png> [Seite]` | Hauptmenü öffnen (`main`, `worlds`, `new-world` = Dialog „Neue Welt“, `multiplayer`, `settings`, `loading` = Ladeseite), Bild speichern, beenden |
| `--empty` | mit leerer Werkstatt starten |
| `--script <file>` | Befehle aus der Datei nacheinander ausführen |
| `--view <view>` | Startansicht (`perspective`, `top`, `front`, `side`, `quad`) |
| `--play` | direkt im Spiel-Modus starten |
| `--port <N>` | Port der Fernsteuerung (Standard 7777) |
| `--shot <image.png>` | nach `--frames` Bildern ein Foto speichern und beenden |
| `--frames <N>` | siehe `--shot` (Standard 30) |
| `--validate` | Vulkan-Fehlerprüfung einschalten (für Entwickler) |
| `--send [command ...]` | Befehle an ein laufendes Spiel schicken (siehe oben) |
| `--headless [--demo]` | ohne Fenster (siehe oben) |
| `--selftest` | automatische Prüfungen von Geometrie, Bearbeitung, Physik, Kollision, Reibung, Befehlssprache, Verbindungen, Bruch, Luft, Zeichnung, Speichern, Wärme, Wasser (auch Meeresgrund und Land), Gelände, Flüsse, Bäume und der Beispielwelt (173 Prüfungen) |
| `--bench [scene]` | Physik-Messung: Rechenzeit, Eindringtiefe, Rettungen und Messwerte in elf Szenen, alte gegen neue Einstellungen (mit Szene: nur Szenen, deren Name das Wort enthält) |

Wird das Programm per Doppelklick gestartet, blendet es das leere Konsolenfenster aus.
Aus einem Terminal gestartet, bleiben Meldungen dort sichtbar.
