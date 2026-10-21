# Mehrspieler im LAN

Mehrere Leute im selben Netz (WLAN oder Kabel) bauen und spielen in **einer** Werkstatt.
Einer ist der **Gastgeber**: Sein Rechner berechnet alles – Physik, Werkzeuge, Befehle, Brüche.
Die **Mitspieler** sehen seine Welt, laufen mit eigener Spielfigur herum, tragen Teile, sägen, bohren und bauen mit.
Ihre Eingaben gehen zum Gastgeber, die Ergebnisse kommen zurück.

## Spiel öffnen (Gastgeber)

1. Menü **Mehrspieler → Im LAN öffnen (Gastgeber)...**
2. Das Fenster *Mehrspieler (LAN)* zeigt die eigene Adresse im Netz (z.B. `192.168.1.20`).
   Mitspieler finden das Spiel auch ohne Adresse in ihrer Liste.
3. Möchte jemand beitreten, erscheint oben die **Beitrittsanfrage** mit *Zulassen* / *Ablehnen*
   (im Spiel-Modus vorher **Esc** drücken, um die Maus freizugeben).
   Wer jeden ohne Nachfrage hereinlassen will, schaltet *Mitspieler erst nach meiner Bestätigung zulassen* aus.

Im Fenster *Mehrspieler → Spieler einladen / verwalten...* sieht der Gastgeber alle Mitspieler
(Adresse, ob sie bauen oder spielen) und kann sie mit *Entfernen* hinauswerfen.
**LAN-Spiel beenden** trennt alle; die Welt bleibt beim Gastgeber unverändert.

## Beitreten (Mitspieler)

1. Menü **Mehrspieler → LAN-Spiel beitreten...**
2. Oben den eigenen Namen eintragen (vorbelegt mit dem Windows-Benutzernamen).
3. Offene Spiele im Netz erscheinen nach etwa einer Sekunde in der Liste → **Beitreten**.
   Steht das Spiel nicht in der Liste (z.B. anderes Teilnetz), die Adresse des Gastgebers eintippen → **Verbinden**
   (anderer Port: `192.168.1.20:7780`).
4. Hat der Gastgeber zugestimmt, wird die eigene Welt durch die des Gastgebers ersetzt.

**Spiel verlassen** trennt die Verbindung. Die Welt bleibt als eigene Kopie stehen, man kann allein weiterbauen
(oder mit `save` speichern).

## Was wo passiert

| | Gastgeber | Mitspieler |
|---|---|---|
| Physik, Spielfiguren, Tragen, Werfen, Brüche | rechnet | sieht das Ergebnis |
| Teile verschieben, drehen, absetzen (Bau-Modus) | rechnet | bewegt das Teil sofort selbst, schickt die neue Lage |
| Sägen, Bohren, Fräsen, Verbinden, Eigenschaften, Erstellen | rechnet | schickt einen Befehl, bekommt das Ergebnis |
| Konsole (F1) | wie immer | die meisten Befehle laufen beim Gastgeber (siehe unten) |
| Rückgängig / Wiederholen | eine gemeinsame Liste | macht die letzte Änderung *irgendeines* Spielers rückgängig |
| Auswahl, Kamera, Ansichten, Werkzeug, Fenster | eigene | eigene |
| Bau- oder Spiel-Modus (Tab) | eigener | eigener |

- Die Physik läuft, sobald **irgendjemand** im Spiel-Modus ist. Baut der Gastgeber gerade, während ein Mitspieler
  herumläuft, fallen und rollen die Teile also trotzdem.
- Andere Spieler sieht man als farbige Figur mit Namen; wer gerade baut, erscheint als schwebende Kamera (*baut*).
- Meldungen wie Brüche oder „X ist beigetreten“ erscheinen bei allen.

## Konsole beim Mitspieler

Eine Zeile läuft **auf dem eigenen Rechner**, wenn sie nur aus einem dieser Befehle besteht:
`help`, `materials`, `mode`, `view`, `camera`, `screenshot`, `tool`, `window`, `display`, `log`, `select`, `drawing`,
`save`, `edges`, `list`, `chunks`, `quit`.
Alles andere – auch jede Befehlsfolge mit `;` oder `{ }` – geht zum Gastgeber und läuft dort mit der **eigenen Auswahl**
(`selected`) und neue Teile entstehen vor der **eigenen Kamera**. Die Antwort erscheint in der eigenen Konsole.

Nicht erlaubt für Mitspieler: `load` und `import` (die Datei liegt auf dem eigenen Rechner),
`player ...` (gilt nur für die Figur des Gastgebers) sowie Befehle, die beim Gastgeber Dateien schreiben würden.
`save` und `drawing ... datei.svg` speichern beim Mitspieler eine Kopie auf dem **eigenen** Rechner.

Die Fernsteuerung (`--send`, Port 7777) wirkt immer auf das Spiel auf dem eigenen Rechner – beim Mitspieler gehen
die Befehle dann genauso an den Gastgeber weiter.

## Per Befehl

Alles aus dem Menü geht auch in der Konsole oder per `--send`:

| Befehl | Wirkung |
|---|---|
| `lan` / `lan status` | Rolle, eigene Adressen, Spieler (Position, was sie tragen), wartende Beitrittsanfragen; ohne Spiel: gefundene Spiele |
| `lan host` | Spiel im LAN öffnen |
| `lan join 192.168.1.20` | beitreten (`:port` anhängen für einen anderen Port) |
| `lan leave` | Spiel verlassen bzw. als Gastgeber schließen |
| `lan accept [name\|all]` · `lan decline <name>` · `lan kick <name>` | Gastgeber: zulassen, ablehnen, entfernen |
| `lan confirm on\|off` | Gastgeber: Beitritt nur nach Bestätigung |
| `lan name <text>` | eigener Name (vor dem Öffnen/Beitreten) |

```
lan host ; lan confirm off              Gastgeber, jeder darf sofort rein
lan join 192.168.1.20                   Mitspieler
```

## Netzwerk

| Port | Zweck |
|---|---|
| TCP **7780** | Spiel (Gastgeber wartet hier) |
| UDP **7781** | Spielesuche im LAN (Broadcast) |

- Beim ersten Öffnen fragt die **Windows-Firewall**, ob Werkstatt im Netz erreichbar sein darf:
  für *private Netzwerke* zulassen. Ohne diese Freigabe sehen Mitspieler das Spiel nicht.
- Beide Seiten brauchen **dieselbe Version** des Spiels; sonst lehnt der Gastgeber den Beitritt ab.
- Gedacht für das lokale Netz. Über das Internet bräuchte es eine Portweiterleitung am Router und ist nicht vorgesehen.
- Übertragen werden nur Änderungen: neue oder bearbeitete Teile komplett (mit fertiger Kollisionsform), sonst nur die
  Lage bewegter Teile (bis 60-mal je Sekunde), Verbindungen und die Spielfiguren.
- Zum Testen auf einem Rechner: zweimal `Werkstatt.exe` starten, im ersten *Im LAN öffnen*, im zweiten mit `127.0.0.1` verbinden.
