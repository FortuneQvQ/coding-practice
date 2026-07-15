#include "crawler.h"

// ===== 日志 =====

void log_write(const std::string& level, const std::string& msg)
{
    time_t now = time(nullptr);
    char buf_now[25];
    strftime(buf_now, sizeof(buf_now), "%Y-%m-%d %H:%M:%S", localtime(&now));
    char buf_day[20];
    strftime(buf_day, sizeof(buf_day), "%Y-%m-%d", localtime(&now));
    std::string filename = std::string(buf_day) + ".txt";
    std::ofstream log(filename, std::ios::app | std::ios::binary);
    log << "[" << buf_now << "][" << level << "] " << msg << std::endl;
}

// ===== fetch_page：抓取网页到内存字符串（主接口） =====

static size_t write_to_string(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    size_t realsize = size * nmemb;
    std::string* str = static_cast<std::string*>(userdata);
    str->append(ptr, realsize);
    return realsize;
}

// Reuse cookies between list, pagination and detail requests during one crawler run.
static CURLSH* shared_cookies()
{
    static CURLSH* share = []() {
        CURLSH* value = curl_share_init();
        if (value) curl_share_setopt(value, CURLSHOPT_SHARE, CURL_LOCK_DATA_COOKIE);
        return value;
    }();
    return share;
}

static void configure_common_request(CURL* curl)
{
    curl_easy_setopt(curl, CURLOPT_USERAGENT,
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/126.0.0.0 Safari/537.36");
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_AUTOREFERER, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 7L);
    curl_easy_setopt(curl, CURLOPT_COOKIEFILE, "");
    // Do not attach a CURL share handle here. Some Windows debug builds of libcurl become
    // unstable after many sequential hosts share one cookie store. Each request remains isolated.
}

std::string fetch_page(const std::string& url)
{
    CURL* curl = curl_easy_init();
    if (!curl)
    {
        log_write("ERROR", "curl 初始化失败");
        return "";
    }

    std::string html;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_to_string);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &html);
    configure_common_request(curl);

    CURLcode code = curl_easy_perform(curl);

    if (code != CURLE_OK)
    {
        std::string err = curl_easy_strerror(code);
        log_write("ERROR", "抓取失败 [" + url + "]: " + err);
        std::cerr << "抓取失败 " << url << " : " << err << std::endl;
        curl_easy_cleanup(curl);
        return "";
    }

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    if (http_code != 200)
    {
        log_write("WARN", "HTTP " + std::to_string(http_code) + " " + url);
        std::cerr << "HTTP " << http_code << " " << url << std::endl;
        return "";
    }

    log_write("INFO", "抓取成功 [" + url + "] " + std::to_string(html.size()) + " bytes");
    return html;
}

std::string fetch_form(const std::string& url, const std::string& form_data)
{
    CURL* curl = curl_easy_init();
    if (!curl) return "";

    std::string response;
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded; charset=UTF-8");
    headers = curl_slist_append(headers, "X-Requested-With: XMLHttpRequest");
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, form_data.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(form_data.size()));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_to_string);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    configure_common_request(curl);

    const CURLcode code = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (code != CURLE_OK || http_code != 200)
    {
        const std::string reason = code != CURLE_OK ? curl_easy_strerror(code) : "HTTP " + std::to_string(http_code);
        log_write("WARN", "POST 抓取失败 [" + url + "]: " + reason);
        return "";
    }
    log_write("INFO", "POST 抓取成功 [" + url + "] " + std::to_string(response.size()) + " bytes");
    return response;
}

// ===== fetch_and_save：抓取网页到磁盘文件（调试用） =====

static size_t write_to_file(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    size_t realsize = size * nmemb;
    std::ofstream* file = static_cast<std::ofstream*>(userdata);
    file->write(ptr, realsize);
    return realsize;
}

bool fetch_and_save(const std::string& url, const std::string& filename)
{
    CURL* curl = curl_easy_init();
    log_write("INFO", "开始抓取：" + url);
    if (!curl)
    {
        log_write("ERROR", "curl初始化失败");
        return false;
    }

    std::ofstream file(filename, std::ios::binary | std::ios::trunc);
    if (!file)
    {
        log_write("ERROR", "打开文件失败：" + filename);
        curl_easy_cleanup(curl);
        return false;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_to_file);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &file);
    curl_easy_setopt(curl, CURLOPT_USERAGENT,
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);

    CURLcode code = curl_easy_perform(curl);
    file.close();

    if (code != CURLE_OK)
    {
        std::string err = curl_easy_strerror(code);
        log_write("ERROR", "抓取失败：" + err);
        curl_easy_cleanup(curl);
        return false;
    }

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    if (http_code != 200)
    {
        log_write("WARN", "HTTP " + std::to_string(http_code) + " " + url);
        return false;
    }

    log_write("INFO", "抓取成功 " + url);
    return true;
}

// ===== extract_domain：提取域名 =====

std::string extract_domain(const std::string& url)
{
    const std::string normalized = normalize_url(url);
    size_t start = normalized.find("://");
    if (start == std::string::npos) return "";
    start += 3;
    size_t end = normalized.find_first_of("/?", start);
    if (end == std::string::npos)
        return normalized.substr(start);
    return normalized.substr(start, end - start);
}

// ===== load_config：加载种子 URL =====

std::vector<std::string> load_config(const std::string& config_path)
{
    std::vector<std::string> all_urls;
    std::ifstream f(config_path);
    if (!f) {
        std::cerr << "[配置] 无法打开 " << config_path << std::endl;
        return all_urls;
    }

    try {
        json config = json::parse(f);
        for (auto& source : config["sources"]) {
            for (auto& u : source["url_list"]) {
                all_urls.push_back(u.get<std::string>());
            }
        }
        log_write("INFO", "从配置加载了 " + std::to_string(all_urls.size()) + " 个种子URL");
    }
    catch (const std::exception& e) {
        std::cerr << "[配置] JSON解析失败: " << e.what() << std::endl;
    }

    return all_urls;
}
