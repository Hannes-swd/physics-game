// Hauptmenü (Qt Quick, Oberfläche in qml/): Spielen (Weltliste), Mehrspieler, Einstellungen, Beenden.
// show() zeigt das Menüfenster, bis eine Welt geöffnet, einem LAN-Spiel beigetreten oder beendet wird. Danach läuft das
// Spiel in seinem eigenen Fenster (App); "Zurück zum Hauptmenü" ruft show() wieder auf. Beim Wechsel zeigt das Menü
// eine Ladeseite, bis das Spielfenster sein erstes Bild hat (hide()); danach gibt es kein Qt-Fenster mehr.
// Alles lässt sich auch per Befehl ("menu ...", Fernsteuerung) bedienen und auslesen, siehe docs/15.
#pragma once
#include <memory>
#include <string>

class RemoteServer;

struct MenuChoice {
    enum Kind { Quit, Play, Join } kind = Quit;
    std::string worldDir;  // Play: Ordner der Welt (UTF-8)
    bool lan = false;      // Play: gleich im LAN öffnen
    std::string address;   // Join: Adresse des Gastgebers
};

class MainMenu {
public:
    MainMenu(int& argc, char** argv);  // argc/argv müssen so lange leben wie das Menü (Qt)
    ~MainMenu();
    // message: Hinweis aus der letzten Sitzung (z.B. Beitritt fehlgeschlagen), leer: keiner.
    // remote: Fernsteuerung (Befehle "menu ..."), command: zuerst ausführen (im Spiel getippt, z.B. "menu worlds")
    // Bei Play/Join bleibt das Fenster danach mit einer Ladeseite stehen, bis hide() kommt (das Spielfenster ist da)
    MenuChoice show(const std::string& message, RemoteServer* remote = nullptr, const std::string& command = "");
    void hide();
    // --menu-shot: Seite öffnen (main, worlds, multiplayer, settings), Bild des Fensters speichern, beenden
    void setScreenshot(const std::string& file, const std::string& page) {
        shotFile_ = file;
        shotPage_ = page;
    }

private:
    std::string shotFile_, shotPage_;
    std::string lastWorld_;  // zuletzt geöffnete Welt: beim Zurückkommen ausgewählt

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
