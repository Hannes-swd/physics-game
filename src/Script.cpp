#include "Script.h"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <map>

#include "Commands.h"
#include "Materials.h"
#include "World.h"

namespace {

constexpr double kPi = 3.14159265358979323846;

// ============================================================================ Werte und Ausdrücke

struct Value {
    bool isStr = false;
    double num = 0;
    std::string str;
};

struct Expr {
    enum Kind { Num, Str, Var, Ref, Unary, Binary, Call } kind = Num;
    double num = 0;
    std::string str;   // Text / Variablenname / Eigenschaft
    std::string base;  // bei Ref: Teilnummer oder Name (auswahl, letzte, spieler ...)
    std::string op;
    std::shared_ptr<Expr> a, b;
    std::vector<std::shared_ptr<Expr>> args;  // Funktionsaufruf
};
using ExprPtr = std::shared_ptr<Expr>;

std::map<std::string, Value>& vars() {
    static std::map<std::string, Value> v;
    return v;
}

std::string fmtValue(const Value& v) {
    if (v.isStr) return v.str;
    if (std::abs(v.num - std::round(v.num)) < 1e-9 && std::abs(v.num) < 1e15) {
        char b[32];
        std::snprintf(b, sizeof(b), "%.0f", v.num);
        return b;
    }
    char b[48];
    std::snprintf(b, sizeof(b), "%.4f", v.num);
    std::string s = b;
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s;
}

bool truthy(const Value& v) { return v.isStr ? !v.str.empty() : std::abs(v.num) > 1e-12; }

Value numV(double d) {
    Value v;
    v.num = d;
    return v;
}
Value strV(const std::string& s) {
    Value v;
    v.isStr = true;
    v.str = s;
    return v;
}

// Eigenschaften eines Teils
bool objectProp(World& w, uint32_t id, const std::string& p, Value& out, std::string& err) {
    Object* o = w.find(id);
    if (p == "exists") {
        out = numV(o ? 1 : 0);
        return true;
    }
    if (!o) {
        err = "part #" + std::to_string(id) + " does not exist";
        return false;
    }
    glm::dvec3 v(0), av(0);
    bool active = false;
    if (o->body != kNoBody) {
        v = glm::dvec3(w.physics().linearVelocity(o->body));
        av = glm::dvec3(w.physics().angularVelocity(o->body));
        active = w.physics().isActive(o->body);
    }
    glm::dvec3 size = (o->bbMax - o->bbMin) * 100.0;
    glm::dvec3 eul = glm::degrees(glm::eulerAngles(o->rot));
    if (p == "id") out = numV(o->id);
    else if (p == "name") out = strV(o->name);
    else if (p == "material") out = strV(materials()[o->material].name);
    else if (p == "x") out = numV(o->pos.x * 100);
    else if (p == "y") out = numV(o->pos.y * 100);
    else if (p == "z") out = numV(o->pos.z * 100);
    else if (p == "vx") out = numV(v.x);
    else if (p == "vy") out = numV(v.y);
    else if (p == "vz") out = numV(v.z);
    else if (p == "v" || p == "speed") out = numV(glm::length(v));
    else if (p == "kmh") out = numV(glm::length(v) * 3.6);
    else if (p == "rpm") out = numV(glm::length(av) * 60.0 / (2 * kPi));
    else if (p == "mass") out = numV(o->mass);
    else if (p == "weight") out = numV(o->mass * 9.81);
    else if (p == "volume") out = numV(o->volume * 1e6);
    else if (p == "bottom") out = numV(o->wMin.y * 100);
    else if (p == "top") out = numV(o->wMax.y * 100);
    else if (p == "length" || p == "sizex") out = numV(size.x);
    else if (p == "height" || p == "sizey") out = numV(size.y);
    else if (p == "width" || p == "sizez") out = numV(size.z);
    else if (p == "rx") out = numV(eul.x);
    else if (p == "ry") out = numV(eul.y);
    else if (p == "rz") out = numV(eul.z);
    else if (p == "resting") out = numV(!active ? 1 : 0);  // Physik hat das Teil schlafen gelegt (wirklich zur Ruhe gekommen)
    else if (p == "still") out = numV((glm::length(v) < 0.01 && glm::length(av) < 0.05) ? 1 : 0);  // gerade fast keine Bewegung
    else if (p == "moving") out = numV((glm::length(v) >= 0.01 || glm::length(av) >= 0.05) ? 1 : 0);
    else if (p == "active") out = numV(active ? 1 : 0);
    else if (p == "fixed") out = numV(o->fixed ? 1 : 0);
    else if (p == "energy") out = numV(0.5 * o->mass * glm::dot(v, v) + o->mass * 9.81 * o->pos.y);
    else if (p == "pieces") out = numV((double)o->solid.pieces.size());
    else if (p == "hulls") out = numV((double)o->coll.hulls.size());
    else if (p == "bores") out = numV((double)o->coll.bores.size());
    else if (p == "pins") out = numV((double)o->coll.cylinders.size());
    else if (p == "lubricant") out = strV(lubricants()[std::clamp(o->lubricant, 0, (int)lubricants().size() - 1)].name);
    else if (p == "worn") out = numV(o->wornVolume * 1e9);  // mm³
    else if (p == "temp" || p == "temperature") out = numV(o->heat.empty() ? w.ambient() : o->heatInfo.tMean);  // °C
    else if (p == "tempmax") out = numV(o->heat.empty() ? w.ambient() : o->heatInfo.tMax);
    else if (p == "tempmin") out = numV(o->heat.empty() ? w.ambient() : o->heatInfo.tMin);
    else if (p == "burning") out = numV(o->heat.empty() ? 0 : o->heatInfo.burning);  // brennende Stellen
    else if (p == "charred") out = numV(o->heat.empty() ? 0 : o->heatInfo.chr * 100.0);  // %
    else if (p == "molten") out = numV(o->heat.empty() ? 0 : o->heatInfo.melt * 100.0);  // %
    else if (p == "boreload" || p == "boreforce" || p == "pressure" || p == "wear") {
        double m = 0;
        for (size_t i = 0; i < o->coll.bores.size() && i < o->boreRt.size(); ++i) {
            const auto& rt = o->boreRt[i];
            const glm::dvec3 ax = o->rot * o->coll.bores[i].axis;
            const double F = glm::length(rt.force - ax * glm::dot(rt.force, ax));
            if (p == "boreload") m = std::max(m, 100.0 * rt.load);
            else if (p == "boreforce") m = std::max(m, std::max(F, rt.load_N));
            else if (p == "pressure") m = std::max(m, rt.pressure);
            else if (i < o->boreWear.size()) m = std::max(m, o->boreWear[i] * 1000.0);  // mm
        }
        out = numV(m);
    }
    else if (p == "edges") out = numV((double)w.edgesOf(*o).size());
    else if (p == "airforce") out = numV(glm::length(o->aeroForce));
    else if (p == "airx") out = numV(o->aeroForce.x);
    else if (p == "airy") out = numV(o->aeroForce.y);
    else if (p == "airz") out = numV(o->aeroForce.z);
    else if (p == "airtorque") out = numV(glm::length(o->aeroTorque));
    else if (p == "overlap") {  // wie tief steckt das Teil in anderen (mm)
        double d = 0;
        for (auto& [oid, depth] : w.overlapsOf(*o, 0.0)) d = std::max(d, depth);
        out = numV(d * 1000);
    } else if (p == "joints" || p == "load") {
        int n = 0;
        double load = 0;
        for (auto& j : w.joints())
            if (j->a == o->id || j->b == o->id) {
                ++n;
                load = std::max(load, j->load);
            }
        out = numV(p == "joints" ? n : load * 100.0);
    }
    else {
        err = "unknown property '" + p +
              "' (x y z v vx vy vz kmh rpm mass weight volume bottom top length height width rx ry rz resting still moving active fixed "
              "energy pieces hulls edges overlap airforce airx airy airz airtorque joints load name material exists)";
        return false;
    }
    return true;
}

bool eval(const Expr& e, CommandHost& host, Value& out, std::string& err);

bool lookupName(const std::string& n, CommandHost& host, Value& out, std::string& err) {
    auto it = vars().find(n);
    if (it != vars().end()) {
        out = it->second;
        return true;
    }
    double d = 0;
    std::string s;
    if (host.queryValue(n, d, s)) {
        out = s.empty() ? numV(d) : strV(s);
        return true;
    }
    World& w = host.world();
    if (n == "parts") out = numV((double)w.objects().size());
    else if (n == "time") out = numV(w.simTime());
    else if (n == "selected" || n == "sel") out = numV(host.selectedId());
    else if (n == "last") out = numV(w.lastCreatedId());
    else if (n == "chunks") out = numV((double)w.grid().chunkCount());
    else if (n == "broken") out = numV(w.brokenCount());
    else if (n == "joints") out = numV((double)w.joints().size());
    else if (n == "rescued") out = numV(w.rescueCount());
    else if (n == "ambient") out = numV(w.ambient());
    else if (n == "fires") out = numV((double)w.fires().size());
    else if (n == "pi") out = numV(kPi);
    else {
        err = "unknown name '" + n + "'";
        return false;
    }
    return true;
}

bool eval(const Expr& e, CommandHost& host, Value& out, std::string& err) {
    switch (e.kind) {
    case Expr::Num: out = numV(e.num); return true;
    case Expr::Str: out = strV(e.str); return true;
    case Expr::Var: return lookupName(e.str, host, out, err);
    case Expr::Ref: {
        const std::string& b = e.base;
        if (b == "player" || b == "camera" || b == "world") {
            double d = 0;
            std::string s;
            if (host.queryValue(b + "." + e.str, d, s)) {
                out = s.empty() ? numV(d) : strV(s);
                return true;
            }
            err = "'" + b + "." + e.str + "' is not available here";
            return false;
        }
        uint32_t id = 0;
        if (!b.empty() && std::isdigit((unsigned char)b[0])) {
            id = (uint32_t)std::stoul(b);
        } else {
            Value v;
            if (!lookupName(b, host, v, err)) return false;
            if (v.isStr) {
                err = "'" + b + "' is not a part number";
                return false;
            }
            id = (uint32_t)std::llround(v.num);
        }
        return objectProp(host.world(), id, e.str, out, err);
    }
    case Expr::Call: {
        std::vector<double> x;
        for (const auto& a : e.args) {
            Value v;
            if (!eval(*a, host, v, err)) return false;
            if (v.isStr) {
                err = "function '" + e.str + "' needs numbers";
                return false;
            }
            x.push_back(v.num);
        }
        auto need = [&](size_t n) {
            if (x.size() == n) return true;
            err = "function '" + e.str + "' needs " + std::to_string(n) + " value(s)";
            return false;
        };
        const std::string& f = e.str;
        if (f == "abs") {
            if (!need(1)) return false;
            out = numV(std::abs(x[0]));
        } else if (f == "sqrt") {
            if (!need(1)) return false;
            out = numV(std::sqrt(std::max(0.0, x[0])));
        } else if (f == "round") {
            if (!need(1)) return false;
            out = numV(std::round(x[0]));
        } else if (f == "sin" || f == "cos" || f == "tan") {
            if (!need(1)) return false;
            double r = x[0] * kPi / 180.0;
            out = numV(f == "sin" ? std::sin(r) : f == "cos" ? std::cos(r) : std::tan(r));
        } else if (f == "min" || f == "max") {
            if (x.empty()) return need(1);
            double r = x[0];
            for (double v : x) r = f == "min" ? std::min(r, v) : std::max(r, v);
            out = numV(r);
        } else if (f == "distance" || f == "dist") {  // Abstand zweier Teile (Schwerpunkte) in cm
            if (!need(2)) return false;
            Object* a = host.world().find((uint32_t)std::llround(x[0]));
            Object* b = host.world().find((uint32_t)std::llround(x[1]));
            if (!a || !b) {
                err = "distance: part does not exist";
                return false;
            }
            out = numV(glm::length(a->pos - b->pos) * 100.0);
        } else {
            err = "unknown function '" + f + "' (abs sqrt round sin cos tan min max distance)";
            return false;
        }
        return true;
    }
    case Expr::Unary: {
        Value a;
        if (!eval(*e.a, host, a, err)) return false;
        if (e.op == "not") out = numV(truthy(a) ? 0 : 1);
        else out = numV(-a.num);
        return true;
    }
    case Expr::Binary: {
        Value a;
        if (!eval(*e.a, host, a, err)) return false;
        if (e.op == "and" && !truthy(a)) {
            out = numV(0);
            return true;
        }
        if (e.op == "or" && truthy(a)) {
            out = numV(1);
            return true;
        }
        Value b;
        if (!eval(*e.b, host, b, err)) return false;
        const std::string& op = e.op;
        if (op == "and" || op == "or") {
            out = numV(truthy(b) ? 1 : 0);
            return true;
        }
        if (a.isStr || b.isStr) {
            std::string sa = fmtValue(a), sb = fmtValue(b);
            if (op == "+") out = strV(sa + sb);
            else if (op == "==") out = numV(normalizeWord(sa) == normalizeWord(sb));
            else if (op == "!=") out = numV(normalizeWord(sa) != normalizeWord(sb));
            else {
                err = "text cannot be combined with '" + op + "'";
                return false;
            }
            return true;
        }
        double x = a.num, y = b.num;
        if (op == "+") out = numV(x + y);
        else if (op == "-") out = numV(x - y);
        else if (op == "*") out = numV(x * y);
        else if (op == "/") {
            if (std::abs(y) < 1e-300) {
                err = "division by 0";
                return false;
            }
            out = numV(x / y);
        } else if (op == "<") out = numV(x < y);
        else if (op == ">") out = numV(x > y);
        else if (op == "<=") out = numV(x <= y);
        else if (op == ">=") out = numV(x >= y);
        else if (op == "==") out = numV(std::abs(x - y) < 1e-9);
        else if (op == "!=") out = numV(std::abs(x - y) >= 1e-9);
        return true;
    }
    }
    return false;
}

// ============================================================================ Anweisungen

struct Stmt {
    enum Kind { Command, Wait, WaitUntil, If, Repeat, Show, Set, Block } kind = Command;
    std::string text;               // Befehl / Variablenname
    ExprPtr expr;                   // Bedingung, Wert, Anzahl, Sekunden
    ExprPtr maxExpr;                // warte bis ... max
    std::vector<ExprPtr> exprs;     // zeige
    std::vector<std::string> texts; // zeige: Quelltext der Ausdrücke
    std::vector<Stmt> body, elseBody;
};

const char* kReserved[] = {"then", "else", "times", "max", "and", "or", "not", "until"};

class Parser {
public:
    explicit Parser(const std::string& s) : s_(s) {}
    std::string error;

