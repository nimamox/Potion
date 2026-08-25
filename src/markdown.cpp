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
std::string strip_attrs(std::string s) { auto at = s.rfind(" {"); if (at != std::string::npos && s.back() == '}') s.resize(at); return s; }
std::string attribute(const std::string &line, const std::string &name) { std::string needle = name + "=\""; auto a = line.find(needle); if (a == std::string::npos) return {}; a += needle.size(); auto b = line.find('"', a); return b == std::string::npos ? std::string{} : line.substr(a, b - a); }
bool starts(const std::string &s, const std::string &prefix) { return s.compare(0, prefix.size(), prefix) == 0; }
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
  const auto at = source.rfind(" {");
  if (at == std::string::npos || source.empty() || source.back() != '}') return {};
  return notion_color_class(source.substr(at));
}
std::string color_attribute(const std::string &source) {
  const std::string color = notion_block_color_class(source);
  return color.empty() ? std::string{} : " class=\"" + color + "\"";
}
std::string colored_inline(const std::string &source, const std::string &html) {
  const std::string color = notion_block_color_class(source);
  return color.empty() ? html : "<span class=\"" + color + "\">" + html + "</span>";
}
std::string notion_page_id(const std::string &url) {
  std::string id;
  for (auto it = url.rbegin(); it != url.rend() && id.size() < 32; ++it) {
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
  std::vector<std::string> cells; std::string cell; bool escaped = false;
  for (char c : line) {
    if (escaped) { cell += c; escaped = false; }
    else if (c == '\\') escaped = true;
    else if (c == '|') { cells.push_back(trim(cell)); cell.clear(); }
    else cell += c;
  }
  if (escaped) cell += '\\';
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
void ImageRegistry::clear() { std::lock_guard<std::mutex> lock(mutex_); urls_.clear(); next_ = 1; }

std::string MarkdownRenderer::sanitize_url(const std::string &url) {
  std::string lower = url; std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (starts(lower, "https://") || starts(lower, "http://")) return html_escape(url);
  return {};
}

std::string MarkdownRenderer::inline_html(const std::string &text) const {
  std::string out;
  for (std::size_t i = 0; i < text.size();) {
    if (text[i] == '\\' && i + 1 < text.size()) { out += html_escape(text.substr(i + 1, 1)); i += 2; continue; }
    if (text.compare(i, 2, "![") == 0) {
      auto mid = text.find("](", i + 2), end = mid == std::string::npos ? mid : text.find(')', mid + 2);
      if (mid != std::string::npos && end != std::string::npos) { std::string url = text.substr(mid + 2, end - mid - 2); if (!sanitize_url(url).empty()) { const std::string caption = text.substr(i + 2, mid - i - 2), key = images_.register_url(url); out += "<figure><img data-src=\"http://127.0.0.1:8766/api/images/" + key + "\" alt=\"" + html_escape(caption) + "\"><figcaption>" + inline_html(caption) + "</figcaption></figure>"; i = end + 1; continue; } }
    }
    if (text[i] == '[') { auto mid = text.find("](", i + 1), end = mid == std::string::npos ? mid : text.find(')', mid + 2); if (mid != std::string::npos && end != std::string::npos) { std::string safe = sanitize_url(text.substr(mid + 2, end - mid - 2)); std::string label = inline_html(text.substr(i + 1, mid - i - 1)); out += safe.empty() ? label : "<a href=\"" + safe + "\">" + label + "</a>"; i = end + 1; continue; } }
    struct Marker { const char *open; const char *close; const char *a; const char *b; };
    static const Marker markers[] = {{"**", "**", "<strong>", "</strong>"}, {"~~", "~~", "<del>", "</del>"}, {"`", "`", "<code>", "</code>"}, {"*", "*", "<em>", "</em>"}, {"$", "$", "<span class=\"math\" data-expr=\"", "</span>"}};
    bool matched = false;
    for (const auto &m : markers) if (text.compare(i, std::strlen(m.open), m.open) == 0) { auto end = text.find(m.close, i + std::strlen(m.open)); if (end != std::string::npos) { std::string body = text.substr(i + std::strlen(m.open), end - i - std::strlen(m.open)); if (m.open[0] == '$') { if (body.size() >= 2 && body.front() == '`' && body.back() == '`') body = body.substr(1, body.size() - 2); out += std::string(m.a) + html_escape(body) + "\">" + html_escape(body) + m.b; } else if (m.open[0] == '`') out += std::string(m.a) + html_escape(body) + m.b; else out += std::string(m.a) + inline_html(body) + m.b; i = end + std::strlen(m.close); matched = true; break; } }
    if (matched) continue;
    if (starts(text.substr(i), "<br>")) { out += "<br>"; i += 4; continue; }
    if (starts(text.substr(i), "<span ")) { auto gt = text.find('>', i), end = text.find("</span>", gt); if (gt != std::string::npos && end != std::string::npos) { std::string tag = text.substr(i, gt - i + 1), body = inline_html(text.substr(gt + 1, end - gt - 1)), color = notion_color_class(tag); if (tag.find("underline=\"true\"") != std::string::npos) body = "<u>" + body + "</u>"; out += color.empty() ? body : "<span class=\"" + color + "\">" + body + "</span>"; i = end + 7; continue; } }
    if (starts(text.substr(i), "<mention-") || starts(text.substr(i), "<page ") || starts(text.substr(i), "<database ")) { auto gt = text.find('>', i); if (gt != std::string::npos) { auto name_end = text.find_first_of(" >", i + 1); std::string name = text.substr(i + 1, name_end - i - 1), close = "</" + name + ">"; const auto end = text.find(close, gt); if (end != std::string::npos) { const std::string url = attribute(text.substr(i, gt - i + 1), "url"), safe = sanitize_url(url), page_id = notion_page_id(url); std::string body = inline_html(text.substr(gt + 1, end - gt - 1)); if (!page_id.empty() && (name == "page" || name == "mention-page")) out += "<button type=\"button\" class=\"child-page\" data-page-id=\"" + page_id + "\">" + body + " <span class=\"child-page-arrow\">&rsaquo;</span></button>"; else out += safe.empty() ? "<span class=\"mention\">" + body + "</span>" : "<a class=\"notion-reference\" href=\"" + safe + "\">" + body + "</a>"; i = end + close.size(); continue; } } }
    out += html_escape(text.substr(i, 1)); ++i;
  }
  return out;
}

std::string MarkdownRenderer::render(const std::string &markdown) const {
  std::istringstream input(markdown); std::string line, out; bool code = false, equation = false, pipe_table = false, notion_table = false, last_plain_paragraph = false, last_image = false; std::string code_text, equation_text, table_header, last_plain_source, last_image_caption; std::vector<std::string> lists; std::vector<std::size_t> heading_toggles; std::size_t list_base_depth = 0, last_plain_output = 0;
  auto close_lists = [&] { while (!lists.empty()) { out += "</li></" + lists.back() + ">"; lists.pop_back(); } list_base_depth = 0; };
  auto close_heading_toggles = [&](std::size_t depth) { if (!heading_toggles.empty() && depth <= heading_toggles.back()) close_lists(); while (!heading_toggles.empty() && depth <= heading_toggles.back()) { out += "</div></div>"; heading_toggles.pop_back(); } };
  auto table_row = [&](const std::string &row, const char *cell_tag) { out += "<tr>"; for (const auto &cell : table_cells(row)) out += std::string("<") + cell_tag + ">" + inline_html(cell) + "</" + cell_tag + ">"; out += "</tr>"; };
  auto close_pipe_table = [&] { if (pipe_table) { out += "</tbody></table>"; pipe_table = false; } };
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    std::size_t depth = 0; while (depth < line.size() && line[depth] == '\t') ++depth; std::string body = line.substr(depth); const bool preceding_plain_paragraph = last_plain_paragraph, preceding_image = last_image; last_plain_paragraph = false; last_image = false;
    if (code) { if (starts(body, "```")) { out += "<pre><code>" + html_escape(code_text) + "</code></pre>"; code = false; code_text.clear(); } else code_text += body + "\n"; continue; }
    if (equation) { if (trim(body) == "$$") { out += "<div class=\"math display-math\" data-expr=\"" + html_escape(trim(equation_text)) + "\">" + html_escape(trim(equation_text)) + "</div>"; equation = false; equation_text.clear(); } else equation_text += body + "\n"; continue; }
    if (!notion_table && !starts(body, "<table>") && !starts(body, "<table ")) close_heading_toggles(depth);
    if (starts(body, "```")) { close_lists(); code = true; continue; }
    if (trim(body) == "$$") { close_lists(); equation = true; continue; }
    const bool pipe_row = body.size() >= 2 && body.find('|') != std::string::npos && trim(body).front() == '|';
    if (pipe_table && pipe_row) { table_row(body, "td"); continue; }
    close_pipe_table();
    if (!table_header.empty()) {
      if (pipe_row && table_separator(body)) {
        close_lists(); out += "<table><thead>"; table_row(table_header, "th"); out += "</thead><tbody>";
        table_header.clear(); pipe_table = true; continue;
      }
      out += "<p>" + inline_html(strip_attrs(table_header)) + "</p>"; table_header.clear();
    }
    if (pipe_row) { close_lists(); table_header = body; continue; }
    bool bullet = starts(body, "- ") || starts(body, "* "); std::size_t dot = body.find(". "); bool numbered = dot > 0 && dot < 5 && std::all_of(body.begin(), body.begin() + static_cast<long>(dot), [](unsigned char c){ return std::isdigit(c); });
    if (bullet || numbered) {
      std::string tag = numbered ? "ol" : "ul", item = bullet ? body.substr(2) : body.substr(dot + 2);
      if (lists.empty()) list_base_depth = depth;
      else if (depth < list_base_depth) { close_lists(); list_base_depth = depth; }
      const std::size_t wanted = depth - list_base_depth + 1;
      while (lists.size() > wanted) { out += "</li></" + lists.back() + ">"; lists.pop_back(); }
      if (lists.size() == wanted && lists.back() != tag) {
        out += "</li></" + lists.back() + "><" + tag + "><li>";
        lists.back() = tag;
      } else if (lists.size() == wanted) {
        out += "</li><li>";
      } else {
        while (lists.size() < wanted) { out += "<" + tag + "><li>"; lists.push_back(tag); }
      }
      bool todo = starts(item, "[ ] ") || starts(item, "[x] ") || starts(item, "[X] "); if (todo) { bool checked = item[1] != ' '; item = item.substr(4); out += "<span class=\"todo-box\">" + std::string(checked ? "&#9745;" : "&#9744;") + "</span> "; }
      out += colored_inline(item, inline_html(strip_attrs(item))); continue;
    }
    close_lists(); body = trim(body); if (body.empty() || body == "<empty-block/>") { out += "<div class=\"empty-block\"></div>"; continue; }
    if (body == "---") { out += "<hr>"; continue; }
    std::size_t hashes = 0; while (hashes < body.size() && body[hashes] == '#') ++hashes;
    if (hashes && hashes <= 6 && hashes < body.size() && body[hashes] == ' ') { unsigned level = std::min<unsigned>(4, hashes); const bool toggle = body.find("toggle=\"true\"") != std::string::npos; const std::string heading = colored_inline(body, inline_html(strip_attrs(body.substr(hashes + 1)))); if (toggle) { out += "<div class=\"toggle heading-toggle\"><button type=\"button\" class=\"toggle-summary\" aria-expanded=\"true\"><span class=\"toggle-arrow\">&#9662;</span><span class=\"toggle-heading toggle-heading-" + std::to_string(level) + "\">" + heading + "</span></button><div class=\"toggle-content\">"; heading_toggles.push_back(depth); } else out += "<h" + std::to_string(level) + ">" + heading + "</h" + std::to_string(level) + ">"; continue; }
    if (starts(body, "> ")) { out += "<blockquote" + color_attribute(body) + ">" + inline_html(strip_attrs(body.substr(2))) + "</blockquote>"; continue; }
    if (starts(body, "<callout")) { const std::string color = notion_color_class(body); out += "<aside class=\"callout" + (color.empty() ? std::string{} : " " + color) + "\"><span class=\"callout-icon\">" + html_escape(attribute(body, "icon")) + "</span>"; continue; }
    if (body == "</callout>") { out += "</aside>"; continue; }
    if (starts(body, "<details")) { const std::string color = notion_color_class(body); out += "<div class=\"toggle" + (color.empty() ? std::string{} : " " + color) + "\">"; continue; }
    if (starts(body, "<summary>") && body.find("</summary>") != std::string::npos) { out += "<button type=\"button\" class=\"toggle-summary\" aria-expanded=\"false\"><span class=\"toggle-arrow\">&#9656;</span><span>" + inline_html(body.substr(9, body.size() - 19)) + "</span></button><div class=\"toggle-content hidden\">"; continue; }
    if (body == "</details>") { out += "</div></div>"; continue; }
    if (starts(body, "<table")) { notion_table = true; out += "<table>"; continue; } if (body == "</table>") { out += "</table>"; notion_table = false; continue; }
    if (starts(body, "<tr")) { out += "<tr>"; continue; } if (body == "</tr>") { out += "</tr>"; continue; }
    if (starts(body, "<td")) { auto gt = body.find('>'), end = body.rfind("</td>"); out += "<td" + color_attribute(body) + ">" + (gt != std::string::npos && end != std::string::npos ? inline_html(body.substr(gt + 1, end - gt - 1)) : std::string("Unsupported cell")) + "</td>"; continue; }
    if (body == "<columns>" || body == "</columns>" || body == "<column>" || body == "</column>" || starts(body, "<col") || body == "</colgroup>") continue;
    if ((starts(body, "<page ") || starts(body, "<database ") || starts(body, "<mention-")) && body.find('>') != std::string::npos) { out += "<div class=\"notion-reference-row\">" + inline_html(body) + "</div>"; continue; }
    if (starts(body, "<span ") && body.find("</span>") != std::string::npos) { out += "<p>" + inline_html(body) + "</p>"; continue; }
    if (starts(body, "![")) { const auto mid = body.find("](", 2), end = mid == std::string::npos ? mid : body.rfind(')'); if (mid != std::string::npos && end == body.size() - 1) { const std::string caption = body.substr(2, mid - 2); if (preceding_plain_paragraph && caption == last_plain_source) out.resize(last_plain_output); out += inline_html(body); last_image = true; last_image_caption = caption; continue; } }
    if (body[0] == '<' && body.find('>') != std::string::npos) { out += "<div class=\"unsupported\">Unsupported Notion content</div>"; continue; }
    last_plain_source = strip_attrs(body); if (preceding_image && last_plain_source == last_image_caption) continue; last_plain_output = out.size(); last_plain_paragraph = true; out += "<p" + color_attribute(body) + ">" + inline_html(last_plain_source) + "</p>";
  }
  close_lists(); close_pipe_table(); while (!heading_toggles.empty()) { out += "</div></div>"; heading_toggles.pop_back(); } if (!table_header.empty()) out += "<p>" + inline_html(strip_attrs(table_header)) + "</p>"; if (code) out += "<pre><code>" + html_escape(code_text) + "</code></pre>"; if (equation) out += "<div class=\"unsupported\">Incomplete equation</div>"; return out;
}
}
