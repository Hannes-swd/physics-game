# Tiere

Alle Modelle in diesem Ordner stammen von **Quaternius** (https://quaternius.com) und stehen unter
**CC0 1.0** (gemeinfrei, https://creativecommons.org/publicdomain/zero/1.0/): frei nutzbar, auch in verkauften Spielen,
ohne Namensnennung. Heruntergeladen über Poly Pizza (https://poly.pizza), dort ist bei jedem Modell „CC0 1.0“ angegeben.

| Datei | Modell (Poly Pizza) | Im Spiel als |
|---|---|---|
| deer.glb | Deer – https://poly.pizza/m/T6Cs7tmMHJ | Reh, Gämse (umgefärbt) |
| stag.glb | Stag – https://poly.pizza/m/tQdzbZ1Cmw | Rothirsch, Rentier (umgefärbt) |
| fox.glb | Fox – https://poly.pizza/m/Bc97C66HKi | Rotfuchs, Polarfuchs |
| wolf.glb | Wolf – https://poly.pizza/m/P1gU3Qkr9r | Wolf, Polarwolf |
| horse.glb | Horse – https://poly.pizza/m/qvTrSG9pZF | Wildpferd, Przewalski-Pferd |
| bull.glb | Bull – https://poly.pizza/m/a8PIIYwF7r | Wisent (umgefärbt) |
| alpaca.glb | Alpaca – https://poly.pizza/m/bCVFD48i2l | Lama |
| donkey.glb | Donkey – https://poly.pizza/m/qmX6nhnvp7 | Wildesel |
| sheep.glb | Sheep – https://poly.pizza/m/rgJXF570ZK | Mufflon (umgefärbt) |
| pig.glb | Pig – https://poly.pizza/m/u35l6uP5vj | Wildschwein (umgefärbt) |
| raccoon.glb | Raccoon – https://poly.pizza/m/xMmx6VQP3r | Waschbär |
| rat.glb | Rat – https://poly.pizza/m/iltq5bVNaV | Eichhörnchen (umgefärbt) |
| snake.glb | Snake – https://poly.pizza/m/x9x0viZs8V | Schlange, Sandotter |
| frog.glb | Frog – https://poly.pizza/m/9Z2V8fpazF | Frosch |
| spider.glb | Spider – https://poly.pizza/m/yRYJiAJyiM | Vogelspinne, Walzenspinne |
| crab.glb | Crab Enemy – https://poly.pizza/m/Gs3yfsV5lB | Krabbe |
| pigeon.glb | Pigeon – https://poly.pizza/m/9NGlBTpDEr | Taube |
| bat.glb | Bat – https://poly.pizza/m/hNO9XvjlKa | Fledermaus |

Unverändert bis auf `sheep.glb`, `pig.glb` und `raccoon.glb`: Sie färben sich im Original über eine kleine Farbtafel
(Textur). `tools/bake_atlas.py` hat daraus einfache Farbmaterialien gemacht, weil das Spiel nur Materialfarben liest.

Die Vögel am Himmel (Adler, Geier, Krähen, Möwen, Papageien, Stare) und die Flügel der auffliegenden Tauben sind keine
Dateien, sondern eine eingebaute Low-Poly-Form (src/AppAnimals.cpp).
