#include "potion/json.hpp"
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace potion {
namespace {
const Json null_json;
void append_utf8(std::string &out, unsigned code) {
  if (code <= 0x7f) out.push_back(static_cast<char>(code));
  else if (code <= 0x7ff) {
    out.push_back(static_cast<char>(0xc0 | code >> 6));
    out.push_back(static_cast<char>(0x80 | (code & 63)));
  } else if (code <= 0xffff) {
    out.push_back(static_cast<char>(0xe0 | code >> 12));
    out.push_back(static_cast<char>(0x80 | ((code >> 6) & 63)));
    out.push_back(static_cast<char>(0x80 | (code & 63)));
  } else {
    out.push_back(static_cast<char>(0xf0 | code >> 18));
    out.push_back(static_cast<char>(0x80 | ((code >> 12) & 63)));
    out.push_back(static_cast<char>(0x80 | ((code >> 6) & 63)));
    out.push_back(static_cast<char>(0x80 | (code & 63)));
  }
}
class Parser {
public:
  explicit Parser(const std::string &text) : text_(text) {}
  Json parse() { Json value = parse_value(); ws(); if (at_ != text_.size()) fail(); return value; }
private:
  void ws() { while (at_ < text_.size() && (text_[at_] == ' ' || text_[at_] == '\n' || text_[at_] == '\r' || text_[at_] == '\t')) ++at_; }
  [[noreturn]] void fail() const { throw std::runtime_error("invalid JSON at byte " + std::to_string(at_)); }
  bool take(char c) { ws(); if (at_ < text_.size() && text_[at_] == c) { ++at_; return true; } return false; }
  unsigned parse_hex4() {
    if (at_ + 4 > text_.size()) fail();
    unsigned code = 0;
    for (int i = 0; i < 4; ++i) {
      const char h = text_[at_++];
      code <<= 4;
      if (h >= '0' && h <= '9') code += h - '0';
      else if (h >= 'a' && h <= 'f') code += h - 'a' + 10;
      else if (h >= 'A' && h <= 'F') code += h - 'A' + 10;
      else fail();
    }
    return code;
  }
  Json parse_value() {
    ws(); if (at_ >= text_.size()) fail();
    if (text_[at_] == '"') return Json(parse_string());
    if (text_[at_] == '{') return parse_object();
    if (text_[at_] == '[') return parse_array();
    if (text_.compare(at_, 4, "true") == 0) { at_ += 4; return Json(true); }
    if (text_.compare(at_, 5, "false") == 0) { at_ += 5; return Json(false); }
    if (text_.compare(at_, 4, "null") == 0) { at_ += 4; return Json(); }
    char *end = nullptr; const char *start = text_.c_str() + at_;
    const double number = std::strtod(start, &end);
    if (end == start) fail();
    at_ += static_cast<std::size_t>(end - start);
    return Json(number);
  }
  std::string parse_string() {
    if (!take('"')) fail();
    std::string out;
    while (at_ < text_.size()) {
      char c = text_[at_++]; if (c == '"') return out;
      if (static_cast<unsigned char>(c) < 0x20) fail();
      if (c != '\\') { out.push_back(c); continue; }
      if (at_ >= text_.size()) fail();
      c = text_[at_++];
      if (c == '"' || c == '\\' || c == '/') out.push_back(c);
      else if (c == 'b') out.push_back('\b'); else if (c == 'f') out.push_back('\f');
      else if (c == 'n') out.push_back('\n'); else if (c == 'r') out.push_back('\r');
      else if (c == 't') out.push_back('\t');
      else if (c == 'u') {
        unsigned code = parse_hex4();
        if (code >= 0xd800 && code <= 0xdbff) {
          if (at_ + 2 > text_.size() || text_[at_] != '\\' ||
              text_[at_ + 1] != 'u')
            fail();
          at_ += 2;
          const unsigned low = parse_hex4();
          if (low < 0xdc00 || low > 0xdfff) fail();
          code = 0x10000 + ((code - 0xd800) << 10) + (low - 0xdc00);
        }
        else if (code >= 0xdc00 && code <= 0xdfff) fail();
        append_utf8(out, code);
      } else fail();
    }
    fail();
  }
  Json parse_array() {
    take('['); Json out = Json::array(); if (take(']')) return out;
    do { out.items().push_back(parse_value()); } while (take(','));
    if (!take(']')) fail();
    return out;
  }
  Json parse_object() {
    take('{'); Json out = Json::object(); if (take('}')) return out;
    do { ws(); if (at_ >= text_.size() || text_[at_] != '"') fail();
      std::string key = parse_string(); if (!take(':')) fail(); out[key] = parse_value();
    } while (take(','));
    if (!take('}')) fail();
    return out;
  }
  const std::string &text_; std::size_t at_{};
};
}

Json Json::array() { Json j; j.type_ = Type::Array; return j; }
Json Json::object() { Json j; j.type_ = Type::Object; return j; }
Json Json::parse(const std::string &text) { return Parser(text).parse(); }
bool Json::boolean(bool fallback) const { return type_ == Type::Bool ? boolean_ : fallback; }
double Json::number(double fallback) const { return type_ == Type::Number ? number_ : fallback; }
const std::string &Json::string() const { static const std::string empty; return type_ == Type::String ? string_ : empty; }
const std::vector<Json> &Json::items() const { static const std::vector<Json> empty; return type_ == Type::Array ? array_ : empty; }
std::vector<Json> &Json::items() { if (type_ != Type::Array) { type_ = Type::Array; array_.clear(); } return array_; }
const std::map<std::string, Json> &Json::members() const { static const std::map<std::string, Json> empty; return type_ == Type::Object ? object_ : empty; }
std::map<std::string, Json> &Json::members() { if (type_ != Type::Object) { type_ = Type::Object; object_.clear(); } return object_; }
const Json &Json::get(const std::string &key) const { if (type_ != Type::Object) return null_json; auto it = object_.find(key); return it == object_.end() ? null_json : it->second; }
Json &Json::operator[](const std::string &key) { if (type_ != Type::Object) { type_ = Type::Object; object_.clear(); } return object_[key]; }
std::string json_escape(const std::string &value) {
  std::ostringstream out; out << '"';
  for (unsigned char c : value) {
    switch (c) { case '"': out << "\\\""; break; case '\\': out << "\\\\"; break;
      case '\b': out << "\\b"; break; case '\f': out << "\\f"; break;
      case '\n': out << "\\n"; break; case '\r': out << "\\r"; break; case '\t': out << "\\t"; break;
      default: if (c < 0x20) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c) << std::dec; else out << static_cast<char>(c); }
  }
  out << '"'; return out.str();
}
std::string Json::dump() const {
  if (type_ == Type::Null) return "null";
  if (type_ == Type::Bool) return boolean_ ? "true" : "false";
  if (type_ == Type::Number) { std::ostringstream out; out << std::setprecision(15) << number_; return out.str(); }
  if (type_ == Type::String) return json_escape(string_);
  std::string out = type_ == Type::Array ? "[" : "{"; bool first = true;
  if (type_ == Type::Array) for (const auto &v : array_) { if (!first) out += ','; first = false; out += v.dump(); }
  else for (const auto &v : object_) { if (!first) out += ','; first = false; out += json_escape(v.first) + ':' + v.second.dump(); }
  return out + (type_ == Type::Array ? "]" : "}");
}
}
