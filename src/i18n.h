#pragma once
#include <string>
#include <unordered_map>
#include <fstream>

// ---------------------------------------------------------------------------
// Minimal GNU .po parser + runtime string lookup.
//
// Usage:
//   _("Some string")          — returns translation or the original key
//   i18n::set_language(...)   — load a .po file (or clear to English)
// ---------------------------------------------------------------------------

enum class Language { English, Catalan };

namespace i18n {

inline std::unordered_map<std::string, std::string> g_strings;
inline Language g_language = Language::English;

namespace detail {

inline std::string unescape(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            switch (s[++i]) {
                case 'n':  out += '\n'; break;
                case 't':  out += '\t'; break;
                case '"':  out += '"';  break;
                case '\\': out += '\\'; break;
                default:   out += '\\'; out += s[i]; break;
            }
        } else {
            out += s[i];
        }
    }
    return out;
}

// Extract content between the outermost pair of quotes on a line.
inline std::string extract_quoted(const std::string& line)
{
    size_t q1 = line.find('"');
    if (q1 == std::string::npos) return "";
    size_t q2 = line.rfind('"');
    if (q2 == q1) return "";
    return unescape(line.substr(q1 + 1, q2 - q1 - 1));
}

} // namespace detail

// Parse a .po file and populate g_strings with msgid -> msgstr pairs.
inline void load(const std::string& path)
{
    g_strings.clear();
    std::ifstream f(path);
    if (!f) return;

    enum State { NONE, IN_MSGID, IN_MSGSTR };
    State       state = NONE;
    std::string msgid, msgstr, line;

    auto commit = [&]() {
        if (!msgid.empty() && !msgstr.empty())
            g_strings[msgid] = msgstr;
        msgid.clear();
        msgstr.clear();
        state = NONE;
    };

    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;

        if (line.rfind("msgid ",   0) == 0) { commit(); state = IN_MSGID;  msgid  = detail::extract_quoted(line.substr(6)); }
        else if (line.rfind("msgstr ", 0) == 0) {        state = IN_MSGSTR; msgstr = detail::extract_quoted(line.substr(7)); }
        else if (line[0] == '"') {
            std::string cont = detail::extract_quoted(line);
            if      (state == IN_MSGID)   msgid  += cont;
            else if (state == IN_MSGSTR)  msgstr += cont;
        }
    }
    commit();
    g_strings.erase(""); // remove .po metadata entry (msgid "")
}

inline void set_language(Language lang, const char* i18n_dir)
{
    g_language = lang;
    if (lang == Language::English) { g_strings.clear(); return; }
    std::string path(i18n_dir);
    if (lang == Language::Catalan) path += "cat.po";
    load(path);
}

inline const char* tr(const char* key)
{
    if (g_language == Language::English) return key;
    auto it = g_strings.find(key);
    return it != g_strings.end() ? it->second.c_str() : key;
}

} // namespace i18n

// The conventional underscore function used throughout the UI.
inline const char* _(const char* key) { return i18n::tr(key); }
