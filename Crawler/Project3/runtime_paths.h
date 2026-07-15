#pragma once

#include <filesystem>

// 定位包含 config.json 和 templates 的项目运行根目录。
// 支持从项目目录、output 目录以及 VS 的 x64/Debug、x64/Release 目录启动。
std::filesystem::path find_runtime_root();

