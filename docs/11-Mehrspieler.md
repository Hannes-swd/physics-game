# Mehrspieler im LAN

Mehrere Leute im selben Netz (WLAN oder Kabel) bauen und spielen in **einer** Werkstatt.
Einer ist der **Gastgeber**: Sein Rechner berechnet alles – Physik, Werkzeuge, Befehle, Brüche.
Die **Mitspieler** sehen seine Welt, laufen mit eigener Spielfigur herum, tragen Teile, sägen, bohren und bauen mit.
Ihre Eingaben gehen zum Gastgeber, die Ergebnisse kommen zurück.

## Spiel öffnen (Gastgeber)

**Aus dem Hauptmenü:** *Spielen* → Welt auswählen → **Im LAN öffnen**. Die Welt öffnet sich und ist gleich für
andere freigegeben (das Fenster *Mehrspieler (LAN)* ist offen). Per Befehl: `menu lan <Welt>`.

**Im Spiel:**

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

**Aus dem Hauptmenü:** Seite **Mehrspieler**. Oben steht der eigene Name. Darunter erscheinen die offenen Spiele im Netz
(sie werden gesucht, solange die Seite offen ist): einmal klicken übernimmt die Adresse, Doppelklick tritt sofort bei.
Oder die Adresse eintippen → **Beitreten**. Per Befehl: `menu games`, `menu join <Adresse|Nummer|Name>`.

Danach öffnet sich das Spielfenster mit einem **Ladebildschirm**:

| Anzeige | Bedeutung |
|---|---|
| Verbinde mit ... | die Verbindung wird aufgebaut |
| Warte, bis ... dich zulässt | der Gastgeber muss den Beitritt noch bestätigen |
| Welt wird übertragen: N Teile | der Gastgeber schickt seine Welt; fertig, sobald nach den Teilen die erste Lage kommt |

Mit **Abbrechen** (oder `menu main`) geht es zurück ins Hauptmenü. Lehnt der Gastgeber ab oder antwortet niemand,
landet man ebenfalls wieder im Hauptmenü, und dort steht die Meldung. Per Befehl liest `menu` den Zustand aus
(`loading       join: Warte, bis ... dich zulässt ...`).

**Im Spiel:**

1. Menü **Mehrspieler → LAN-Spiel beitreten...**
2. Oben den eigenen Namen eintragen (vorbelegt mit dem Namen aus den Einstellungen bzw. dem Windows-Benutzernamen).
3. Offene Spiele im Netz erscheinen nach etwa einer Sekunde in der Liste → **Beitreten**.
   Steht das Spiel nicht in der Liste (z.B. anderes Teilnetz), die Adresse des Gastgebers eintippen → **Verbinden**
   (anderer Port: `192.168.1.20:7780`).
4. Hat der Gastgeber zugestimmt, wird die eigene Welt durch die des Gastgebers ersetzt. Ist gerade eine Welt aus der
   Weltliste offen, wird sie vorher gespeichert; was man danach beim Gastgeber baut, landet nicht in ihr.

**Spiel verlassen** trennt die Verbindung. Die Welt bleibt als eigene Kopie stehen, man kann allein weiterbauen
(oder mit `save` als Szene speichern).

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

Aus dem Hauptmenü (auch per Fernsteuerung, [15](15-Hauptmenue-und-Welten.md#menü-per-befehl-fernsteuerung-ki)):

```
menu lan "Meine Welt"                   Gastgeber: Welt öffnen und gleich freigeben
menu games                              Mitspieler: offene Spiele im Netz (nummeriert)
menu join 1                             Mitspieler: dem ersten Spiel der Liste beitreten (oder Adresse/Name)
```

Zwei Spiele auf **einem** Rechner (zum Ausprobieren): das zweite mit eigenem Fernsteuerungs-Port starten
(`Werkstatt.exe --port 7778`) und dort `menu join 127.0.0.1`. Befehle an das zweite: `Werkstatt.exe --port 7778 --send "..."`
(`--port` vor `--send`).

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
