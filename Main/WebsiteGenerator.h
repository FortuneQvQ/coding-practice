#pragma once

// 使用 main 已打开的数据库生成 output 下的完整静态网站；
// 返回 false 表示模板、目录或关键输出文件生成失败，main 会据此返回错误码。
bool generateNewsWebsite();
