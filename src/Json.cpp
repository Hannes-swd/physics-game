#include "Json.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

class Parser {
public:
    Parser(const char* begin, const char* end) : begin_(begin), p_(begin), end_(end) {}
    bool parse(Json& out, std::string* err) {
        if (end_ - p_ >= 3 && std::strncmp(p_, "\xEF\xBB\xBF", 3) == 0) p_ += 3;  // BOM (Windows-Editor)
        bool ok = value(out, 0);
        if (ok) {
            space();
            if (p_ < end_) ok = fail("unexpected text after the end");
        }
        if (!ok && err) {
            int line = 1, col = 1;
            for (const char* q = begin_; q < p_ && q < end_; ++q) {
                if (*q == '\n') ++line, col = 1;
                else if (((unsigned char)*q & 0xC0) != 0x80) ++col;
            }
            *err = "line " + std::to_string(line) + ", column " + std::to_string(col) + ": " + (msg_.empty() ? "invalid JSON" : msg_);
        }
        return ok;
    }

private:
    const char* begin_;
    const char* p_;
    const char* end_;
    std::string msg_;

    bool fail(const char* m) {
        if (msg_.empty()) msg_ = m;
        return false;
    }
    void space() {
        while (p_ < end_ && (*p_ == ' ' || *p_ == '\t' || *p_ == '\n' || *p_ == '\r')) ++p_;
    }
    bool literal(const char* word) {
        size_t n = std::strlen(word);
        if ((size_t)(end_ - p_) < n || std::strncmp(p_, word, n) != 0) return false;
        p_ += n;
        return true;
    }
    bool string(std::string& s) {
        if (p_ >= end_ || *p_ != '"') return fail("'\"' expected");
        ++p_;
        while (p_ < end_ && *p_ != '"') {
            char c = *p_++;
            if (c == '\n') return fail("line break inside a text (missing '\"'?)");
            if (c != '\\') {
                s += c;
                continue;
            }
            if (p_ >= end_) return fail("text not closed");
            c = *p_++;
            switch (c) {
            case 'n': s += '\n'; break;
            case 't': s += '\t'; break;
            case 'r': s += '\r'; break;
            case 'b': s += '\b'; break;
            case 'f': s += '\f'; break;
            case 'u': {
                if (end_ - p_ < 4) return fail("bad \\u escape");
                unsigned cp = (unsigned)std::strtoul(std::string(p_, p_ + 4).c_str(), nullptr, 16);
                p_ += 4;
                if (cp < 0x80) {
                    s += (char)cp;
                } else if (cp < 0x800) {
                    s += (char)(0xC0 | (cp >> 6));
                    s += (char)(0x80 | (cp & 0x3F));
                } else {
                    s += (char)(0xE0 | (cp >> 12));
                    s += (char)(0x80 | ((cp >> 6) & 0x3F));
                    s += (char)(0x80 | (cp & 0x3F));
                }
                break;
            }
            default: s += c;  // \" \\ \/
            }
        }
        if (p_ >= end_) return fail("text not closed (missing '\"')");
        ++p_;
        return true;
    }
    bool value(Json& v, int depth) {
        if (depth > 64) return fail("nested too deeply");
        space();
        if (p_ >= end_) return fail("value expected, file ends");
        const char c = *p_;
        if (c == '{') {
            ++p_;
            v.type = Json::Object;
            space();
            if (p_ < end_ && *p_ == '}') return ++p_, true;
            while (true) {
                space();
                std::string key;
                if (!string(key)) return fail("key in \"quotes\" expected (or a ',' too many before '}')");
                space();
                if (p_ >= end_ || *p_ != ':') return fail("':' expected after the key");
                ++p_;
                v.obj.emplace_back(std::move(key), Json());
                if (!value(v.obj.back().second, depth + 1)) return false;
                space();
                if (p_ < end_ && *p_ == ',') {
                    ++p_;
                    continue;
                }
                if (p_ < end_ && *p_ == '}') return ++p_, true;
                return fail("',' or '}' expected");
            }
        }
        if (c == '[') {
            ++p_;
            v.type = Json::Array;
            space();
            if (p_ < end_ && *p_ == ']') return ++p_, true;
            while (true) {
                v.arr.emplace_back();
                if (!value(v.arr.back(), depth + 1)) return false;
                space();
                if (p_ < end_ && *p_ == ',') {
                    ++p_;
                    continue;
                }
                if (p_ < end_ && *p_ == ']') return ++p_, true;
                return fail("',' or ']' expected");
            }
        }
        if (c == '"') {
            v.type = Json::String;
            return string(v.str);
        }
        if (literal("true")) return v.type = Json::Bool, v.num = 1, true;
        if (literal("false")) return v.type = Json::Bool, true;
        if (literal("null")) return true;
        char* stop = nullptr;
        std::string tmp(p_, std::min<size_t>(end_ - p_, 64));
        v.num = std::strtod(tmp.c_str(), &stop);
        if (stop == tmp.c_str()) return fail("value expected (number, \"text\", [list], {object}, true, false, null)");
        v.type = Json::Number;
        p_ += stop - tmp.c_str();
        return true;
    }
};

