#pragma once
#include "common.hpp"

struct JsonValue {
    enum Type { STRING, NUMBER, OBJECT, ARRAY, BOOL, NIL } type = NIL;
    std::string str;
    long long number = 0;
    std::map<std::string, JsonValue> object;
    std::vector<JsonValue> array;
};
class MiniJson {
    std::string s;
    size_t p = 0;
    void ws() { while (p < s.size() && std::isspace(static_cast<unsigned char>(s[p]))) ++p; }
    bool consume(char c) { ws(); if (p < s.size() && s[p] == c) { ++p; return true; } return false; }
    std::string parse_string() {
        ws();
        if (p >= s.size() || s[p] != '"') throw std::runtime_error("expected string");
        ++p; std::string out;
        while (p < s.size()) {
            char c = s[p++];
            if (c == '"') return out;
            if (c == '\\') {
                if (p >= s.size()) throw std::runtime_error("bad string escape");
                char e = s[p++];
                if (e == '"' || e == '\\' || e == '/') out.push_back(e);
                else if (e == 'n') out.push_back('\n');
                else if (e == 'r') out.push_back('\r');
                else if (e == 't') out.push_back('\t');
                else throw std::runtime_error("unsupported string escape");
            } else out.push_back(c);
        }
        throw std::runtime_error("unterminated string");
    }
    JsonValue value() {
        ws();
        if (p >= s.size()) throw std::runtime_error("unexpected end of json");
        if (s[p] == '"') { JsonValue v; v.type = JsonValue::STRING; v.str = parse_string(); return v; }
        if (s[p] == '{') return object();
        if (s[p] == '[') return array();
        if (s.compare(p, 4, "true") == 0) { p += 4; JsonValue v; v.type = JsonValue::BOOL; return v; }
        if (s.compare(p, 5, "false") == 0) { p += 5; JsonValue v; v.type = JsonValue::BOOL; return v; }
        if (s.compare(p, 4, "null") == 0) { p += 4; JsonValue v; v.type = JsonValue::NIL; return v; }
        size_t start = p;
        if (s[p] == '-') ++p;
        while (p < s.size() && std::isdigit(static_cast<unsigned char>(s[p]))) ++p;
        if (start == p) throw std::runtime_error("unexpected token");
        JsonValue v; v.type = JsonValue::NUMBER; v.number = std::stoll(s.substr(start, p - start)); return v;
    }
    JsonValue object() {
        JsonValue v; v.type = JsonValue::OBJECT; consume('{'); ws();
        if (consume('}')) return v;
        while (true) {
            std::string key = parse_string();
            if (!consume(':')) throw std::runtime_error("expected ':'");
            v.object[key] = value();
            if (consume('}')) break;
            if (!consume(',')) throw std::runtime_error("expected ','");
        }
        return v;
    }
    JsonValue array() {
        JsonValue v; v.type = JsonValue::ARRAY; consume('['); ws();
        if (consume(']')) return v;
        while (true) {
            v.array.push_back(value());
            if (consume(']')) break;
            if (!consume(',')) throw std::runtime_error("expected ','");
        }
        return v;
    }
public:
    explicit MiniJson(std::string text) : s(std::move(text)) {}
    JsonValue parse() { JsonValue v = value(); ws(); if (p != s.size()) throw std::runtime_error("trailing data"); return v; }
};

static inline const JsonValue* required(const JsonValue& obj, const std::string& key, const std::string& path) {
    if (obj.type != JsonValue::OBJECT) throw std::runtime_error("error: '" + path + "' must be an object");
    auto it = obj.object.find(key);
    if (it == obj.object.end()) throw std::runtime_error("error: missing required field '" + path + "'");
    return &it->second;
}
static inline Config load_config(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("error: cannot open config file '" + path + "'");
    std::stringstream ss; ss << in.rdbuf();
    JsonValue root;
    try { root = MiniJson(ss.str()).parse(); }
    catch (const std::exception& e) { throw std::runtime_error(std::string("error: malformed JSON: ") + e.what()); }
    const JsonValue& server = *required(root, "server", "server");
    if (server.type != JsonValue::OBJECT) throw std::runtime_error("error: 'server' must be an object");
    const JsonValue& ip = *required(server, "ip", "server.ip");
    const JsonValue& port = *required(server, "port", "server.port");
    const JsonValue& st = *required(server, "server_threads", "server.server_threads");
    const JsonValue& ct = *required(server, "client_threads", "server.client_threads");
    if (ip.type != JsonValue::STRING) throw std::runtime_error("error: 'server.ip' must be a string");
    if (port.type != JsonValue::NUMBER) throw std::runtime_error("error: 'server.port' must be a number");
    if (st.type != JsonValue::NUMBER) throw std::runtime_error("error: 'server.server_threads' must be a number");
    if (ct.type != JsonValue::NUMBER) throw std::runtime_error("error: 'server.client_threads' must be a number");
    if (port.number < 1 || port.number > 65535) throw std::runtime_error("error: 'server.port' out of range");
    if (st.number < 1) throw std::runtime_error("error: 'server.server_threads' must be positive");
    if (ct.number < 1) throw std::runtime_error("error: 'server.client_threads' must be positive");
    return {ip.str, static_cast<int>(port.number), static_cast<int>(st.number), static_cast<int>(ct.number)};
}