    // Einzelnen Ausdruck übersetzen (für $(...) in Befehlen)
    ExprPtr parseWhole() {
        ExprPtr e = parseExpr();
        ws();
        if (p_ < s_.size()) fail("unexpected character in expression");
        return e;
    }

    std::vector<Stmt> parseBlock(bool inBraces) {
        std::vector<Stmt> list;
        for (;;) {
            ws();
            while (p_ < s_.size() && s_[p_] == ';') {
                ++p_;
                ws();
            }
            if (p_ >= s_.size() || !error.empty()) break;
            if (s_[p_] == '}') {
                if (!inBraces) fail("'}' without matching '{'");
                break;
            }
            list.push_back(parseStmt());
            if (!error.empty()) break;
            ws();
            if (p_ < s_.size() && s_[p_] != ';' && s_[p_] != '}') fail("';' expected");
        }
        return list;
    }

private:
    const std::string& s_;
    size_t p_ = 0;

    void fail(const std::string& m) {
        if (!error.empty()) return;
        error = m + " (at character " + std::to_string(p_ + 1) + ": \"" + s_.substr(p_, 20) + "\")";
    }
    void ws() {
        while (p_ < s_.size() && std::isspace((unsigned char)s_[p_])) ++p_;
    }
    static bool identStart(unsigned char c) { return std::isalpha(c) || c == '_' || c >= 0x80; }
    static bool identChar(unsigned char c) { return std::isalnum(c) || c == '_' || c >= 0x80; }
    size_t wordEnd(size_t q) const {
        while (q < s_.size() && identChar((unsigned char)s_[q])) ++q;
        return q;
    }
    std::string peekWord() {
        ws();
        return normalizeWord(s_.substr(p_, wordEnd(p_) - p_));
    }
    bool acceptWord(const char* w) {
        ws();
        size_t e = wordEnd(p_);
        if (e > p_ && normalizeWord(s_.substr(p_, e - p_)) == w) {
            p_ = e;
            return true;
        }
        return false;
    }
    std::string readIdent() {
        ws();
        size_t e = wordEnd(p_);
        std::string r = normalizeWord(s_.substr(p_, e - p_));
        p_ = e;
        return r;
    }

