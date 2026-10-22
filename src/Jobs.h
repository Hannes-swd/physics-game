// Hilfs-Threads der Engine: Arbeit im Hintergrund (Kollisionsformen) und Schleifen auf alle Kerne verteilen.
// Die Physik (Jolt) hat ihren eigenen Pool; diese Threads laufen mit niedrigerer Priorität, damit das Bild nicht ruckelt.
#pragma once
#include <cstddef>
#include <functional>

namespace jobs {

// Arbeit im Hintergrund erledigen (Reihenfolge wie übergeben)
void submit(std::function<void()> task);
// fn(i) für i = 0 .. count-1, verteilt auf die Hilfs-Threads und den aufrufenden Thread. Kehrt zurück, wenn alles fertig ist.
// Sind die Hilfs-Threads beschäftigt, rechnet der Aufrufer allein weiter (kein Warten, keine Verklemmung).
void parallelFor(size_t count, const std::function<void(size_t)>& fn);
size_t threads();     // Anzahl Hilfs-Threads
size_t queued();      // wartende Hintergrund-Aufgaben

} // namespace jobs
