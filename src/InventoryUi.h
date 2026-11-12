// Inventar des Spiel-Modus (E): der Bereich „Inventar“ des gemeinsamen Fensters mit Herstellen und Bauen (CatalogUi.h).
// Wie das Pausenfenster ein Teil des Hauptmenüs (Qt Quick, qml/BagWindow.qml): ein eigenes Fenster über dem Spielfenster.
// Es zeigt nur, was App ausrechnet (Kästchen im Raster, Gewicht); was man darin tut, holt das Spiel jedes Bild mit poll()
// ab. Ohne Hauptmenü zeigt App ein einfaches Fenster im Spiel.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "CatalogUi.h"

class InventoryUi {
public:
    // Ein Teil im Inventar: Rechteck im Raster (Zellen), Beschriftung
    struct Item {
        uint32_t key = 0;
        int x = 0, y = 0, w = 1, h = 1;
        std::string name, info;  // info: Material und Gewicht
        std::vector<CatalogUi::Poly> polys;  // Zeichnung des Teils im Kästchen (0 … 1, von hinten nach vorne)
    };
    struct Event {
        enum Kind { None, Close, TakeOut, Switch } kind = None;
        uint32_t key = 0;  // TakeOut: welches Teil
        int tab = 0;       // Switch: anderer Bereich des Fensters (1 Herstellen, 2 Bauen)
    };
    virtual ~InventoryUi() = default;

    // gameWindow: HWND des Spielfensters (das Inventar steht mittig davor). Ist das Fenster schon mit Herstellen oder
    // Bauen offen, wechselt es nur den Bereich
    virtual void open(void* gameWindow) = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;
    // Inhalt: Raster cols x rows, Teile, Zeile mit Gewicht (z.B. "12,4 / 30 kg")
    virtual void setContents(int cols, int rows, const std::vector<Item>& items, const std::string& summary) = 0;
    virtual Event poll() = 0;  // Qt-Ereignisse abarbeiten
    virtual bool grab(const std::string& file) = 0;  // Bild des Fensters als PNG
};
