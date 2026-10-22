// Kleiner JSON-Leser und -Schreiber (Spielfigur glTF, Materialdatei). Objekte behalten die Reihenfolge ihrer Schlüssel.
#pragma once
#include <string>
#include <utility>
#include <vector>

struct Json {
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    double num = 0;
    std::string str;
    std::vector<Json> arr;
    std::vector<std::pair<std::string, Json>> obj;

    static const Json& none() {
        static const Json n;
        return n;
    }
    // Schlüssel als std::string, damit j[0] eindeutig der Index ist (0 wäre sonst auch ein Nullzeiger)
    const Json& operator[](const std::string& key) const {
        for (const auto& kv : obj)
            if (kv.first == key) return kv.second;
        return none();
    }
    const Json& operator[](size_t i) const { return i < arr.size() ? arr[i] : none(); }
    bool has(const std::string& key) const { return &(*this)[key] != &none(); }
    size_t size() const { return arr.size(); }
    int integer(int def = -1) const { return type == Number ? (int)num : def; }
    float number(float def = 0.0f) const { return type == Number ? (float)num : def; }

    // Aufbauen (zum Schreiben)
    static Json makeNumber(double v) {
        Json j;
        j.type = Number;
        j.num = v;
        return j;
    }
    static Json makeString(std::string s) {
        Json j;
        j.type = String;
        j.str = std::move(s);
        return j;
    }
    static Json makeArray() {
        Json j;
        j.type = Array;
        return j;
    }
    static Json makeObject() {
        Json j;
        j.type = Object;
        return j;
    }
    Json& set(const std::string& key, Json v) {
        obj.emplace_back(key, std::move(v));
        return obj.back().second;
    }
    Json& push(Json v) {
        arr.push_back(std::move(v));
        return arr.back();
    }
};

// Text lesen. false: err = "Zeile 12, Spalte 5: ..." (wenn err nicht null)
bool parseJson(const char* begin, const char* end, Json& out, std::string* err = nullptr);
bool parseJson(const std::string& text, Json& out, std::string* err = nullptr);
// Lesbar schreiben: Objekte mit einem Schlüssel je Zeile, Listen aus Zahlen oder Texten in einer Zeile
std::string writeJson(const Json& j, int indent = 2);