    std::vector<Stmt> parseBody() {
        ws();
        if (p_ < s_.size() && s_[p_] == '{') {
            ++p_;
            std::vector<Stmt> b = parseBlock(true);
            ws();
            if (p_ < s_.size() && s_[p_] == '}') ++p_;
            else fail("'}' missing");
            return b;
        }
        std::vector<Stmt> b;
        b.push_back(parseStmt());
        return b;
    }

    Stmt parseStmt() {
        Stmt s;
        ws();
        if (p_ < s_.size() && s_[p_] == '{') {
            s.kind = Stmt::Block;
            s.body = parseBody();
            return s;
        }
        std::string w = peekWord();
        if (w == "if") {
            acceptWord("if");
            s.kind = Stmt::If;
            s.expr = parseExpr();
            acceptWord("then");
            s.body = parseBody();
            size_t save = p_;
            ws();
            if (p_ < s_.size() && s_[p_] == ';') {  // "...} ; else {...}" erlauben
                ++p_;
                if (peekWord() != "else") p_ = save;
            }
            if (acceptWord("else")) s.elseBody = parseBody();
            return s;
        }
        if (w == "repeat") {
            acceptWord("repeat");
            s.kind = Stmt::Repeat;
            s.expr = parseExpr();
            acceptWord("times");
            s.body = parseBody();
            return s;
        }
        if (w == "wait") {
            acceptWord("wait");
            if (acceptWord("until")) {
                s.kind = Stmt::WaitUntil;
                s.expr = parseExpr();
                if (acceptWord("max")) s.maxExpr = parseExpr();
                acceptWord("s");
                acceptWord("sec");
                return s;
            }
            s.kind = Stmt::Wait;
            ws();
            if (p_ >= s_.size() || s_[p_] == ';' || s_[p_] == '}') {
                Expr one;
                one.num = 1;
                s.expr = std::make_shared<Expr>(one);
            } else {
                s.expr = parseExpr();
            }
            acceptWord("s");
            acceptWord("sec");
            acceptWord("seconds");
            return s;
        }
        if (w == "print" || w == "show") {
            acceptWord(w.c_str());
            s.kind = Stmt::Show;
            for (;;) {
                ws();
                size_t start = p_;
                s.exprs.push_back(parseExpr());
                std::string t = s_.substr(start, p_ - start);
                while (!t.empty() && std::isspace((unsigned char)t.back())) t.pop_back();
                s.texts.push_back(t);
                ws();
                if (p_ < s_.size() && s_[p_] == ',') {
                    ++p_;
                    continue;
                }
                break;
            }
            return s;
        }
        if (w == "set" || w == "let") {
            acceptWord(w.c_str());
            s.kind = Stmt::Set;
            s.text = readIdent();
            if (s.text.empty()) fail("name expected");
            ws();
            if (p_ < s_.size() && s_[p_] == '=') ++p_;
            else fail("'=' expected");
            s.expr = parseExpr();
            return s;
        }
        // normaler Befehl bis ';' oder '}'
        s.kind = Stmt::Command;
        char quote = 0;
        size_t start = p_;
        while (p_ < s_.size()) {
            char c = s_[p_];
            if ((c == '"' || c == '\x27') && (quote == 0 || quote == c)) quote = quote ? 0 : c;
            if (!quote && (c == ';' || c == '}')) break;
            ++p_;
        }
        s.text = s_.substr(start, p_ - start);
        while (!s.text.empty() && std::isspace((unsigned char)s.text.back())) s.text.pop_back();
        if (s.text.empty()) fail("command expected");
        return s;
    }

