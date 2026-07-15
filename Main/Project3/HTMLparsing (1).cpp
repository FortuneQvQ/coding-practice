#define PUGIXML_HEADER_ONLY

#include "HTMLparsing.h"

#include <algorithm>
#include <cctype>
#include <regex>
#include <sstream>
#include <unordered_set>
#include "crawler.h"

using json = nlohmann::json;

namespace
{
std::string lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool contains_any(const std::string& value, const std::vector<std::string>& words)
{
    for (const auto& word : words)
        if (value.find(word) != std::string::npos) return true;
    return false;
}

std::string collapse_text(const std::string& value)
{
    std::string clean_value = value;
    const std::size_t last_lt = clean_value.rfind('<');
    const std::size_t last_gt = clean_value.rfind('>');
    if (last_lt != std::string::npos &&
        (last_gt == std::string::npos || last_lt > last_gt) &&
        last_lt + 1 < clean_value.size())
    {
        const unsigned char marker = static_cast<unsigned char>(clean_value[last_lt + 1]);
        if (std::isalpha(marker) || marker == '/' || marker == '!' || marker == '?')
            clean_value.erase(last_lt);
    }
    std::string result;
    bool whitespace = false;
    for (unsigned char c : clean_value)
    {
        if (std::isspace(c))
        {
            whitespace = true;
            continue;
        }
        if (whitespace && !result.empty()) result.push_back(' ');
        whitespace = false;
        result.push_back(static_cast<char>(c));
    }
    return result;
}

std::string node_text(const pugi::xml_node& node)
{
    pugi::xpath_query query("string(.)");
    return collapse_text(query.evaluate_string(node));
}

bool has_bad_marker(const pugi::xml_node& node)
{
    const std::string name = lower(node.name());
    if (name == "nav" || name == "header" || name == "footer" || name == "aside" ||
        name == "script" || name == "style" || name == "noscript")
        return true;

    const std::string marker = lower(
        std::string(node.attribute("id").value()) + " " + node.attribute("class").value());
    return marker.find("nav") != std::string::npos ||
           marker.find("menu") != std::string::npos ||
           marker.find("footer") != std::string::npos ||
           marker.find("sidebar") != std::string::npos ||
           marker.find("comment") != std::string::npos ||
           marker.find("share") != std::string::npos ||
           marker.find("breadcrumb") != std::string::npos;
}

bool is_content_container(const pugi::xml_node& node)
{
    if (has_bad_marker(node)) return false;
    const std::string name = lower(node.name());
    const std::string marker = lower(
        std::string(node.attribute("id").value()) + " " + node.attribute("class").value());
    return name == "article" || marker.find("article") != std::string::npos ||
           marker.find("content") != std::string::npos || marker.find("detail") != std::string::npos ||
           marker.find("main-text") != std::string::npos || marker.find("main_text") != std::string::npos;
}

std::string text_from_html_fragment(const std::string& html)
{
    if (html.find('<') == std::string::npos) return collapse_text(html);
    // CMS fragments are frequently not balanced XML.  A failed XML parse used to return the
    // raw fragment and store tags in SQLite; strip scripts/styles and tags mechanically.
    static const std::regex script_pattern(
        R"REGEX(<(script|style)\b[^>]*>[\s\S]*?</\1\s*>)REGEX", std::regex::icase);
    static const std::regex tag_pattern(R"(<[^>]+>)");
    std::string text = std::regex_replace(html, script_pattern, " ");
    text = std::regex_replace(text, tag_pattern, " ");
    const std::vector<std::pair<std::string, std::string>> entities = {
        {"&nbsp;", " "}, {"&amp;", "&"}, {"&quot;", "\""},
        {"&#39;", "'"}, {"&lt;", "<"}, {"&gt;", ">"}
    };
    for (const auto& entity : entities)
    {
        std::size_t pos = 0;
        while ((pos = text.find(entity.first, pos)) != std::string::npos)
            text.replace(pos, entity.first.size(), entity.second);
    }
    return collapse_text(text);
}

std::string utf8_prefix(const std::string& text, std::size_t max_bytes)
{
    if (text.size() <= max_bytes) return text;
    std::size_t end = max_bytes;
    // If the byte immediately after the prefix is a UTF-8 continuation byte, the prefix
    // ends inside a code point.  Move back to the leading byte and exclude that character.
    while (end > 0 &&
           (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80)
        --end;
    return text.substr(0, end);
}

std::string make_abstract(const std::string& content)
{
    return utf8_prefix(content, 240) + (content.size() > 240 ? "..." : "");
}

std::set<std::string> selected_link_set(const std::string& html, const std::string& page_url)
{
    std::vector<LinkCandidate> raw = extract_link_candidates(html, page_url);
    const std::vector<LinkCandidate> selected = select_news_links(raw, page_url, 20);
    std::set<std::string> links;
    for (const auto& candidate : selected) links.insert(candidate.url);
    return links;
}

bool is_title_key(const std::string& key)
{
    const std::string value = lower(key);
    return value == "title" || value == "headline" || value == "name";
}

bool is_time_key(const std::string& key)
{
    const std::string value = lower(key);
    return value == "datepublished" || value == "publishtime" || value == "publishdate" || value == "date";
}

bool is_content_key(const std::string& key)
{
    const std::string value = lower(key);
    return value == "articlebody" || value == "content" || value == "body" || value == "description";
}

bool build_news_from_json(const json& value, News& out_news)
{
    if (!value.is_object()) return false;

    std::string title;
    std::string time;
    std::string content;
    std::string image;
    for (auto it = value.begin(); it != value.end(); ++it)
    {
        if (!it.value().is_string()) continue;
        const std::string raw_text = it.value().get<std::string>();
        const std::string text = collapse_text(raw_text);
        if (title.empty() && is_title_key(it.key()) && text.size() >= 4) title = text;
        if (time.empty() && is_time_key(it.key())) time = text;
        if (content.empty() && is_content_key(it.key()))
        {
            static const std::regex image_pattern(
                R"REGEX(<img\b[^>]*(?:src|data-src|data-original)\s*=\s*["']([^"']+)["'])REGEX",
                std::regex::icase);
            std::smatch image_match;
            if (std::regex_search(raw_text, image_match, image_pattern)) image = image_match[1].str();
            content = text_from_html_fragment(raw_text);
        }
    }
    if (!title.empty() && (content.size() >= 80 || !image.empty()))
    {
        out_news.title = title;
        out_news.time = time;
        out_news.content = content.size() >= 80
            ? utf8_prefix(content, 8000)
            : "正文以图片形式发布，请查看 news.image 字段中的原始图片。";
        out_news.image = image;
        out_news.abstract = make_abstract(out_news.content);
        return true;
    }

    for (auto it = value.begin(); it != value.end(); ++it)
    {
        if (it.value().is_object() && build_news_from_json(it.value(), out_news)) return true;
        if (it.value().is_array())
        {
            for (const auto& item : it.value())
                if (build_news_from_json(item, out_news)) return true;
        }
    }
    return false;
}

std::string balanced_json(const std::string& text, std::size_t start)
{
    const char opening = text[start];
    const char closing = opening == '{' ? '}' : ']';
    std::vector<char> stack;
    bool in_string = false;
    bool escaped = false;
    for (std::size_t i = start; i < text.size(); ++i)
    {
        const char c = text[i];
        if (in_string)
        {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') in_string = false;
            continue;
        }
        if (c == '"') in_string = true;
        else if (c == '{' || c == '[') stack.push_back(c);
        else if (c == '}' || c == ']')
        {
            if (stack.empty()) return "";
            const char expected = stack.back() == '{' ? '}' : ']';
            if (c != expected) return "";
            stack.pop_back();
            if (stack.empty()) return text.substr(start, i - start + 1);
        }
    }
    return "";
}

bool parse_json_text(const std::string& text, News& out_news)
{
    const std::string direct = collapse_text(text);
    json parsed = json::parse(direct, nullptr, false);
    if (!parsed.is_discarded() && build_news_from_json(parsed, out_news)) return true;

    for (std::size_t pos = 0; pos < text.size(); ++pos)
    {
        if (text[pos] != '{' && text[pos] != '[') continue;
        const std::string candidate = balanced_json(text, pos);
        if (candidate.empty()) continue;
        parsed = json::parse(candidate, nullptr, false);
        if (!parsed.is_discarded() && build_news_from_json(parsed, out_news)) return true;
    }
    return false;
}
}

