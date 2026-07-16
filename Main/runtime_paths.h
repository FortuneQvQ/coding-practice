#pragma once

#include <filesystem>

// 定位包含 config.json 和 templates 的运行根目录，供 main 与展示模块统一解析相对路径；
// 支持从项目目录、output 目录以及 VS 的 bin/Debug、bin/Release 目录启动。
std::filesystem::path find_runtime_root();