    // ---------------------------------------------------------------- Ausdrücke
    ExprPtr bin(const std::string& op, ExprPtr a, ExprPtr b) {
        auto e = std::make_shared<Expr>();
        e->kind = Expr::Binary;
        e->op = op;
        e->a = a;
        e->b = b;
        return e;
    }
    ExprPtr parseExpr() { return parseOr(); }
    ExprPtr parseOr() {
        ExprPtr a = parseAnd();
        while (acceptWord("or")) a = bin("or", a, parseAnd());
        return a;
    }
    ExprPtr parseAnd() {
        ExprPtr a = parseNot();
        while (acceptWord("and")) a = bin("and", a, parseNot());
        return a;
    }
    ExprPtr parseNot() {
        if (acceptWord("not")) {
            auto e = std::make_shared<Expr>();
            e->kind = Expr::Unary;
            e->op = "not";
            e->a = parseNot();
            return e;
        }
        return parseCmp();
    }
    ExprPtr parseCmp() {
        ExprPtr a = parseSum();
        ws();
        static const char* ops[] = {"<=", ">=", "==", "!=", "<", ">", "="};
        for (const char* op : ops) {
            size_t n = std::char_traits<char>::length(op);
            if (s_.compare(p_, n, op) == 0) {
                p_ += n;
                return bin(std::string(op) == "=" ? "==" : op, a, parseSum());
            }
        }
        return a;
    }
    ExprPtr parseSum() {
        ExprPtr a = parseProd();
        for (;;) {
            ws();
            if (p_ < s_.size() && (s_[p_] == '+' || s_[p_] == '-')) {
                std::string op(1, s_[p_++]);
                a = bin(op, a, parseProd());
            } else {
                return a;
            }
        }
    }
    ExprPtr parseProd() {
        ExprPtr a = parseUnary();
        for (;;) {
            ws();
            if (p_ < s_.size() && (s_[p_] == '*' || s_[p_] == '/')) {
                std::string op(1, s_[p_++]);
                a = bin(op, a, parseUnary());
            } else {
                return a;
            }
        }
    }
    ExprPtr parseUnary() {
        ws();
        if (p_ < s_.size() && s_[p_] == '-') {
            ++p_;
            auto e = std::make_shared<Expr>();
            e->kind = Expr::Unary;
            e->op = "-";
            e->a = parseUnary();
            return e;
        }
        return parsePrimary();
    }
    ExprPtr parsePrimary() {
        ws();
        auto e = std::make_shared<Expr>();
        if (p_ >= s_.size()) {
            fail("value expected");
            return e;
        }
        char c = s_[p_];
        if (c == '(') {
            ++p_;
            ExprPtr in = parseExpr();
            ws();
            if (p_ < s_.size() && s_[p_] == ')') ++p_;
            else fail("')' missing");
            return in;
        }
        if (c == '"' || c == '\x27') {  // Text in "..." oder '...'
            size_t end = s_.find(c, p_ + 1);
            if (end == std::string::npos) {
                fail("closing quote missing");
                return e;
            }
            e->kind = Expr::Str;
            e->str = s_.substr(p_ + 1, end - p_ - 1);
            p_ = end + 1;
            return e;
        }
        if (std::isdigit((unsigned char)c) || (c == '.' && p_ + 1 < s_.size() && std::isdigit((unsigned char)s_[p_ + 1]))) {
            size_t start = p_;
            while (p_ < s_.size() && std::isdigit((unsigned char)s_[p_])) ++p_;
            if (p_ + 1 < s_.size() && s_[p_] == '.' && identStart((unsigned char)s_[p_ + 1])) {  // 17.y
                e->kind = Expr::Ref;
                e->base = s_.substr(start, p_ - start);
                ++p_;
                e->str = readIdent();
                return e;
            }
            if (p_ < s_.size() && s_[p_] == '.') {
                ++p_;
                while (p_ < s_.size() && std::isdigit((unsigned char)s_[p_])) ++p_;
            }
            e->kind = Expr::Num;
            e->num = std::stod(s_.substr(start, p_ - start));
            return e;
        }
        if (c == '$') ++p_;
        if (p_ < s_.size() && identStart((unsigned char)s_[p_])) {
            std::string name = readIdent();
            bool call = p_ < s_.size() && s_[p_] == '(';  // max(...) ist eine Funktion, kein Schlüsselwort
            for (const char* r : kReserved)
                if (name == r && !call) {
                    fail("value expected instead of '" + name + "'");
                    return e;
                }
            if (p_ + 1 < s_.size() && s_[p_] == '.' && identStart((unsigned char)s_[p_ + 1])) {
                ++p_;
                e->kind = Expr::Ref;
                e->base = name;
                e->str = readIdent();
                return e;
            }
            ws();
            if (p_ < s_.size() && s_[p_] == '(') {  // Funktion, z.B. abstand(3, 7)
                ++p_;
                e->kind = Expr::Call;
                e->str = name;
                ws();
                if (p_ < s_.size() && s_[p_] == ')') {
                    ++p_;
                    return e;
                }
                for (;;) {
                    e->args.push_back(parseExpr());
                    ws();
                    if (p_ < s_.size() && s_[p_] == ',') {
                        ++p_;
                        continue;
                    }
                    if (p_ < s_.size() && s_[p_] == ')') {
                        ++p_;
                        break;
                    }
                    fail("')' missing in function call");
                    break;
                }
                return e;
            }
            if (name == "true" || name == "yes" || name == "on") {
                e->num = 1;
                return e;
            }
            if (name == "false" || name == "no" || name == "off") {
                e->num = 0;
                return e;
            }
            e->kind = Expr::Var;
            e->str = name;
            return e;
        }
        fail("value expected");
        return e;
    }
};

// $name durch den Wert ersetzen, $(ausdruck) ausrechnen und einsetzen
std::string substitute(const std::string& text, CommandHost& host, std::string& err) {
    std::string r;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '$' && i + 1 < text.size() && text[i + 1] == '(') {
            int depth = 0;
            size_t e = i + 1;
            for (; e < text.size(); ++e) {
                if (text[e] == '(') ++depth;
                if (text[e] == ')' && --depth == 0) break;
            }
            if (e >= text.size()) {
                err = "')' missing after $(";
                return r;
            }
            const std::string inner = text.substr(i + 2, e - i - 2);  // Parser hält nur eine Referenz
            Parser p(inner);
            ExprPtr ex = p.parseWhole();
            if (!p.error.empty()) {
                err = p.error;
                return r;
            }
            Value v;
            if (!eval(*ex, host, v, err)) return r;
            r += fmtValue(v);
            i = e;
            continue;
        }
        if (text[i] == '$' && i + 1 < text.size()) {
            size_t e = i + 1;
            while (e < text.size() && (std::isalnum((unsigned char)text[e]) || text[e] == '_' || (unsigned char)text[e] >= 0x80)) ++e;
            std::string name = normalizeWord(text.substr(i + 1, e - i - 1));
            auto it = vars().find(name);
            if (!name.empty() && it != vars().end()) {
                r += fmtValue(it->second);
                i = e - 1;
                continue;
            }
        }
        r += text[i];
    }
    return r;
}

} // namespace

