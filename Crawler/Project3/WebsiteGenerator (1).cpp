#include "WebsiteGenerator.h"

#include "database.h"
#include "json.hpp"
#include "runtime_paths.h"

#include <curl/curl.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace
{
namespace fs = std::filesystem;

fs::path runtime_root()
{
    return find_runtime_root();
}

fs::path output_root()
{
    return runtime_root() / "output";
}

std::string read_text(const fs::path& file_name)
{
    std::ifstream file(file_name, std::ios::binary);
    if (!file)
    {
        std::cerr << "[website] cannot open template: " << file_name.string() << '\n';
        return {};
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

bool write_text(const fs::path& file_name, const std::string& text)
{
    std::ofstream file(file_name, std::ios::binary | std::ios::trunc);
    if (!file)
    {
        std::cerr << "[website] cannot write: " << file_name.string() << '\n';
        return false;
    }
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    return file.good();
}

void replace_all(std::string& text, const std::string& key, const std::string& value)
{
    std::size_t position = 0;
    while ((position = text.find(key, position)) != std::string::npos)
    {
        text.replace(position, key.size(), value);
        position += value.size();
    }
}

std::string html_escape(const std::string& value)
{
    std::string result;
    result.reserve(value.size());
    for (const char c : value)
    {
        switch (c)
        {
        case '&': result += "&amp;"; break;
        case '<': result += "&lt;"; break;
        case '>': result += "&gt;"; break;
        case '"': result += "&quot;"; break;
        case '\'': result += "&#39;"; break;
        default: result += c; break;
        }
    }
    return result;
}

std::string content_html(const std::string& content)
{
    std::string escaped = html_escape(content);
    replace_all(escaped, "\r\n", "<br>\n");
    replace_all(escaped, "\n", "<br>\n");
    return escaped;
}

bool starts_with_http(const std::string& value)
{
    return value.rfind("http://", 0) == 0 || value.rfind("https://", 0) == 0;
}

struct ImageBuffer
{
    std::vector<unsigned char> bytes;
    bool too_large = false;
};

std::size_t image_write_callback(char* data, std::size_t size, std::size_t count, void* user)
{
    constexpr std::size_t kMaximumImageBytes = 20 * 1024 * 1024;
    auto* buffer = static_cast<ImageBuffer*>(user);
    const std::size_t bytes = size * count;
    if (bytes > kMaximumImageBytes || buffer->bytes.size() > kMaximumImageBytes - bytes)
    {
        buffer->too_large = true;
        return 0;
    }
    buffer->bytes.insert(buffer->bytes.end(),
                         reinterpret_cast<unsigned char*>(data),
                         reinterpret_cast<unsigned char*>(data) + bytes);
    return bytes;
}

std::string image_extension(const std::vector<unsigned char>& bytes)
{
    if (bytes.size() >= 3 && bytes[0] == 0xFF && bytes[1] == 0xD8 && bytes[2] == 0xFF)
        return ".jpg";
    if (bytes.size() >= 8 && bytes[0] == 0x89 && bytes[1] == 'P' && bytes[2] == 'N' &&
        bytes[3] == 'G')
        return ".png";
    if (bytes.size() >= 6 && bytes[0] == 'G' && bytes[1] == 'I' && bytes[2] == 'F')
        return ".gif";
    if (bytes.size() >= 12 && bytes[0] == 'R' && bytes[1] == 'I' && bytes[2] == 'F' &&
        bytes[8] == 'W' && bytes[9] == 'E' && bytes[10] == 'B' && bytes[11] == 'P')
        return ".webp";
    return {};
}

// 返回详情页可直接使用的相对路径（../images/id.ext）。数据库仍保留原始 URL，
// 既方便追溯，也不会因为本地展示目录变化而污染抓取数据。
std::string prepare_local_image(const News& news)
{
    if (news.image.empty()) return {};
    const fs::path image_directory = output_root() / "images";

    // 已下载过的图片直接复用，避免每次运行重复消耗网站流量。
    for (const char* extension : {".jpg", ".png", ".gif", ".webp"})
    {
        const fs::path existing = image_directory / (std::to_string(news.id) + extension);
        if (fs::is_regular_file(existing)) return "../images/" + existing.filename().string();
    }

    if (!starts_with_http(news.image))
    {
        std::error_code error;
        fs::path source = fs::path(news.image);
        if (source.is_relative()) source = runtime_root() / source;
        if (!fs::is_regular_file(source)) return {};
        std::string extension = source.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (extension != ".jpg" && extension != ".jpeg" && extension != ".png" &&
            extension != ".gif" && extension != ".webp")
            return {};
        if (extension == ".jpeg") extension = ".jpg";
        const fs::path target = image_directory / (std::to_string(news.id) + extension);
        fs::copy_file(source, target, fs::copy_options::overwrite_existing, error);
        return error ? std::string{} : "../images/" + target.filename().string();
    }

    CURL* curl = curl_easy_init();
    if (curl == nullptr) return {};
    ImageBuffer buffer;
    curl_easy_setopt(curl, CURLOPT_URL, news.image.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, image_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 CampusNewsHub/1.0");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 7L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
    const CURLcode result = curl_easy_perform(curl);
    long response_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    curl_easy_cleanup(curl);

    const std::string extension = image_extension(buffer.bytes);
    if (result != CURLE_OK || response_code != 200 || buffer.too_large || extension.empty())
    {
        std::cerr << "[website] image download skipped: " << news.image << '\n';
        return {};
    }

    const fs::path target = image_directory / (std::to_string(news.id) + extension);
    std::ofstream file(target, std::ios::binary | std::ios::trunc);
    if (!file) return {};
    file.write(reinterpret_cast<const char*>(buffer.bytes.data()),
               static_cast<std::streamsize>(buffer.bytes.size()));
    return file.good() ? "../images/" + target.filename().string() : std::string{};
}

std::string news_cards(const std::vector<News>& news_list, std::size_t maximum)
{
    std::string block;
    std::size_t count = 0;
    for (const News& news : news_list)
    {
        if (count++ >= maximum) break;
        block += "<div class=\"news-card\">";
        block += "<div class=\"news-source\">" + html_escape(news.source) +
                 " · " + html_escape(news.topic) + "</div>";
        block += "<div class=\"news-title\">" + html_escape(news.title) + "</div>";
        block += "<div class=\"news-time\">发布时间：" + html_escape(news.time) + "</div>";
        block += "<div class=\"news-abstract\">" + html_escape(news.abstract) + "</div>";
        block += "<a class=\"detail-btn\" href=\"detail/" + std::to_string(news.id) +
                 ".html?from=index\">查看详情</a></div>";
    }
    return block;
}

bool generate_detail_page(const News& news, const std::string& template_text)
{
    std::string html = template_text;
    replace_all(html, "{{title}}", html_escape(news.title));
    replace_all(html, "{{time}}", html_escape(news.time));
    replace_all(html, "{{content}}", content_html(news.content));
    replace_all(html, "{{url}}", html_escape(news.url));
    replace_all(html, "{{source}}", html_escape(news.source + " · " + news.topic));
    replace_all(html, "{{abstract}}", html_escape(news.abstract));

    const std::string local_image = prepare_local_image(news);
    std::string image_block;
    if (!local_image.empty())
        image_block = "<img class=\"news-image\" src=\"" + html_escape(local_image) +
                      "\" alt=\"新闻图片\" loading=\"lazy\">";
    else if (starts_with_http(news.image))
        image_block = "<img class=\"news-image\" src=\"" + html_escape(news.image) +
                      "\" alt=\"新闻图片\" loading=\"lazy\" onerror=\"this.remove()\">";
    replace_all(html, "{{imageBlock}}", image_block);
    return write_text(output_root() / "detail" / (std::to_string(news.id) + ".html"), html);
}

bool copy_template(const char* name)
{
    const std::string html = read_text(runtime_root() / "templates" / name);
    return !html.empty() && write_text(output_root() / name, html);
}

bool generate_json(const std::vector<News>& news_list)
{
    nlohmann::ordered_json json_news = nlohmann::ordered_json::array();
    for (const News& news : news_list)
    {
        json_news.push_back({
            {"id", std::to_string(news.id)}, {"title", news.title}, {"time", news.time},
            {"content", news.content}, {"abstract", news.abstract}, {"url", news.url},
            {"source", news.source}, {"image", news.image}, {"topic", news.topic}
        });
    }
    const std::string json_text = json_news.dump(
        2, ' ', false, nlohmann::ordered_json::error_handler_t::replace);
    return write_text(output_root() / "json" / "news.json", json_text);
}

bool copy_script(const char* name)
{
    const std::string script = read_text(runtime_root() / "js" / name);
    return !script.empty() && write_text(output_root() / "js" / name, script);
}
}

bool generateNewsWebsite()
{
    std::error_code error;
    fs::create_directories(output_root() / "detail", error);
    fs::create_directories(output_root() / "images", error);
    fs::create_directories(output_root() / "json", error);
    fs::create_directories(output_root() / "js", error);
    if (error)
    {
        std::cerr << "[website] cannot create output directories: " << error.message() << '\n';
        return false;
    }

    const std::vector<News> news_list = database::getAllNews();
    const std::string detail_template = read_text(runtime_root() / "templates" / "news_detail.html");
    const std::string index_template = read_text(runtime_root() / "templates" / "index.html");
    if (detail_template.empty() || index_template.empty()) return false;

    std::size_t detail_success = 0;
    for (const News& news : news_list)
        if (generate_detail_page(news, detail_template)) ++detail_success;

    std::string index = index_template;
    replace_all(index, "{{news_list}}", news_cards(news_list, 3));
    const bool ok = write_text(output_root() / "index.html", index) &&
                    copy_template("category_index.html") &&
                    copy_template("category_result.html") &&
                    copy_template("search.html") &&
                    copy_script("search.js") &&
                    copy_script("category.js") &&
                    copy_script("back.js") &&
                    generate_json(news_list);

    std::cout << "[website] output=" << output_root().string()
              << " details=" << detail_success << '/' << news_list.size()
              << " status=" << (ok ? "ok" : "incomplete") << '\n';
    return ok && detail_success == news_list.size();
}