std::string get_baseUrl(const std::string& url)
{
    const std::string normalized = normalize_url(url);
    const auto scheme_end = normalized.find("://");
    if (scheme_end == std::string::npos) return "";
    const auto path_start = normalized.find('/', scheme_end + 3);
    return path_start == std::string::npos ? normalized : normalized.substr(0, path_start);
}

std::string resolve_Url(const std::string& baseUrl, const std::string& relativeUrl)
{
    return resolve_url(baseUrl, relativeUrl);
}

std::vector<LinkCandidate> extract_link_candidates(const std::string& html, const std::string& page_url)
{
    std::vector<LinkCandidate> result;
    // List discovery deliberately avoids HTML Tidy: malformed legacy list pages can crash
    // its Debug build, while anchor extraction does not require a repaired DOM.
    if (false)
    {
        pugi::xml_document doc;
        const auto link_nodes = doc.select_nodes("//a[@href] | //area[@href]");
        for (const auto& node : link_nodes)
        {
            const std::string raw = node.attribute().value();
            const std::string url = resolve_url(page_url, raw);
            if (url.empty()) continue;
            result.push_back({url, node_text(node.node()), 0});
        }
    }

    // Extract anchors directly from the original response.
    if (result.empty())
    {
        static const std::regex double_quoted(
            R"REGEX(<a\b[^>]*\bhref\s*=\s*"([^"]+)"[^>]*>([\s\S]*?)<\/a\s*>)REGEX",
            std::regex::icase);
        static const std::regex single_quoted(
            R"REGEX(<a\b[^>]*\bhref\s*=\s*'([^']+)'[^>]*>([\s\S]*?)<\/a\s*>)REGEX",
            std::regex::icase);
        static const std::regex tags(R"(<[^>]+>)");
        auto decode_entities = [](std::string value) {
            const std::vector<std::pair<std::string, std::string>> entities = {
                {"&amp;", "&"}, {"&quot;", "\""}, {"&#39;", "'"}, {"&nbsp;", " "},
                {"&raquo;", ">"}, {"&laquo;", "<"}
            };
            for (const auto& entity : entities)
            {
                std::size_t pos = 0;
                while ((pos = value.find(entity.first, pos)) != std::string::npos)
                {
                    value.replace(pos, entity.first.size(), entity.second);
                    pos += entity.second.size();
                }
            }
            return value;
        };
        auto append_matches = [&](const std::regex& pattern) {
            for (std::sregex_iterator it(html.begin(), html.end(), pattern), end; it != end; ++it)
            {
                const std::string raw = decode_entities((*it)[1].str());
                const std::string url = resolve_url(page_url, raw);
                if (url.empty()) continue;
                const std::string text = collapse_text(
                    decode_entities(std::regex_replace((*it)[2].str(), tags, " ")));
                result.push_back({url, text, 0});
            }
        };
        append_matches(double_quoted);
        append_matches(single_quoted);
    }
    return result;
}

std::set<std::string> extract_all_links(const std::string& html, const std::string& base_url)
{
    std::set<std::string> result;
    for (const auto& candidate : extract_link_candidates(html, base_url)) result.insert(candidate.url);
    return result;
}

namespace
{
constexpr int kTidyNativeException = -10000;

// libtidy is a native C library.  A malformed legacy page can raise a Windows access
// violation inside tidyd.dll; a normal C++ try/catch cannot catch that exception.  Keep
// the calls in a small function without C++ objects so MSVC can safely apply SEH.  The
// caller treats an exception exactly like an ordinary parse failure and skips one page.
int run_tidy_with_native_guard(TidyDoc document, const char* input, TidyBuffer* output)
{
#if defined(_MSC_VER) && defined(_WIN32)
    __try
    {
#endif
        int rc = tidyParseString(document, input);
        if (rc >= 0) rc = tidyCleanAndRepair(document);
        if (rc >= 0) rc = tidySaveBuffer(document, output);
        return rc;
#if defined(_MSC_VER) && defined(_WIN32)
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return kTidyNativeException;
    }
#endif
}

// Cleanup is guarded separately because a failed native parse may leave libtidy's
// internal structures partially initialised.  A cleanup failure must not terminate the
// crawler either.  At worst this leaks one failed page's temporary allocation.
void release_tidy_with_native_guard(TidyDoc document, TidyBuffer* output)
{
#if defined(_MSC_VER) && defined(_WIN32)
    __try
    {
#endif
        tidyBufFree(output);
        tidyRelease(document);
#if defined(_MSC_VER) && defined(_WIN32)
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        // Deliberately continue: the crawler can still process the remaining seeds.
    }
#endif
}
}

std::string tidy_html(const std::string& html_input)
{
    if (html_input.empty()) return {};

    TidyDoc tdoc = tidyCreate();
    if (tdoc == nullptr) return {};
    TidyBuffer output = {0};
    tidyOptSetBool(tdoc, TidyXhtmlOut, yes);
    tidyOptSetBool(tdoc, TidyQuiet, yes);
    tidyOptSetBool(tdoc, TidyShowWarnings, no);
    tidyOptSetBool(tdoc, TidyShowInfo, no);

    const int rc = run_tidy_with_native_guard(tdoc, html_input.c_str(), &output);
    if (rc >= 0)
    {
        std::string cleaned(reinterpret_cast<char*>(output.bp), output.size);
        release_tidy_with_native_guard(tdoc, &output);
        return cleaned;
    }
    release_tidy_with_native_guard(tdoc, &output);
    if (rc == kTidyNativeException)
        std::cerr << "[parse] tidyd.dll native exception; skipped malformed page\n";
    return {};
}

bool parse_embedded_news_json(const std::string& html, News& out_news)
{
    const std::string original_url = out_news.url;
    News direct;
    if (parse_json_text(html, direct))
    {
        direct.url = original_url;
        if (!direct.image.empty()) direct.image = resolve_url(original_url, direct.image);
        out_news = direct;
        return true;
    }

    const std::string xhtml = tidy_html(html);
    if (xhtml.empty()) return false;
    pugi::xml_document doc;
    if (!doc.load_string(xhtml.c_str(), pugi::parse_full)) return false;

    const auto scripts = doc.select_nodes("//script");
    for (const auto& item : scripts)
    {
        const pugi::xml_node script = item.node();
        const std::string type = lower(script.attribute("type").value());
        const std::string id = lower(script.attribute("id").value());
        if (!type.empty() && type != "application/ld+json" && id != "__next_data__" &&
            id != "__initial_state__" && id != "__nuxt__")
            continue;
        News parsed;
        if (parse_json_text(script.text().get(), parsed))
        {
            parsed.url = original_url;
            if (!parsed.image.empty()) parsed.image = resolve_url(original_url, parsed.image);
            out_news = parsed;
            return true;
        }
    }
    return false;
}

std::vector<std::string> extract_json_api_candidates(const std::string& html, const std::string& page_url)
{
    std::vector<std::string> result;
    std::unordered_set<std::string> seen;
    static const std::regex url_pattern(R"((https?:\/\/[^"'<>\s]+|\/[^"'<>\s]+))");

    for (std::sregex_iterator it(html.begin(), html.end(), url_pattern), end; it != end; ++it)
    {
        std::string raw = (*it)[1].str();
        std::replace(raw.begin(), raw.end(), '\\', ' ');
        raw.erase(std::remove(raw.begin(), raw.end(), ' '), raw.end());
        const std::string lower_raw = lower(raw);
        if (lower_raw.find(".json") == std::string::npos &&
            lower_raw.find("/api/") == std::string::npos &&
            lower_raw.find("/ajax/") == std::string::npos)
            continue;

        const std::string resolved = resolve_url(page_url, raw);
        if (resolved.empty() || !is_same_domain(resolved, page_url) || !seen.insert(resolved).second)
            continue;
        result.push_back(resolved);
        if (result.size() == 3) break;
    }
    return result;
}