struct ScriptProgram {
    std::vector<Stmt> stmts;
    bool compound = false;  // mehr als ein einfacher Befehl
};

std::unique_ptr<ScriptJob> parseScript(const std::string& text, std::string* err) {
    std::string src = text;
    if (src.rfind("\xEF\xBB\xBF", 0) == 0) src.erase(0, 3);
    Parser p(src);
    auto prog = std::make_shared<ScriptProgram>();
    prog->stmts = p.parseBlock(false);
    if (!p.error.empty()) {
        if (err) *err = p.error;
        return nullptr;
    }
    prog->compound = prog->stmts.size() != 1 || prog->stmts[0].kind != Stmt::Command;
    auto job = std::make_unique<ScriptJob>();
    job->program = prog;
    job->stack.push_back({&prog->stmts, 0, 1});
    return job;
}

bool stepScript(ScriptJob& job, CommandHost& host, double now, const std::function<void(const std::string&, bool)>& onOutput) {
    if (job.finished) return true;
    auto emit = [&](const std::string& text, bool ok) {
        if (text.empty()) return;
        job.output += text;
        if (job.output.back() != '\n') job.output += '\n';
        if (!ok) job.ok = false;
        if (onOutput) onOutput(text, ok);
    };
    auto abort = [&](const std::string& msg) {
        emit("Error: " + msg, false);
        job.finished = true;
        job.stack.clear();
        return true;
    };
    int budget = 5000;
    while (!job.stack.empty()) {
        if (--budget <= 0) return false;  // sehr lange Schleifen auf mehrere Bilder verteilen
        ScriptFrame& f = job.stack.back();
        const auto& list = *static_cast<const std::vector<Stmt>*>(f.list);
        if (f.index >= list.size()) {
            if (f.repeatLeft > 1) {
                --f.repeatLeft;
                f.index = 0;
            } else {
                job.stack.pop_back();
            }
            continue;
        }
        const Stmt& s = list[f.index];
        std::string err;
        Value v;
        switch (s.kind) {
        case Stmt::Command: {
            ++f.index;
            std::string text = substitute(s.text, host, err);
            if (!err.empty()) return abort(err);
            if (job.program->compound) emit("> " + text, true);
            CommandResult r = runCommand(host, text);
            emit(r.text, r.ok);
            break;
        }
        case Stmt::Wait: {
            if (!job.waiting) {
                if (!eval(*s.expr, host, v, err)) return abort(err);
                job.waiting = true;
                job.waitingUntil = false;
                job.waitStart = now;
                job.wakeAt = now + std::clamp(v.num, 0.0, 3600.0);
            }
            if (now < job.wakeAt) return false;
            job.waiting = false;
            ++f.index;
            break;
        }
        case Stmt::WaitUntil: {
            if (!job.waiting) {
                double maxS = 60.0;
                if (s.maxExpr) {
                    if (!eval(*s.maxExpr, host, v, err)) return abort(err);
                    maxS = std::clamp(v.num, 0.0, 3600.0);
                }
                job.waiting = true;
                job.waitingUntil = true;
                job.waitStart = now;
                job.wakeAt = now + maxS;
            }
            if (!eval(*s.expr, host, v, err)) return abort(err);
            if (truthy(v)) {
                job.waiting = false;
                ++f.index;
                char buf[96];
                std::snprintf(buf, sizeof(buf), "wait until: true after %.2f s", now - job.waitStart);
                emit(buf, true);
                break;
            }
            if (now >= job.wakeAt) {
                job.waiting = false;
                ++f.index;
                char buf[96];
                std::snprintf(buf, sizeof(buf), "wait until: time limit %.1f s reached, condition not met", job.wakeAt - job.waitStart);
                emit(buf, true);
                break;
            }
            return false;
        }
        case Stmt::If: {
            ++f.index;
            if (!eval(*s.expr, host, v, err)) return abort(err);
            const std::vector<Stmt>& branch = truthy(v) ? s.body : s.elseBody;
            if (!branch.empty()) job.stack.push_back({&branch, 0, 1});
            break;
        }
        case Stmt::Repeat: {
            ++f.index;
            if (!eval(*s.expr, host, v, err)) return abort(err);
            long long n = std::llround(v.num);
            if (n > 1000000) return abort("at most 1000000 repetitions");
            if (n > 0 && !s.body.empty()) job.stack.push_back({&s.body, 0, n});
            break;
        }
        case Stmt::Show: {
            ++f.index;
            std::string line;
            for (size_t i = 0; i < s.exprs.size(); ++i) {
                if (!eval(*s.exprs[i], host, v, err)) return abort(err);
                if (!line.empty()) line += "   ";
                if (s.exprs[i]->kind == Expr::Str) line += v.str;
                else line += s.texts[i] + " = " + fmtValue(v);
            }
            emit(line, true);
            break;
        }
        case Stmt::Set: {
            ++f.index;
            if (!eval(*s.expr, host, v, err)) return abort(err);
            vars()[s.text] = v;
            break;
        }
        case Stmt::Block: {
            ++f.index;
            if (!s.body.empty()) job.stack.push_back({&s.body, 0, 1});
            break;
        }
        }
    }
    job.finished = true;
    return true;
}

