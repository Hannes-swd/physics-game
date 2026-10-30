// Inventarfenster des Spiel-Modus (E). Wie das Pausenfenster ein Teil des Hauptmenüs (Qt Quick, qml/InventoryWindow.qml):
// ein eigenes kleines Fenster über dem Spielfenster. Es zeigt nur, was App ausrechnet (Kästchen im Raster, Gewicht);
// was man darin tut, holt das Spiel jedes Bild mit poll() ab. Ohne Hauptmenü zeigt App ein einfaches Fenster im Spiel.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

class InventoryUi {
public:
    // Ein Teil im Inventar: Rechteck im Raster (Zellen), Beschriftung
    struct Item {
        uint32_t key = 0;
        int x = 0, y = 0, w = 1, h = 1;
        std::string name, info;  // info: Material und Gewicht
    };
    struct Event {
        enum Kind { None, Close, TakeOut } kind = None;
        uint32_t key = 0;  // TakeOut: welches Teil
    };
    virtual ~InventoryUi() = default;

    virtual void open(void* gameWindow) = 0;  // gameWindow: HWND des Spielfensters (das Inventar steht mittig davor)
    virtual void close() = 0;
    virtual bool isOpen() const = 0;
    // Inhalt: Raster cols x rows, Teile, Zeile mit Gewicht (z.B. "12,4 / 30 kg")
    virtual void setContents(int cols, int rows, const std::vector<Item>& items, const std::string& summary) = 0;
    virtual Event poll() = 0;  // Qt-Ereignisse abarbeiten
    virtual bool grab(const std::string& file) = 0;  // Bild des Fensters als PNG
};
