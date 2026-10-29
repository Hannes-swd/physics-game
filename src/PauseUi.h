// Pausenfenster des Spiel-Modus (ESC): Weiterspielen, Modus wechseln, Einstellungen, Beenden.
// Mit Hauptmenü ist es ein Teil davon (Qt Quick, qml/PauseWindow.qml): ein eigenes kleines Fenster über dem
// Spielfenster, mit derselben Oberfläche und derselben Einstellungsseite wie das Hauptmenü. Solange es offen ist, ruft
// das Spiel jedes Bild poll() auf. Ohne Hauptmenü (Start mit --world, --script, ohne Qt) zeigt App ein einfaches
// Fenster im Spiel (AppUI.cpp).
#pragma once
#include <string>

class PauseUi {
public:
    enum class Action { None, Resume, SwitchMode, ToMenu, QuitGame };
    virtual ~PauseUi() = default;

    // gameWindow: HWND des Spielfensters (das Pausenfenster steht mittig davor). fromMenu: "Zurück zum Hauptmenü"
    // anbieten. note: Hinweis auf der Seite "Beenden" (z.B. ob die Welt gespeichert wird)
    virtual void open(void* gameWindow, bool fromMenu, const std::string& note) = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;
    // Qt-Ereignisse abarbeiten; Rückgabe: was gewählt wurde (None: nichts)
    virtual Action poll() = 0;
    // settings.json wurde geändert (Einstellungsseite): das Spiel liest sie neu. Setzt die Meldung zurück
    virtual bool takeSettingsChanged() = 0;

    // Befehle (Konsole, Fernsteuerung): Seite "pause", "settings" oder "quit" zeigen bzw. abfragen,
    // "menu set <key> <value>" wie im Hauptmenü, Bild des Fensters als PNG
    virtual void showPage(const std::string& page) = 0;
    virtual std::string page() const = 0;
    virtual std::string command(const std::string& line) = 0;
    virtual bool grab(const std::string& file) = 0;
};