std::string runScriptSync(CommandHost& host, const std::string& text, bool* ok) {
    std::string err;
    auto job = parseScript(text, &err);
    if (!job) {
        if (ok) *ok = false;
        return "Error: " + err;
    }
    World& w = host.world();
    for (int guard = 0; guard < 10000000 && !stepScript(*job, host, w.simTime()); ++guard) {
        double dt = 1.0 / 60.0;
        if (job->waiting && !job->waitingUntil) dt = std::max(job->wakeAt - w.simTime(), 1e-4);
        else if (!job->waiting) continue;  // nur Rechenbudget aufgebraucht
        w.simulateSeconds(dt, false);  // ruhende Teile dürfen weiterschlafen
    }
    for (const std::string& e : w.takeEvents()) job->output += "!! " + e + "\n";  // Brüche usw.
    if (ok) *ok = job->ok;
    return job->output;
}

std::string scriptHelp() {
    return "SCRIPT LANGUAGE (everything can be on one line):\n"
           "  command1 ; command2 ; ...                run one after the other\n"
           "  wait 2                                   wait 2 seconds (the game keeps running)\n"
           "  wait until <condition> [max 10]          wait until something happens\n"
           "  if <condition> then { ... } else { ... }\n"
           "  repeat 5 times { ... }\n"
           "  print 17.y, 17.v, \"text\"                 print values\n"
           "  set h = 17.y   ->  later: print h  or in a command: position 3 0 $h 0   or $(h + 5)\n"
           "Conditions/math: < > <= >= == != and or not + - * / ( )   true false\n"
           "Functions: abs sqrt round sin cos tan (degrees) min max distance(a, b)\n"
           "Values: <id>.x .y .z (cm) .v .vx .vy .vz (m/s) .kmh .rpm .mass .weight .volume .bottom .top\n"
           "        .length .height .width .rx .ry .rz .resting .still .moving .fixed .energy .overlap (mm)\n"
           "        .pieces .hulls .edges .airforce .airx .airy .airz .airtorque .joints .load .name .material .exists\n"
           "        .bores .pins .boreload (%) .boreforce (N) .pressure (MPa) .wear (mm) .worn (mm3) .lubricant\n"
           "        .temp .tempmax .tempmin (°C) .burning (spots) .charred .molten (%)\n"
           "        instead of <id>: selected, last (last created) or a variable set with 'set'\n"
           "        player.x .y .z .onground .holding .lookingat   camera.x .y .z .distance\n"
           "        mode time parts joints broken rescued fps ambient fires\n";
}
