// Befehlssprache: Befehlsfolgen (;), Blöcke { }, wait, wait until, if/then/else,
// repeat, print, set + Ausdrücke über Teile, Spieler und Variablen.
//
//   mode play ; wait 2 ; print 17.y, 17.v
//   if 17.v > 0.1 then { screenshot a.png ; print "sliding" } else print "still"
//   repeat 3 times { new box spruce 10 10 2 ; wait 0.5 }
//   set h = last.y ; print h ; position last 0 $h 0
#pragma once
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class CommandHost;
struct ScriptProgram;

struct ScriptFrame {
    const void* list = nullptr;  // Anweisungsliste (intern)
    size_t index = 0;
    long long repeatLeft = 1;
};

struct ScriptJob {
    std::shared_ptr<ScriptProgram> program;
    std::vector<ScriptFrame> stack;
    std::string output;     // gesammelte Ausgabe
    bool ok = true;         // false, sobald ein Fehler auftrat
    bool finished = false;
    bool waiting = false;       // in "warte" oder "warte bis"
    bool waitingUntil = false;  // in "warte bis"
    double wakeAt = 0;          // warte: weiter ab hier; warte bis: Zeitlimit
    double waitStart = 0;
};

// Text übersetzen. Bei Syntaxfehler: nullptr + err.
std::unique_ptr<ScriptJob> parseScript(const std::string& text, std::string* err);

// Weiter ausführen bis zum nächsten Warten. now = aktuelle Zeit (s). Rückgabe: true = fertig.
// onOutput bekommt jede Ausgabezeile sofort (z.B. für die Konsole).
bool stepScript(ScriptJob& job, CommandHost& host, double now,
                const std::function<void(const std::string&, bool)>& onOutput = nullptr);

// Komplett ausführen; Wartezeit lässt die Physik entsprechend lange laufen (ohne Fenster, Tests).
std::string runScriptSync(CommandHost& host, const std::string& text, bool* ok = nullptr);

std::string scriptHelp();