std::string number(double v) {
    char b[40];
    if (std::isfinite(v) && std::abs(v) < 1e15 && v == std::floor(v)) std::snprintf(b, sizeof(b), "%.0f", v);
    else if (std::isfinite(v)) std::snprintf(b, sizeof(b), "%.6g", v);
    else std::snprintf(b, sizeof(b), "null");
    return b;
}

std::string quoted(const std::string& s) {
    std::string o = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') o += '\\', o += c;
        else if (c == '\n') o += "\\n";
        else if (c == '\t') o += "\\t";
        else if ((unsigned char)c < 0x20) {
            char b[8];
            std::snprintf(b, sizeof(b), "\\u%04x", (unsigned)c);
            o += b;
        } else o += c;
    }
    return o + "\"";
}

bool flat(const Json& j) {  // Liste ohne Objekte/Listen darin: in einer Zeile
    if (j.type != Json::Array) return false;
    for (const Json& e : j.arr)
        if (e.type == Json::Array || e.type == Json::Object) return false;
    return true;
}

void write(const Json& j, int indent, int level, std::string& out) {
    const std::string pad((size_t)indent * (level + 1), ' '), padEnd((size_t)indent * level, ' ');
    switch (j.type) {
    case Json::Null: out += "null"; break;
    case Json::Bool: out += j.num != 0 ? "true" : "false"; break;
    case Json::Number: out += number(j.num); break;
    case Json::String: out += quoted(j.str); break;
    case Json::Array:
        if (j.arr.empty()) {
            out += "[]";
        } else if (flat(j) && [&] {  // kurze Liste aus Zahlen/Texten: in einer Zeile
                       std::string line;
                       for (const Json& e : j.arr) write(e, indent, level + 1, line);
                       return line.size() + 2 * j.arr.size() <= 100;
                   }()) {
            out += "[";
            for (size_t i = 0; i < j.arr.size(); ++i) {
                if (i) out += ", ";
                write(j.arr[i], indent, level + 1, out);
            }
            out += "]";
        } else {
            out += "[\n";
            for (size_t i = 0; i < j.arr.size(); ++i) {
                out += pad;
                write(j.arr[i], indent, level + 1, out);
                out += i + 1 < j.arr.size() ? ",\n" : "\n";
            }
            out += padEnd + "]";
        }
        break;
    case Json::Object: {
        if (j.obj.empty()) {
            out += "{}";
            break;
        }
        // kleines Objekt nur aus einfachen Werten: in einer Zeile ({"a": "steel", "b": "steel", "friction": [0.74, 0.57]})
        if (level > 0) {
            bool simple = true;
            for (const auto& kv : j.obj) simple &= kv.second.type != Json::Object && (kv.second.type != Json::Array || flat(kv.second));
            if (simple) {
                std::string line = "{";
                for (size_t i = 0; i < j.obj.size(); ++i) {
                    line += (i ? ", " : "") + quoted(j.obj[i].first) + ": ";
                    write(j.obj[i].second, indent, level + 1, line);
                }
                line += "}";
                if (line.size() <= 90) {
                    out += line;
                    break;
                }
            }
        }
        size_t w = 0;  // Werte untereinander ausrichten
        for (const auto& kv : j.obj) w = std::max(w, kv.first.size());
        out += "{\n";
        for (size_t i = 0; i < j.obj.size(); ++i) {
            const auto& [k, v] = j.obj[i];
            std::string val;  // Werte untereinander, nur wenn sie in eine Zeile passen
            write(v, indent, level + 1, val);
            const bool oneLine = val.find('\n') == std::string::npos;
            out += pad + quoted(k) + ":" + (oneLine ? std::string(w - k.size() + 1, ' ') : std::string(" ")) + val;
            out += i + 1 < j.obj.size() ? ",\n" : "\n";
        }
        out += padEnd + "}";
        break;
    }
    }
}

} // namespace

bool parseJson(const char* begin, const char* end, Json& out, std::string* err) {
    out = Json();
    return Parser(begin, end).parse(out, err);
}

bool parseJson(const std::string& text, Json& out, std::string* err) { return parseJson(text.data(), text.data() + text.size(), out, err); }

std::string writeJson(const Json& j, int indent) {
    std::string out;
    write(j, indent, 0, out);
    return out + "\n";
}
