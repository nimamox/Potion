#include "potion/markdown.hpp"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>
#include <vector>

namespace potion {
namespace {
std::string html_escape(const std::string &text) {
  std::string out; out.reserve(text.size());
  for (char c : text) { if (c == '&') out += "&amp;"; else if (c == '<') out += "&lt;"; else if (c == '>') out += "&gt;"; else if (c == '"') out += "&quot;"; else if (c == '\'') out += "&#39;"; else out += c; }
  return out;
}
std::string trim(std::string s) { auto a = s.find_first_not_of(" \t\r"); if (a == std::string::npos) return {}; auto b = s.find_last_not_of(" \t\r"); return s.substr(a, b - a + 1); }
struct NotionAttributes { std::size_t start{std::string::npos}; std::string text; bool toggle{}; };
bool known_notion_attribute(const std::string &name) { return name == "color" || name == "toggle" || name == "underline"; }
NotionAttributes trailing_notion_attributes(const std::string &source) {
  NotionAttributes result; const auto at = source.rfind(" {");
  if (at == std::string::npos || source.empty() || source.back() != '}') return result;
  const std::string attributes = source.substr(at + 2, source.size() - at - 3); std::size_t pos = 0; bool found = false;
  while (pos < attributes.size()) {
    while (pos < attributes.size() && attributes[pos] == ' ') ++pos;
    const auto name_start = pos;
    while (pos < attributes.size() && (std::isalnum(static_cast<unsigned char>(attributes[pos])) || attributes[pos] == '_' || attributes[pos] == '-')) ++pos;
    if (name_start == pos || pos + 2 > attributes.size() || attributes[pos] != '=' || attributes[pos + 1] != '"') return {};
    const std::string name = attributes.substr(name_start, pos - name_start); if (!known_notion_attribute(name)) return {};
    pos += 2; const auto value_start = pos; const auto value_end = attributes.find('"', pos); if (value_end == std::string::npos) return {};
    if (name == "toggle" && attributes.substr(value_start, value_end - value_start) == "true") result.toggle = true;
    pos = value_end + 1; found = true;
  }
  if (!found) return {};
  result.start = at; result.text = attributes; return result;
}
std::string strip_attrs(std::string s) { const auto attributes = trailing_notion_attributes(s); if (attributes.start != std::string::npos) s.resize(attributes.start); return s; }
std::string attribute(const std::string &line, const std::string &name) { std::string needle = name + "=\""; auto a = line.find(needle); if (a == std::string::npos) return {}; a += needle.size(); auto b = line.find('"', a); return b == std::string::npos ? std::string{} : line.substr(a, b - a); }
bool starts(const std::string &s, const std::string &prefix) { return s.compare(0, prefix.size(), prefix) == 0; }
bool contains_rtl_text(const std::string &text) {
  for (std::size_t i = 0; i < text.size();) {
    const unsigned char first = static_cast<unsigned char>(text[i]);
    unsigned codepoint = first;
    std::size_t length = 1;
    if ((first & 0xe0) == 0xc0 && i + 1 < text.size()) {
      codepoint = ((first & 0x1f) << 6) |
                  (static_cast<unsigned char>(text[i + 1]) & 0x3f);
      length = 2;
    } else if ((first & 0xf0) == 0xe0 && i + 2 < text.size()) {
      codepoint = ((first & 0x0f) << 12) |
                  ((static_cast<unsigned char>(text[i + 1]) & 0x3f) << 6) |
                  (static_cast<unsigned char>(text[i + 2]) & 0x3f);
      length = 3;
    } else if ((first & 0xf8) == 0xf0 && i + 3 < text.size()) {
      codepoint = ((first & 0x07) << 18) |
                  ((static_cast<unsigned char>(text[i + 1]) & 0x3f) << 12) |
                  ((static_cast<unsigned char>(text[i + 2]) & 0x3f) << 6) |
                  (static_cast<unsigned char>(text[i + 3]) & 0x3f);
      length = 4;
    }
    if ((codepoint >= 0x0590 && codepoint <= 0x08ff) ||
        (codepoint >= 0xfb1d && codepoint <= 0xfdff) ||
        (codepoint >= 0xfe70 && codepoint <= 0xfeff) ||
        (codepoint >= 0x10800 && codepoint <= 0x10fff) ||
        (codepoint >= 0x1e800 && codepoint <= 0x1edff))
      return true;
    i += length;
  }
  return false;
}
std::string direction_attribute(const std::string &text) {
  return contains_rtl_text(text) ? " dir=\"rtl\"" : std::string{};
}
bool markdown_escapable(char c) { return c == '\\' || std::ispunct(static_cast<unsigned char>(c)); }
bool escaped_at(const std::string &text, std::size_t at) { std::size_t slashes = 0; while (at > slashes && text[at - slashes - 1] == '\\') ++slashes; return (slashes & 1) != 0; }
std::size_t markdown_delimiter_end(const std::string &text, const std::string &delimiter, std::size_t start) { std::size_t at = text.find(delimiter, start); while (at != std::string::npos && escaped_at(text, at)) at = text.find(delimiter, at + 1); return at; }
std::size_t markdown_url_end(const std::string &text, std::size_t start) {
  std::size_t nested = 0;
  for (std::size_t i = start; i < text.size(); ++i) {
    if (text[i] == '\\' && i + 1 < text.size() && markdown_escapable(text[i + 1])) {
      ++i;
      continue;
    }
    if (text[i] == '(') ++nested;
    else if (text[i] == ')') {
      if (nested == 0) return i;
      --nested;
    }
  }
  return std::string::npos;
}
bool https_url(const std::string &url) {
  if (url.size() < 8) return false;
  std::string scheme = url.substr(0, 8);
  std::transform(scheme.begin(), scheme.end(), scheme.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return scheme == "https://";
}
std::string notion_color_class(const std::string &source) {
  std::string color = attribute(source, "color");
  const std::string background = "_background";
  if (color.size() > background.size() &&
      color.compare(color.size() - background.size(), background.size(), background) == 0)
    color.replace(color.size() - background.size(), background.size(), "_bg");
  static const char *colors[] = {
    "gray", "brown", "orange", "yellow", "green", "blue", "purple", "pink", "red",
    "gray_bg", "brown_bg", "orange_bg", "yellow_bg", "green_bg", "blue_bg", "purple_bg", "pink_bg", "red_bg"
  };
  for (const char *candidate : colors) if (color == candidate) {
    std::replace(color.begin(), color.end(), '_', '-');
    return "notion-color notion-color-" + color;
  }
  return {};
}
std::string notion_block_color_class(const std::string &source) {
  const auto attributes = trailing_notion_attributes(source);
  return attributes.start == std::string::npos ? std::string{} : notion_color_class(" {" + attributes.text + "}");
}
std::string color_attribute(const std::string &source) {
  const std::string color = notion_block_color_class(source);
  return color.empty() ? std::string{} : " class=\"" + color + "\"";
}
std::string block_class_attribute(const std::string &source) {
  const std::string color = notion_block_color_class(source);
  return " class=\"potion-block" + (color.empty() ? std::string{} : " " + color) + "\"";
}
std::string editable_block_class_attribute(const std::string &source) {
  const std::string color = notion_block_color_class(source);
  return " class=\"potion-block potion-editable" + (color.empty() ? std::string{} : " " + color) + "\"";
}
std::string colored_inline(const std::string &source, const std::string &html) {
  const std::string color = notion_block_color_class(source);
  return color.empty() ? html : "<span class=\"" + color + "\">" + html + "</span>";
}
std::string notion_page_id(const std::string &url) {
  const auto suffix = url.find_first_of("?#");
  const std::string clean = url.substr(0, suffix);
  std::string id;
  for (auto it = clean.rbegin(); it != clean.rend() && id.size() < 32; ++it) {
    if (std::isxdigit(static_cast<unsigned char>(*it))) id += *it;
    else if (*it != '-' && !id.empty()) break;
  }
  if (id.size() != 32) return {};
  std::reverse(id.begin(), id.end());
  return id;
}
std::vector<std::string> table_cells(std::string line) {
  line = trim(line);
  if (!line.empty() && line.front() == '|') line.erase(line.begin());
  if (!line.empty() && line.back() == '|') line.pop_back();
  std::vector<std::string> cells; std::string cell;
  for (std::size_t i = 0; i < line.size(); ++i) {
    const char c = line[i];
    if (c == '\\' && i + 1 < line.size() && markdown_escapable(line[i + 1]))
      cell += line[++i];
    else if (c == '|') { cells.push_back(trim(cell)); cell.clear(); }
    else cell += c;
  }
  cells.push_back(trim(cell));
  return cells;
}
bool table_separator(const std::string &line) {
  const auto cells = table_cells(line);
  if (cells.empty()) return false;
  for (auto cell : cells) {
    cell = trim(cell);
    if (!cell.empty() && cell.front() == ':') cell.erase(cell.begin());
    if (!cell.empty() && cell.back() == ':') cell.pop_back();
    if (cell.size() < 3 || !std::all_of(cell.begin(), cell.end(), [](char c) { return c == '-'; })) return false;
  }
  return true;
}
}

std::string ImageRegistry::register_url(const std::string &url) { std::lock_guard<std::mutex> lock(mutex_); const std::string key = std::to_string(next_++); urls_[key] = url; return key; }
bool ImageRegistry::resolve(const std::string &key, std::string &url) const { std::lock_guard<std::mutex> lock(mutex_); auto it = urls_.find(key); if (it == urls_.end()) return false; url = it->second; return true; }
void ImageRegistry::clear() { std::lock_guard<std::mutex> lock(mutex_); urls_.clear(); }

std::string MarkdownRenderer::sanitize_url(const std::string &url) {
  std::string lower = url; std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (starts(lower, "https://") || starts(lower, "http://")) return html_escape(url);
  return {};
}

std::string MarkdownRenderer::inline_html(const std::string &text) const {
  std::string out;
  for (std::size_t i = 0; i < text.size();) {
    if (text[i] == '\\' && i + 1 < text.size() && markdown_escapable(text[i + 1])) { out += html_escape(text.substr(i + 1, 1)); i += 2; continue; }
    if (text.compare(i, 2, "![") == 0) {
      auto mid = text.find("](", i + 2), end = mid == std::string::npos ? mid : markdown_url_end(text, mid + 2);
      if (mid != std::string::npos && end != std::string::npos) {
        const std::string url = text.substr(mid + 2, end - mid - 2), caption = text.substr(i + 2, mid - i - 2);
        if (https_url(url)) { const std::string key = images_.register_url(url); out += "<figure class=\"potion-block\"><img data-src=\"http://127.0.0.1:8766/api/images/" + key + "\" alt=\"" + html_escape(caption) + "\"><figcaption>" + inline_html(caption) + "</figcaption></figure>"; }
        else out += inline_html(caption);
        i = end + 1; continue;
      }
    }
    if (text[i] == '[') { auto mid = text.find("](", i + 1), end = mid == std::string::npos ? mid : markdown_url_end(text, mid + 2); if (mid != std::string::npos && end != std::string::npos) { std::string safe = sanitize_url(text.substr(mid + 2, end - mid - 2)); std::string label = inline_html(text.substr(i + 1, mid - i - 1)); out += safe.empty() ? label : "<a href=\"" + safe + "\">" + label + "</a>"; i = end + 1; continue; } }
    struct Marker { const char *open; const char *close; const char *a; const char *b; };
    static const Marker markers[] = {{"**", "**", "<strong>", "</strong>"}, {"~~", "~~", "<del>", "</del>"}, {"`", "`", "<code>", "</code>"}, {"*", "*", "<em>", "</em>"}, {"$", "$", "", ""}};
    bool matched = false;
    for (const auto &m : markers) if (text.compare(i, std::strlen(m.open), m.open) == 0) { auto end = markdown_delimiter_end(text, m.close, i + std::strlen(m.open)); if (end != std::string::npos) { std::string body = text.substr(i + std::strlen(m.open), end - i - std::strlen(m.open)); if (m.open[0] == '$') { if (body.size() >= 2 && body.front() == '`' && body.back() == '`') body = body.substr(1, body.size() - 2); out += "<span class=\"math\" data-potion-atomic=\"1\">" + math_.render(body, false) + "</span>"; } else if (m.open[0] == '`') out += std::string(m.a) + html_escape(body) + m.b; else out += std::string(m.a) + inline_html(body) + m.b; i = end + std::strlen(m.close); matched = true; break; } }
    if (matched) continue;
    if (starts(text.substr(i), "<br>")) { out += "<br>"; i += 4; continue; }
    if (starts(text.substr(i), "<span ")) { auto gt = text.find('>', i), end = text.find("</span>", gt); if (gt != std::string::npos && end != std::string::npos) { std::string tag = text.substr(i, gt - i + 1), body = inline_html(text.substr(gt + 1, end - gt - 1)), color = notion_color_class(tag); if (tag.find("underline=\"true\"") != std::string::npos) body = "<u>" + body + "</u>"; out += color.empty() ? body : "<span class=\"" + color + "\">" + body + "</span>"; i = end + 7; continue; } }
    if (starts(text.substr(i), "<mention-") || starts(text.substr(i), "<page ") || starts(text.substr(i), "<database ")) { auto gt = text.find('>', i); if (gt != std::string::npos) { auto name_end = text.find_first_of(" />", i + 1); std::string name = text.substr(i + 1, name_end - i - 1), tag = text.substr(i, gt - i + 1), url = attribute(tag, "url"), safe = sanitize_url(url), page_id = notion_page_id(url); const bool self_closing = gt > i && text[gt - 1] == '/'; if (self_closing) { if (!page_id.empty() && (name == "page" || name == "mention-page")) out += "<button type=\"button\" class=\"child-page\" data-potion-atomic=\"1\" data-page-id=\"" + page_id + "\">&#9783; Notion page <span class=\"child-page-arrow\">&rsaquo;</span></button>"; else if (!safe.empty()) out += "<a class=\"notion-reference\" data-potion-atomic=\"1\" href=\"" + safe + "\">Notion reference</a>"; else out += "<span class=\"mention\" data-potion-atomic=\"1\">Notion mention</span>"; i = gt + 1; continue; } std::string close = "</" + name + ">"; const auto end = text.find(close, gt); if (end != std::string::npos) { std::string body = inline_html(text.substr(gt + 1, end - gt - 1)); if (!page_id.empty() && (name == "page" || name == "mention-page")) out += "<button type=\"button\" class=\"child-page\" data-potion-atomic=\"1\" data-page-id=\"" + page_id + "\">" + body + " <span class=\"child-page-arrow\">&rsaquo;</span></button>"; else out += safe.empty() ? "<span class=\"mention\" data-potion-atomic=\"1\">" + body + "</span>" : "<a class=\"notion-reference\" data-potion-atomic=\"1\" href=\"" + safe + "\">" + body + "</a>"; i = end + close.size(); continue; } } }
    out += html_escape(text.substr(i, 1)); ++i;
  }
  return out;
}

std::string MarkdownRenderer::render(const std::string &markdown) const {
  std::istringstream input(markdown); std::string line, out; bool code = false, equation = false, pipe_table = false, notion_table = false, last_plain_paragraph = false, last_image = false; std::string code_text, equation_text, table_header, last_plain_source, last_image_caption; std::vector<std::string> lists; std::vector<std::size_t> heading_toggles; std::size_t list_base_depth = 0, last_plain_output = 0, last_plain_depth = 0, last_image_depth = 0;
  auto close_lists = [&] { while (!lists.empty()) { out += "</li></" + lists.back() + ">"; lists.pop_back(); } list_base_depth = 0; };
  auto close_heading_toggles = [&](std::size_t depth) { if (!heading_toggles.empty() && depth <= heading_toggles.back()) close_lists(); while (!heading_toggles.empty() && depth <= heading_toggles.back()) { out += "</div></div>"; heading_toggles.pop_back(); } };
  auto table_row = [&](const std::string &row, const char *cell_tag) { out += "<tr>"; for (const auto &cell : table_cells(row)) out += std::string("<") + cell_tag + ">" + inline_html(cell) + "</" + cell_tag + ">"; out += "</tr>"; };
  auto close_pipe_table = [&] { if (pipe_table) { out += "</tbody></table>"; pipe_table = false; } };
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    std::size_t depth = 0; while (depth < line.size() && line[depth] == '\t') ++depth; std::string body = line.substr(depth); const bool preceding_plain_paragraph = last_plain_paragraph, preceding_image = last_image; last_plain_paragraph = false; last_image = false;
    if (code) { if (starts(body, "```")) { out += "<pre class=\"potion-block\"><code>" + html_escape(code_text) + "</code></pre>"; code = false; code_text.clear(); } else code_text += body + "\n"; continue; }
    if (equation) { if (trim(body) == "$$") { const std::string expression = trim(equation_text); out += "<div class=\"potion-block math display-math\">" + math_.render(expression, true) + "</div>"; equation = false; equation_text.clear(); } else equation_text += body + "\n"; continue; }
    if (!notion_table) close_heading_toggles(depth);
    if (starts(body, "```")) { close_lists(); code = true; continue; }
    if (trim(body) == "$$") { close_lists(); equation = true; continue; }
    const bool pipe_row = body.size() >= 2 && body.find('|') != std::string::npos && trim(body).front() == '|';
    if (pipe_table && pipe_row) { table_row(body, "td"); continue; }
    close_pipe_table();
    if (!table_header.empty()) {
      if (pipe_row && table_separator(body)) {
        close_lists(); out += "<table class=\"potion-block\"><thead>"; table_row(table_header, "th"); out += "</thead><tbody>";
        table_header.clear(); pipe_table = true; continue;
      }
      out += "<p class=\"potion-block potion-editable\"><span class=\"potion-editable-content\">" + inline_html(strip_attrs(table_header)) + "</span></p>"; table_header.clear();
    }
    if (pipe_row) { close_lists(); table_header = body; continue; }
    bool bullet = starts(body, "- ") || starts(body, "* "); std::size_t dot = body.find(". "); bool numbered = dot != std::string::npos && dot > 0 && std::all_of(body.begin(), body.begin() + static_cast<long>(dot), [](unsigned char c){ return std::isdigit(c); });
    if (bullet || numbered) {
      std::string tag = numbered ? "ol" : "ul", item = bullet ? body.substr(2) : body.substr(dot + 2), start = numbered ? body.substr(0, dot) : std::string{};
      const bool todo = starts(item, "[ ] ") || starts(item, "[x] ") || starts(item, "[X] ");
      const std::string item_open = todo ? "<li class=\"potion-block\">" : "<li class=\"potion-block potion-editable\"><span class=\"potion-editable-content\">";
      if (lists.empty()) list_base_depth = depth;
      else if (depth < list_base_depth) { close_lists(); list_base_depth = depth; }
      const std::size_t wanted = depth - list_base_depth + 1;
      while (lists.size() > wanted) { out += "</li></" + lists.back() + ">"; lists.pop_back(); }
      if (lists.size() == wanted && lists.back() != tag) {
        out += "</li></" + lists.back() + "><" + tag + (tag == "ol" && start != "1" ? " start=\"" + start + "\"" : "") + ">" + item_open;
        lists.back() = tag;
      } else if (lists.size() == wanted) {
        out += "</li>" + item_open;
      } else {
        while (lists.size() < wanted) { out += "<" + tag + (tag == "ol" && start != "1" ? " start=\"" + start + "\"" : "") + ">" + item_open; lists.push_back(tag); }
      }
      if (todo) { bool checked = item[1] != ' '; item = item.substr(4); out += "<span class=\"todo-box\">" + std::string(checked ? "&#9745;" : "&#9744;") + "</span> "; }
      out += colored_inline(item, inline_html(strip_attrs(item))) + (todo ? "" : "</span>"); continue;
    }
    close_lists(); body = trim(body); if (body.empty() || body == "<empty-block/>") { out += "<div class=\"potion-block empty-block\"></div>"; continue; }
    if (body == "---") { out += "<hr class=\"potion-block\">"; continue; }
    std::size_t hashes = 0; while (hashes < body.size() && body[hashes] == '#') ++hashes;
    if (hashes && hashes <= 6 && hashes < body.size() && body[hashes] == ' ') { unsigned level = std::min<unsigned>(4, hashes); const std::string heading_source = body.substr(hashes + 1); const bool toggle = trailing_notion_attributes(heading_source).toggle; const std::string heading = colored_inline(heading_source, inline_html(strip_attrs(heading_source))); if (toggle) { out += "<div class=\"toggle heading-toggle\"><button type=\"button\" class=\"potion-block toggle-summary\" aria-expanded=\"true\"><span class=\"toggle-arrow\">&#9662;</span><span class=\"toggle-heading toggle-heading-" + std::to_string(level) + "\">" + heading + "</span></button><div class=\"toggle-content\">"; heading_toggles.push_back(depth); } else out += "<h" + std::to_string(level) + " class=\"potion-block\">" + heading + "</h" + std::to_string(level) + ">"; continue; }
    if (starts(body, "> ")) { out += "<blockquote" + block_class_attribute(body) + ">" + inline_html(strip_attrs(body.substr(2))) + "</blockquote>"; continue; }
    if (starts(body, "<callout")) { const std::string color = notion_color_class(body); out += "<aside class=\"callout" + (color.empty() ? std::string{} : " " + color) + "\"><span class=\"callout-icon\">" + html_escape(attribute(body, "icon")) + "</span>"; continue; }
    if (body == "</callout>") { out += "</aside>"; continue; }
    if (starts(body, "<details")) { const std::string color = notion_color_class(body); out += "<div class=\"toggle" + (color.empty() ? std::string{} : " " + color) + "\">"; continue; }
    if (starts(body, "<summary>") && body.find("</summary>") != std::string::npos) { out += "<button type=\"button\" class=\"potion-block toggle-summary\" aria-expanded=\"false\"><span class=\"toggle-arrow\">&#9656;</span><span>" + inline_html(body.substr(9, body.size() - 19)) + "</span></button><div class=\"toggle-content hidden\">"; continue; }
    if (body == "</details>") { out += "</div></div>"; continue; }
    if (starts(body, "<table")) { notion_table = true; out += "<table class=\"potion-block\">"; continue; } if (body == "</table>") { out += "</table>"; notion_table = false; continue; }
    if (starts(body, "<tr")) { out += "<tr>"; continue; } if (body == "</tr>") { out += "</tr>"; continue; }
    if (starts(body, "<td")) { auto gt = body.find('>'), end = body.rfind("</td>"); out += "<td" + color_attribute(body) + ">" + (gt != std::string::npos && end != std::string::npos ? inline_html(body.substr(gt + 1, end - gt - 1)) : std::string("Unsupported cell")) + "</td>"; continue; }
    if (body == "<columns>" || body == "</columns>" || body == "<column>" || body == "</column>" || starts(body, "<col") || body == "</colgroup>") continue;
    if ((starts(body, "<page ") || starts(body, "<database ") || starts(body, "<mention-")) && body.find('>') != std::string::npos) { out += "<div class=\"potion-block notion-reference-row\">" + inline_html(body) + "</div>"; continue; }
    if (starts(body, "<span ") && body.find("</span>") != std::string::npos) { out += "<p class=\"potion-block potion-editable\"><span class=\"potion-editable-content\">" + inline_html(body) + "</span></p>"; continue; }
    if (starts(body, "![")) {
      const auto mid = body.find("](", 2), end = mid == std::string::npos ? mid : markdown_url_end(body, mid + 2);
      const std::string trailing = end == std::string::npos ? std::string{} : trim(body.substr(end + 1));
      if (mid != std::string::npos && end != std::string::npos &&
          (trailing.empty() || (trailing.front() == '{' && trailing.back() == '}'))) {
        const std::string caption = body.substr(2, mid - 2), url = body.substr(mid + 2, end - mid - 2);
        if (https_url(url)) {
          if (preceding_plain_paragraph && depth == last_plain_depth && caption == last_plain_source) out.resize(last_plain_output);
          out += inline_html(body.substr(0, end + 1)); last_image = true; last_image_caption = caption; last_image_depth = depth; continue;
        }
      }
    }
    if (body[0] == '<' && body.find('>') != std::string::npos) { out += "<div class=\"potion-block unsupported\">Unsupported Notion content</div>"; continue; }
    last_plain_source = strip_attrs(body); if (preceding_image && depth == last_image_depth && last_plain_source == last_image_caption) continue; last_plain_output = out.size(); last_plain_depth = depth; last_plain_paragraph = true; out += "<p" + editable_block_class_attribute(body) + direction_attribute(last_plain_source) + "><span class=\"potion-editable-content\">" + inline_html(last_plain_source) + "</span></p>";
  }
  close_lists(); close_pipe_table(); while (!heading_toggles.empty()) { out += "</div></div>"; heading_toggles.pop_back(); } if (!table_header.empty()) out += "<p class=\"potion-block potion-editable\"><span class=\"potion-editable-content\">" + inline_html(strip_attrs(table_header)) + "</span></p>"; if (code) out += "<pre class=\"potion-block\"><code>" + html_escape(code_text) + "</code></pre>"; if (equation) out += "<div class=\"potion-block unsupported\">Incomplete equation</div>"; return out;
}
}
