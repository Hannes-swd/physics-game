// Auswahlfenster des Spiel-Modus für Bauvorlagen (B) und Rezepte (C). Wie das Inventar ein Teil des Hauptmenüs
// (Qt Quick, qml/CatalogWindow.qml): fest in der Mitte über dem grau abgedunkelten Spiel, ein Kasten je Vorlage mit einer
// kleinen Zeichnung. Die Zeichnung rechnet App aus (Flächen schon projiziert und schattiert); was man wählt, holt das
// Spiel jedes Bild mit poll() ab. Ohne Hauptmenü wählt man wie früher mit Mausrad und 1 … 0.
#pragma once
#include <string>
#include <vector>

class CatalogUi {
public:
    // Eine Fläche der Zeichnung: Ecken x0 y0 x1 y1 … im Kasten (0 … 1, y nach unten), Farbe "#rrggbb"
    struct Poly {
        std::vector<float> pts;
        std::string color;
    };
    struct Item {
        std::string label, info;  // info: was es braucht
        std::vector<Poly> polys;  // von hinten nach vorne
    };
    struct Event {
        enum Kind { None, Close, Choose } kind = None;
        int index = -1;  // Choose: welcher Kasten
    };
    virtual ~CatalogUi() = default;

    // gameWindow: HWND des Spielfensters; closeKey: Taste, die es (neben Esc) wieder schließt ("B", "C")
    virtual void open(void* gameWindow, const std::string& title, const std::string& closeKey, const std::vector<Item>& items,
                      int current) = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;
    virtual Event poll() = 0;                         // Qt-Ereignisse abarbeiten
    virtual bool grab(const std::string& file) = 0;  // Bild des Fensters als PNG
};