bool extract_engine2_list_request(const std::string& html, const std::string& page_url,
                                  Engine2ListRequest& out_request,
                                  int page_num, int page_size)
{
    out_request = Engine2ListRequest{};
    if (lower(page_url).find("/engine2/") == std::string::npos) return false;

    static const std::regex app_id_pattern(R"(var\s+appId\s*=\s*([0-9]+))");
    static const std::regex engine_pattern(R"(var\s+engineInstanceId\s*=\s*([0-9]+))");
    static const std::regex sign_pattern(R"regex(var\s+sign\s*=\s*"([^"]+)")regex");
    static const std::regex type_pattern(R"regex(var\s+typeId\s*=\s*"([^"]*)")regex");

    std::smatch match;
    auto value_for = [&](const std::regex& pattern, std::string& value) {
        if (!std::regex_search(html, match, pattern) || match.size() < 2) return false;
        value = match[1].str();
        return !value.empty();
    };

    std::string sign;
    if (!value_for(app_id_pattern, out_request.app_id) ||
        !value_for(engine_pattern, out_request.engine_instance_id) ||
        !value_for(sign_pattern, sign))
        return false;
    // 部分 engine2 入口用空 typeId 表示“全部分类”。接口实际接受 -1。
    if (!std::regex_search(html, match, type_pattern) || match.size() < 2 || match[1].str().empty())
        out_request.type_id = "-1";
    else
        out_request.type_id = match[1].str();

    const std::string base = get_baseUrl(page_url);
    if (base.empty()) return false;
    out_request.endpoint = base + "/engine2/general/" + out_request.app_id + "/type/more-datas";
    out_request.form_data = "engineInstanceId=" + out_request.engine_instance_id +
        "&sign=" + sign +
        "&pageNum=" + std::to_string((std::max)(1, page_num)) +
        "&pageSize=" + std::to_string((std::max)(1, page_size)) + "&typeId=" + out_request.type_id +
        "&topTypeId=&sw=&relId=&startDate=&endDate=&typeDataSort=-1&needViewNum=false&letter=";
    return true;
}

std::vector<LinkCandidate> extract_engine2_detail_links(const std::string& json_response,
                                                        const std::string& page_url,
                                                        const Engine2ListRequest& request,
                                                        std::size_t limit,
                                                        int* total_pages)
{
    std::vector<LinkCandidate> result;
    const json parsed = json::parse(json_response, nullptr, false);
    if (parsed.is_discarded() || !parsed.contains("data")) return result;
    const auto& data = parsed["data"];
    if (!data.contains("datas") || !data["datas"].contains("datas") || !data["datas"]["datas"].is_array())
        return result;
    if (total_pages != nullptr && data["datas"].contains("totalPage") && data["datas"]["totalPage"].is_number_integer())
        *total_pages = data["datas"]["totalPage"].get<int>();

    const std::string base = get_baseUrl(page_url);
    if (base.empty()) return result;
    std::unordered_set<std::string> seen;
    for (const auto& item : data["datas"]["datas"])
    {
        if (!item.is_object() || !item.contains("id")) continue;
        std::string id;
        if (item["id"].is_number_integer()) id = std::to_string(item["id"].get<long long>());
        else if (item["id"].is_string()) id = item["id"].get<std::string>();
        if (id.empty()) continue;

        std::string title;
        if (item.contains("title") && item["title"].is_string()) title = item["title"].get<std::string>();
        const std::string url = base + "/engine2/d/" + id + "/" + request.engine_instance_id +
            "/0/" + request.app_id + "?t=" + request.type_id + "&p=1";
        if (!seen.insert(url).second) continue;
        result.push_back({url, title, 100});
        if (result.size() >= limit) break;
    }
    return result;
}

std::string extract_next_page_link(const std::string& html, const std::string& page_url)
{
    // Pagination is link metadata; avoid repairing malformed list HTML with Tidy.
    if (false)
    {
        pugi::xml_document doc;
        for (const auto& item : doc.select_nodes("//a[@href]"))
        {
            const pugi::xml_node node = item.node();
            const std::string text = lower(node_text(node));
            const std::string rel = lower(node.attribute("rel").value());
            const std::string marker = lower(std::string(node.attribute("class").value()) + " " +
                                             node.parent().attribute("class").value());
            const bool is_next = rel == "next" || text == "下一页" || text == "下页" ||
                                 text == "next" || text == ">" || text == "›" || text == "»" ||
                                 marker.find("p_next") != std::string::npos ||
                                 marker.find("page-next") != std::string::npos;
            if (!is_next) continue;
            const std::string resolved = resolve_url(page_url, node.attribute("href").value());
            if (!resolved.empty() && is_same_domain(resolved, page_url) && normalize_url(resolved) != normalize_url(page_url))
                return resolved;
        }
    }

    // DOM 失败时使用原始 HTML 提取器提供的锚文本识别“下一页”。
    for (const auto& candidate : extract_link_candidates(html, page_url))
    {
        const std::string text = lower(collapse_text(candidate.anchor_text));
        if (text == "下一页" || text == "下页" || text == "next" ||
            text == ">" || text == "›" || text == "»")
        {
            if (is_same_domain(candidate.url, page_url) &&
                normalize_url(candidate.url) != normalize_url(page_url))
                return candidate.url;
        }
    }

    // Some legacy sites submit pagination through JavaScript and a hidden POST form, so the
    // anchor itself is only "#".  Their backend also accepts the same fields as a query string;
    // synthesize that stable URL so it can be persisted as the backfill cursor.
    static const std::regex current_pattern(
        R"REGEX(<span[^>]*class\s*=\s*["']current["'][^>]*>\s*([0-9]+)\s*</span>)REGEX",
        std::regex::icase);
    static const std::regex page_count_pattern(
        R"REGEX(turnpageFormpage\s*\(\s*[0-9]+\s*,\s*([0-9]+))REGEX",
        std::regex::icase);
    static const std::regex dispatch_pattern(
        R"REGEX(name\s*=\s*["']dispatch["'][^>]*value\s*=\s*["']([^"']+)["'])REGEX",
        std::regex::icase);
    static const std::regex type_pattern(
        R"REGEX(name\s*=\s*["']ntype_id["'][^>]*value\s*=\s*["']([^"']+)["'])REGEX",
        std::regex::icase);
    std::smatch current_match;
    std::smatch count_match;
    std::smatch dispatch_match;
    std::smatch type_match;
    if (std::regex_search(html, current_match, current_pattern) &&
        std::regex_search(html, count_match, page_count_pattern) &&
        std::regex_search(html, dispatch_match, dispatch_pattern) &&
        std::regex_search(html, type_match, type_pattern))
    {
        const int current = std::stoi(current_match[1].str());
        const int count = std::stoi(count_match[1].str());
        if (current < count)
        {
            const auto query_pos = page_url.find('?');
            const std::string action = query_pos == std::string::npos
                ? page_url : page_url.substr(0, query_pos);
            return normalize_url(action + "?dispatch=" + dispatch_match[1].str() +
                "&ntype_id=" + type_match[1].str() + "&pageNo=" +
                std::to_string(current + 1));
        }
    }
    return "";
}

