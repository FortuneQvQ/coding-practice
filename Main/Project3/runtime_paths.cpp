#include "runtime_paths.h"

#include <windows.h>

#include <array>
#include <system_error>

namespace
{
bool is_runtime_root(const std::filesystem::path& path)
{
    return std::filesystem::is_regular_file(path / "config.json") &&
           std::filesystem::is_directory(path / "templates");
}

std::filesystem::path search_from(std::filesystem::path start)
{
    std::error_code error;
    start = std::filesystem::absolute(start, error);
    for (int depth = 0; depth < 8 && !start.empty(); ++depth)
    {
        if (is_runtime_root(start)) return start;
        // VS 默认输出在解决方案的 x64 目录，而源码项目是其 Project3 子目录。
        if (is_runtime_root(start / "Project3")) return start / "Project3";
        const auto parent = start.parent_path();
        if (parent == start) break;
        start = parent;
    }
    return {};
}
}

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

