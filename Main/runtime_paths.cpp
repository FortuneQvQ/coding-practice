#include "runtime_paths.h"

#include <windows.h>

#include <array>
#include <system_error>

namespace
{
// 同时存在配置文件和模板目录时才认定为运行根目录，避免误选普通输出目录。
bool is_runtime_root(const std::filesystem::path& path)
{
    return std::filesystem::is_regular_file(path / "config.json") &&
           std::filesystem::is_directory(path / "templates");
}

// 从给定目录逐级向上搜索统一项目根目录。
std::filesystem::path search_from(std::filesystem::path start)
{
    std::error_code error;
    start = std::filesystem::absolute(start, error);
    for (int depth = 0; depth < 8 && !start.empty(); ++depth)
    {
        if (is_runtime_root(start)) return start;
        const auto parent = start.parent_path();
        if (parent == start) break;
        start = parent;
    }
    return {};
}
}

// 先从当前工作目录搜索，再从可执行文件位置搜索；main 和展示模块共享该结果，
// 从而固定 config、数据库、模板和 output 的相对关系。
std::filesystem::path find_runtime_root()
{
    if (const auto from_cwd = search_from(std::filesystem::current_path()); !from_cwd.empty())
        return from_cwd;

    std::array<wchar_t, 32768> executable{};
    const DWORD size = GetModuleFileNameW(nullptr, executable.data(),
                                          static_cast<DWORD>(executable.size()));
    if (size > 0 && size < executable.size())
    {
        if (const auto from_exe = search_from(
                std::filesystem::path(executable.data()).parent_path()); !from_exe.empty())
            return from_exe;
    }
    return std::filesystem::current_path();
}