int json_search(std::string& html, News& oneNews, std::set<std::string>& links)
{
    links.clear();
    if (parse_embedded_news_json(html, oneNews))
    {
        links.clear();
        return 0;
    }
    links = selected_link_set(html, oneNews.url);
    return -1;
}

bool generic_parse(const std::string& html, const std::string& page_url,
                   News& out_news, std::set<std::string>& out_links)
{
    out_news = News{};
    out_news.url = normalize_url(page_url);
    out_links.clear();

    // The international-office notice template can contain hundreds of thousands of
    // deeply nested formatting tags.  Running the whole document through HTML Tidy may
    // take minutes, so read its explicit article boundaries directly and cap the fragment.
    if (page_url.find("global.scu.edu.cn/?notice/_/") != std::string::npos &&
        html.find("news-article-block") != std::string::npos)
    {
        static const std::regex title_pattern(
            R"REGEX(<div[^>]*class\s*=\s*["'][^"']*news-head[^"']*["'][^>]*>[\s\S]*?<h1[^>]*>([\s\S]*?)</h1\s*>)REGEX",
            std::regex::icase);
        static const std::regex body_start_pattern(
            R"REGEX(<div[^>]*class\s*=\s*["'][^"']*news-content[^"']*["'][^>]*>)REGEX",
            std::regex::icase);
        static const std::regex image_pattern(
            R"REGEX(<img\b[^>]*(?:src|data-src|data-original)\s*=\s*["']([^"']+)["'])REGEX",
            std::regex::icase);
        static const std::regex script_pattern(
            R"REGEX(<(script|style)\b[^>]*>[\s\S]*?</\1\s*>)REGEX", std::regex::icase);
        static const std::regex tag_pattern(R"(<[^>]+>)");
        auto clean = [&](std::string value) {
            const std::vector<std::pair<std::string, std::string>> entities = {
                {"&nbsp;", " "}, {"&amp;", "&"}, {"&quot;", "\""},
                {"&#39;", "'"}, {"&lt;", "<"}, {"&gt;", ">"}
            };
            for (const auto& entity : entities)
            {
                std::size_t pos = 0;
                while ((pos = value.find(entity.first, pos)) != std::string::npos)
                    value.replace(pos, entity.first.size(), entity.second);
            }
            return collapse_text(std::regex_replace(value, tag_pattern, " "));
        };

        std::smatch title_match;
        std::smatch body_match;
        if (std::regex_search(html, title_match, title_pattern) &&
            std::regex_search(html, body_match, body_start_pattern))
        {
            out_news.title = clean(title_match[1].str());
            const std::size_t begin = static_cast<std::size_t>(body_match.position() + body_match.length());
            std::size_t end = html.find("<div class=\"right-column", begin);
            if (end == std::string::npos) end = (std::min)(html.size(), begin + 200000);
            // A bounded prefix is enough for the complete semantic text and prevents a
            // pathological formatting-heavy page from monopolising a seed run.
            end = (std::min)(end, begin + 200000);
            std::string fragment = html.substr(begin, end - begin);

            std::smatch image_match;
            if (std::regex_search(fragment, image_match, image_pattern))
            {
                const std::string candidate = resolve_url(page_url, image_match[1].str());
                const std::string marker = lower(candidate);
                if (!contains_any(marker, {"filetypeimages", "icon_", "/logo", "qrcode", "qr-code"}))
                    out_news.image = candidate;
            }
            fragment = std::regex_replace(fragment, script_pattern, " ");
            out_news.content = clean(fragment);
            if (out_news.content.size() > 8000) out_news.content = utf8_prefix(out_news.content, 8000);
            if (out_news.title.size() >= 4 &&
                (out_news.content.size() >= 80 || !out_news.image.empty()))
            {
                static const std::regex date_pattern(R"((20[0-9]{2}[-/.][0-9]{1,2}[-/.][0-9]{1,2}))");
                std::smatch date_match;
                if (std::regex_search(html, date_match, date_pattern)) out_news.time = date_match[1].str();
                out_news.abstract = make_abstract(out_news.content);
                out_news.source = extract_domain(page_url);
                return true;
            }
        }
        // A known detail template that fails its explicit boundaries is not sent to Tidy.
        return false;
    }

    // Union detail pages often contain malformed Microsoft Office tags.  Parse the stable
    // info-box/content-box boundary directly so libtidy never sees those native-crash inputs.
    if (page_url.find("gh.scu.edu.cn/front/news.do") != std::string::npos &&
        page_url.find("dispatch=showProNews") != std::string::npos &&
        html.find("class=\"info-box\"") != std::string::npos)
    {
        static const std::regex title_pattern(
            R"REGEX(<div[^>]*class\s*=\s*["'][^"']*info-box[^"']*["'][^>]*>[\s\S]*?<div[^>]*class\s*=\s*["'][^"']*title[^"']*["'][^>]*>([\s\S]*?)</div\s*>)REGEX",
            std::regex::icase);
        static const std::regex body_start_pattern(
            R"REGEX(<div[^>]*class\s*=\s*["'][^"']*content-box[^"']*["'][^>]*>)REGEX",
            std::regex::icase);
        static const std::regex image_pattern(
            R"REGEX(<img\b[^>]*(?:src|data-src|data-original)\s*=\s*["']([^"']+)["'])REGEX",
            std::regex::icase);
        static const std::regex script_pattern(
            R"REGEX(<(script|style)\b[^>]*>[\s\S]*?</\1\s*>)REGEX", std::regex::icase);
        static const std::regex tag_pattern(R"(<[^>]+>)");
        auto clean = [&](std::string value) {
            value = std::regex_replace(value, script_pattern, " ");
            value = std::regex_replace(value, tag_pattern, " ");
            const std::vector<std::pair<std::string, std::string>> entities = {
                {"&nbsp;", " "}, {"&amp;", "&"}, {"&quot;", "\""}, {"&#39;", "'"}
            };
            for (const auto& entity : entities)
            {
                std::size_t pos = 0;
                while ((pos = value.find(entity.first, pos)) != std::string::npos)
                    value.replace(pos, entity.first.size(), entity.second);
            }
            return collapse_text(value);
        };

        std::smatch title_match;
        const std::size_t info_marker = html.find("class=\"info-box\"");
        const std::size_t info_pos = html.rfind("<div", info_marker);
        std::smatch body_match;
        const std::string article_html = html.substr(info_pos);
        if (std::regex_search(article_html, title_match, title_pattern) &&
            std::regex_search(article_html, body_match, body_start_pattern))
        {
            out_news.title = clean(title_match[1].str());
            const std::size_t begin = info_pos + static_cast<std::size_t>(body_match.position() + body_match.length());
            std::size_t end = html.find("class=\"general-PCtitle1", begin);
            if (end == std::string::npos) end = (std::min)(html.size(), begin + 120000);
            std::string fragment = html.substr(begin, end - begin);
            std::smatch image_match;
            if (std::regex_search(fragment, image_match, image_pattern))
            {
                const std::string candidate = resolve_url(page_url, image_match[1].str());
                const std::string marker = lower(candidate);
                if (!contains_any(marker, {"filetypeimages", "icon_", "/logo", "qrcode", "qr-code"}))
                    out_news.image = candidate;
            }
            out_news.content = clean(fragment);
            if (out_news.content.size() > 8000) out_news.content = utf8_prefix(out_news.content, 8000);
            if (out_news.title.size() >= 4 &&
                (out_news.content.size() >= 80 || !out_news.image.empty()))
            {
                static const std::regex date_pattern(R"((20[0-9]{2}[-/.][0-9]{1,2}[-/.][0-9]{1,2}))");
                std::smatch date_match;
                if (std::regex_search(html, date_match, date_pattern)) out_news.time = date_match[1].str();
                out_news.abstract = make_abstract(out_news.content);
                out_news.source = extract_domain(page_url);
                return true;
            }
        }
        return false;
    }
    if (lower(page_url).find("gh.scu.edu.cn/front/news.do") != std::string::npos &&
        lower(page_url).find("dispatch=showpronews") != std::string::npos)
    {
        // An abnormal union detail may return a multi-megabyte shell without info-box.
        // It is not a valid article and must never fall through to native HTML repair.
        return false;
    }

    // West China Hospital of Stomatology exposes stable article_title/article_cont
    // containers.  Its full template is large enough that a whole-document Tidy pass costs
    // roughly ten seconds per item, while these boundaries can be parsed immediately.
    if (page_url.find("hxkq.org/Html/News/Articles/") != std::string::npos &&
        html.find("article_cont") != std::string::npos)
    {
        static const std::regex title_pattern(
            R"REGEX(<h1[^>]*class\s*=\s*["'][^"']*article_title[^"']*["'][^>]*>([\s\S]*?)</h1\s*>)REGEX",
            std::regex::icase);
        static const std::regex body_start_pattern(
            R"REGEX(<div[^>]*class\s*=\s*["'][^"']*article_cont[^"']*["'][^>]*>)REGEX",
            std::regex::icase);
        static const std::regex image_pattern(
            R"REGEX(<img\b[^>]*(?:src|data-src|data-original)\s*=\s*["']([^"']+)["'])REGEX",
            std::regex::icase);
        static const std::regex script_pattern(
            R"REGEX(<(script|style)\b[^>]*>[\s\S]*?</\1\s*>)REGEX", std::regex::icase);
        static const std::regex tag_pattern(R"(<[^>]+>)");
        auto clean = [&](std::string value) {
            const std::vector<std::pair<std::string, std::string>> entities = {
                {"&nbsp;", " "}, {"&amp;", "&"}, {"&quot;", "\""},
                {"&#39;", "'"}, {"&lt;", "<"}, {"&gt;", ">"}
            };
            for (const auto& entity : entities)
            {
                std::size_t pos = 0;
                while ((pos = value.find(entity.first, pos)) != std::string::npos)
                    value.replace(pos, entity.first.size(), entity.second);
            }
            return collapse_text(std::regex_replace(value, tag_pattern, " "));
        };

        std::smatch title_match;
        std::smatch body_match;
        if (std::regex_search(html, title_match, title_pattern) &&
            std::regex_search(html, body_match, body_start_pattern))
        {
            out_news.title = clean(title_match[1].str());
            const std::size_t begin = static_cast<std::size_t>(body_match.position() + body_match.length());
            std::size_t end = html.find("class=\"share", begin);
            if (end == std::string::npos) end = (std::min)(html.size(), begin + 120000);
            std::string fragment = html.substr(begin, end - begin);
            std::smatch image_match;
            if (std::regex_search(fragment, image_match, image_pattern))
            {
                const std::string candidate = resolve_url(page_url, image_match[1].str());
                const std::string marker = lower(candidate);
                if (!contains_any(marker, {"filetypeimages", "icon_", "/logo", "qrcode", "qr-code"}))
                    out_news.image = candidate;
            }
            fragment = std::regex_replace(fragment, script_pattern, " ");
            out_news.content = clean(fragment);
            if (out_news.content.size() > 8000) out_news.content = utf8_prefix(out_news.content, 8000);
            if (out_news.title.size() >= 4 &&
                (out_news.content.size() >= 80 || !out_news.image.empty()))
            {
                static const std::regex date_pattern(R"((20[0-9]{2}[-/.][0-9]{1,2}[-/.][0-9]{1,2}))");
                std::smatch date_match;
                if (std::regex_search(html, date_match, date_pattern)) out_news.time = date_match[1].str();
                out_news.abstract = make_abstract(out_news.content);
                out_news.source = extract_domain(page_url);
                return true;
            }
        }
        return false;
    }

    // The career-center template wraps a small article in a very large malformed page. Reading
    // its explicit title/content ids avoids an expensive full-page Tidy repair.
    if (html.find("news-content-tit") != std::string::npos && html.find("mycontent") != std::string::npos)
    {
        static const std::regex title_pattern(
            R"REGEX(<div[^>]*class\s*=\s*["'][^"']*news-content-tit[^"']*["'][^>]*>[\s\S]*?<p[^>]*>([\s\S]*?)</p\s*>)REGEX",
            std::regex::icase);
        static const std::regex body_pattern(
            R"REGEX(<div[^>]*id\s*=\s*["']mycontent["'][^>]*>([\s\S]*?)</div\s*>)REGEX",
            std::regex::icase);
        static const std::regex image_pattern(
            R"REGEX(<img\b[^>]*(?:src|data-src|data-original)\s*=\s*["']([^"']+)["'])REGEX",
            std::regex::icase);
        static const std::regex tag_pattern(R"(<[^>]+>)");
        auto clean = [&](std::string value) {
            const std::vector<std::pair<std::string, std::string>> entities = {
                {"&nbsp;", " "}, {"&amp;", "&"}, {"&quot;", "\""}, {"&#39;", "'"}
            };
            for (const auto& entity : entities)
            {
                std::size_t pos = 0;
                while ((pos = value.find(entity.first, pos)) != std::string::npos)
                    value.replace(pos, entity.first.size(), entity.second);
            }
            return collapse_text(std::regex_replace(value, tag_pattern, " "));
        };

        std::smatch title_match;
        std::smatch body_match;
        if (std::regex_search(html, title_match, title_pattern) &&
            std::regex_search(html, body_match, body_pattern))
        {
            out_news.title = clean(title_match[1].str());
            const std::string fragment = body_match[1].str();
            out_news.content = clean(fragment);
            std::smatch image_match;
            if (std::regex_search(fragment, image_match, image_pattern))
            {
                const std::string candidate = resolve_url(page_url, image_match[1].str());
                const std::string marker = lower(candidate);
                if (!contains_any(marker, {"filetypeimages", "icon_", "/logo", "qrcode", "qr-code"}))
                    out_news.image = candidate;
            }
            if (out_news.content.size() < 80 && !out_news.image.empty())
                out_news.content = "正文以图片形式发布，请查看 news.image 字段中的原始图片。";
            if (out_news.title.size() >= 4 &&
                (out_news.content.size() >= 80 || !out_news.image.empty()))
            {
                static const std::regex date_pattern(R"((20[0-9]{2}[-/.][0-9]{1,2}[-/.][0-9]{1,2}))");
                std::smatch date_match;
                if (std::regex_search(html, date_match, date_pattern)) out_news.time = date_match[1].str();
                out_news.abstract = make_abstract(out_news.content);
                out_news.source = extract_domain(page_url);
                return true;
            }
        }
        // This is the known template; malformed/empty content is rejected without invoking Tidy.
        return false;
    }
    if (page_url.find("jy.scu.edu.cn/index/index/newsdetail.html") != std::string::npos)
    {
        // Expired career-center details redirect to the huge portal homepage. It is not an
        // article and must not fall through to full-page repair.
        return false;
    }

    // Most SCU departmental sites use the VSB template. Parse its explicit content boundary
    // directly: this is safer than repairing the entire (often malformed) legacy document.
    if (html.find("vsb_content") != std::string::npos)
    {
        static const std::regex title_pattern(
            R"REGEX(<title[^>]*>([\s\S]*?)</title\s*>)REGEX", std::regex::icase);
        static const std::regex body_start_pattern(
            R"REGEX(<div[^>]*id\s*=\s*["']vsb_content["'][^>]*>)REGEX", std::regex::icase);
        static const std::regex script_pattern(
            R"REGEX(<(script|style)\b[^>]*>[\s\S]*?</\1\s*>)REGEX", std::regex::icase);
        static const std::regex tag_pattern(R"(<[^>]+>)");
        static const std::regex image_pattern(
            R"REGEX(<img\b[^>]*(?:src|data-src|data-original)\s*=\s*["']([^"']+)["'])REGEX",
            std::regex::icase);
        auto decode = [](std::string value) {
            const std::vector<std::pair<std::string, std::string>> entities = {
                {"&nbsp;", " "}, {"&amp;", "&"}, {"&quot;", "\""}, {"&#39;", "'"},
                {"&ldquo;", "“"}, {"&rdquo;", "”"}, {"&lt;", "<"}, {"&gt;", ">"}
            };
            for (const auto& entity : entities)
            {
                std::size_t pos = 0;
                while ((pos = value.find(entity.first, pos)) != std::string::npos)
                    value.replace(pos, entity.first.size(), entity.second);
            }
            return collapse_text(value);
        };

        std::smatch match;
        if (std::regex_search(html, match, title_pattern))
        {
            out_news.title = decode(std::regex_replace(match[1].str(), tag_pattern, " "));
            // VSB titles usually end in "-site name"; keep the article headline only.
            const auto suffix = out_news.title.rfind('-');
            if (suffix != std::string::npos && out_news.title.size() - suffix < 80)
                out_news.title = collapse_text(out_news.title.substr(0, suffix));
        }

        std::smatch body_match;
        if (std::regex_search(html, body_match, body_start_pattern))
        {
            const std::size_t begin = static_cast<std::size_t>(body_match.position() + body_match.length());
            std::size_t end = html.find("id=\"div_vote_id\"", begin);
            if (end == std::string::npos) end = html.find("id='div_vote_id'", begin);
            if (end != std::string::npos)
            {
                const std::size_t tag_begin = html.rfind("<div", end);
                if (tag_begin != std::string::npos && tag_begin >= begin) end = tag_begin;
            }
            if (end == std::string::npos) end = html.find("</form", begin);
            if (end == std::string::npos) end = (std::min)(html.size(), begin + 200000);
            std::string fragment = html.substr(begin, end - begin);

            std::smatch image_match;
            if (std::regex_search(fragment, image_match, image_pattern))
            {
                const std::string candidate = resolve_url(page_url, image_match[1].str());
                const std::string marker = lower(candidate);
                if (!contains_any(marker, {"filetypeimages", "icon_", "/logo", "qrcode", "qr-code"}))
                    out_news.image = candidate;
            }

            fragment = std::regex_replace(fragment, script_pattern, " ");
            const std::string text = decode(std::regex_replace(fragment, tag_pattern, " "));
            if (text.size() >= 80)
                out_news.content = utf8_prefix(text, 8000);
            else if (!out_news.image.empty())
                out_news.content = "正文以图片形式发布，请查看 news.image 字段中的原始图片。";
        }

        if (out_news.title.size() >= 4 &&
            (out_news.content.size() >= 80 || !out_news.image.empty()))
        {
            static const std::regex date_pattern(R"((20[0-9]{2}[-/.][0-9]{1,2}[-/.][0-9]{1,2}))");
            if (std::regex_search(html, match, date_pattern)) out_news.time = match[1].str();
            out_news.abstract = make_abstract(out_news.content);
            out_news.source = extract_domain(page_url);
            return true;
        }
    }

    // These VSB sites publish real details under /info/...; short .htm section shells such
    // as mse.scu.edu.cn/ggl.htm are list/navigation pages.  Never send those malformed
    // shells to Tidy: they are not articles and some can crash the native repair library.
    const bool guarded_vsb_site =
        page_url.find("life.scu.edu.cn/") != std::string::npos ||
        page_url.find("mse.scu.edu.cn/") != std::string::npos;
    if (guarded_vsb_site &&
        page_url.find("/info/") == std::string::npos)
    {
        out_links = selected_link_set(html, page_url);
        return false;
    }

    const std::string xhtml = tidy_html(html);
    if (xhtml.empty())
    {
        out_links = selected_link_set(html, page_url);
        return false;
    }
    pugi::xml_document doc;
    if (!doc.load_string(xhtml.c_str(), pugi::parse_full))
    {
        out_links = selected_link_set(html, page_url);
        return false;
    }

    // A few legacy pages use H1 for body paragraphs and keep the headline in
    // .info-box > .title, so prefer that explicit headline when present.
    auto take_explicit_title = [&](const char* xpath) {
        if (!out_news.title.empty()) return;
        for (const auto& item : doc.select_nodes(xpath))
        {
            const std::string title = node_text(item.node());
            if (title.size() >= 4 && title.size() <= 200)
            {
                out_news.title = title;
                return;
            }
        }
    };
    // Site templates often contain several H1 nodes.  Prefer selectors that are known to
    // denote the article headline, and only then fall back to a generic H1.
    take_explicit_title("//h1[contains(concat(' ',normalize-space(@class),' '),' article_title ')]");
    if (page_url.find("gh.scu.edu.cn/") != std::string::npos)
        take_explicit_title(
            "//*[contains(concat(' ',normalize-space(@class),' '),' info-box ')]"
            "//*[contains(concat(' ',normalize-space(@class),' '),' title ')]");
    take_explicit_title("//h1[contains(concat(' ',normalize-space(@class),' '),' Title ')]");
    take_explicit_title("//h1[contains(concat(' ',normalize-space(@class),' '),' title ')]");
    take_explicit_title(
        "//*[contains(concat(' ',normalize-space(@class),' '),' post-content-container ')]/h3[1]");
    take_explicit_title(
        "//*[contains(concat(' ',normalize-space(@class),' '),' news-content-tit ')]/p[1]");
    const auto h1_nodes = doc.select_nodes("//article//h1 | //main//h1 | //h1");
    for (const auto& item : h1_nodes)
    {
        if (!out_news.title.empty()) break;
        const std::string title = node_text(item.node());
        if (title.size() >= 4 && title.size() <= 200)
        {
            out_news.title = title;
            break;
        }
    }
    if (out_news.title.empty())
    {
        const auto meta = doc.select_node("//meta[translate(@property,'ABCDEFGHIJKLMNOPQRSTUVWXYZ','abcdefghijklmnopqrstuvwxyz')='og:title']");
        if (meta) out_news.title = collapse_text(meta.node().attribute("content").value());
    }
    if (out_news.title.empty())
    {
        const auto title_node = doc.select_node("//title");
        if (title_node) out_news.title = node_text(title_node.node());
    }

    std::string best_body;
    std::size_t best_score = 0;
    for (const auto& item : doc.select_nodes("//*"))
    {
        const pugi::xml_node container = item.node();
        if (!is_content_container(container)) continue;

        std::string body;
        std::size_t paragraphs = 0;
        for (const auto& paragraph : container.select_nodes(".//p | .//div | .//section"))
        {
            if (has_bad_marker(paragraph.node())) continue;
            const std::string text = node_text(paragraph.node());
            if (text.size() < 8) continue;
            body += text + "\n";
            ++paragraphs;
        }
        if (paragraphs == 0) body = node_text(container);
        if (body.size() < 80) continue;

        const std::size_t score = body.size() + paragraphs * 100;
        if (score > best_score)
        {
            best_score = score;
            best_body = body;
        }
    }

    // Preserve image-only notices (for example, a notice published as one long poster) in the
    // dedicated image column instead of treating image bytes or markup as article text.
    std::string main_image;
    const auto image_nodes = doc.select_nodes(
        "//*[@id='mycontent']//img | //*[@id='vsb_content']//img | "
        "//*[contains(concat(' ',normalize-space(@class),' '),' article_cont ')]//img | "
        "//*[contains(concat(' ',normalize-space(@class),' '),' v_news_content ')]//img");
    for (const auto& item : image_nodes)
    {
        const pugi::xml_node image = item.node();
        std::string raw = image.attribute("src").value();
        if (raw.empty()) raw = image.attribute("data-src").value();
        if (raw.empty()) raw = image.attribute("data-original").value();
        const std::string resolved = resolve_url(page_url, raw);
        const std::string marker = lower(resolved);
        if (!resolved.empty() &&
            !contains_any(marker, {"filetypeimages", "icon_", "/logo", "qrcode", "qr-code"}))
        {
            main_image = resolved;
            break;
        }
    }

    if ((best_body.size() < 80 && main_image.empty()) || out_news.title.size() < 4)
    {
        out_links = selected_link_set(html, page_url);
        return false;
    }

    out_news.content = best_body.size() >= 80
        ? utf8_prefix(best_body, 8000)
        : "正文以图片形式发布，请查看 news.image 字段中的原始图片。";
    out_news.image = main_image;
    out_news.abstract = make_abstract(out_news.content);
    out_news.source = extract_domain(page_url);
    out_links.clear();
    return true;
}

bool is_news_detail_url(const std::string& url)
{
    return score_news_link(url, "新闻") >= 5;
}

bool is_list_page_url(const std::string& url)
{
    const std::string lower_url = lower(url);
    return lower_url.find("/index") != std::string::npos ||
           lower_url.find("/list") != std::string::npos ||
           lower_url.find("/search") != std::string::npos ||
           lower_url.find("general/more") != std::string::npos;
}
