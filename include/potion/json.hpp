#pragma once
#include <map>
#include <string>
#include <vector>

namespace potion {
class Json {
public:
  enum class Type { Null, Bool, Number, String, Array, Object };
  Json() = default;
  explicit Json(bool value) : type_(Type::Bool), boolean_(value) {}
  explicit Json(double value) : type_(Type::Number), number_(value) {}
  explicit Json(std::string value) : type_(Type::String), string_(std::move(value)) {}
  static Json array();
  static Json object();
  static Json parse(const std::string &text);
  [[nodiscard]] std::string dump() const;
  [[nodiscard]] Type type() const { return type_; }
  [[nodiscard]] bool is_null() const { return type_ == Type::Null; }
  [[nodiscard]] bool boolean(bool fallback = false) const;
  [[nodiscard]] double number(double fallback = 0) const;
  [[nodiscard]] const std::string &string() const;
  [[nodiscard]] const std::vector<Json> &items() const;
  [[nodiscard]] std::vector<Json> &items();
  [[nodiscard]] const std::map<std::string, Json> &members() const;
  [[nodiscard]] std::map<std::string, Json> &members();
  [[nodiscard]] const Json &get(const std::string &key) const;
  Json &operator[](const std::string &key);
private:
  Type type_{Type::Null};
  bool boolean_{};
  double number_{};
  std::string string_;
  std::vector<Json> array_;
  std::map<std::string, Json> object_;
};
std::string json_escape(const std::string &value);
}

