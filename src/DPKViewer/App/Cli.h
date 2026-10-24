// Cli.h - 命令行批处理模式（/list、/extract、/pack），与主窗口共用同一份 Core
#pragma once

#include <string>

namespace app {

// 解析命令行并执行；不是命令行模式（没有 /list、/extract、/pack）时返回 -1。
// 返回值为进程退出码：0 成功，1 失败。
int RunCli(const std::wstring& cmdLine);

}  // namespace app