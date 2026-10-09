// GitHubDesktop2Chinese.cpp: 定义应用程序的入口点。


#define _SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING
#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING   //消除 converter.to_bytes的警告
#define _CRT_SECURE_NO_WARNINGS                             //消除 sprintf的警告

// PAUSE: 等待确认(Windows 任意键 / POSIX 一行回车); 非交互 EOF 时立即返回, 管道运行不挂起
// 展开调用下方 WaitAnyKey()(避免 system("pause") 每次调用创建 shell 进程)
#ifdef _WIN32
#define PAUSE if(!no_pause) { spdlog::info("按任意键继续..."); WaitAnyKey(); }
#else
#define PAUSE if(!no_pause) { spdlog::info("按回车键继续..."); WaitAnyKey(); }
#endif

#include "GitHubDesktop2Chinese.h"
#include <string>
#include <vector>
#include <limits>
#include <filesystem>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <optional>
#include <cstdio>
#include <ctime>
#include <iostream>
#ifdef _WIN32
#include <conio.h>                  // WaitAnyKey: 交互控制台下读取任意按键
#endif

#include <regex>

#include "spdlog/spdlog.h"          // 日志式输出库
#include "nlohmann/json.hpp"        // JSON读取本地配置库
#ifdef _WIN32
#include "WinReg/WinReg.hpp"        // 注册表操作库(仅 Windows)
#endif

#include <CLI/CLI.hpp>              // 参数管理器:   https://github.com/CLIUtils/CLI11
#include "utils/utils.hpp"
#include "version/Version.hpp"

#ifdef _MSC_VER
#pragma comment(lib, "winhttp.lib")
#endif

// 不进行替换 以便调试
#define NO_REPLACE 0

versionparse::Version FileVer{0,0,0};


using json = nlohmann::json;

// 设置一个路径的全局变量  指向要修改JS的目录
fs::path Base;
fs::path LocalizationJSON;

bool no_pause;                                  // 程序在结束前是否暂停
bool only_read_from_remote;                     // 仅从远程url中读取本地化文件
bool rollback;                                  // 从备份中还原汉化前的文件

json localization = R"(
                        {
                            "main": [
                                ["",""]
                            ],
                            "main_dev": [
                                ["",""]
                            ],
                            "renderer": [
                                ["",""]
                            ],
                            "renderer_dev": [
                                ["",""]
                            ]
                        }
                    )"_json;
bool _debug_goto_devoptions;

bool _debug_error_check_mode_main = false;
bool _debug_error_check_mode_renderer = false;
bool _debug_invalid_check_mode = false;
bool _debug_no_replace_res = false;
bool _debug_translation_from_bak = false;		// 直接从备份文件中翻译到目标文件中
bool _debug_dev_replace = false;				// 开发模式替换
bool _debug_dev_setversion = false;				// 开发模式可以指定当前版本

std::optional<std::string> formatTime(std::string time_str);

// 清屏: 跨平台 ANSI 转义(入口已为控制台启用 VT 处理, 不再创建 shell 进程)
inline void ClearScreen() {
    std::cout << "\033[2J\033[H" << std::flush;
}

// 等待确认: Windows 交互控制台读取任意按键; 非交互(重定向/无控制台)退化为等待一行(EOF 立即返回)
inline void WaitAnyKey() {
#ifdef _WIN32
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;
    if(hIn != INVALID_HANDLE_VALUE && hIn != nullptr && GetConsoleMode(hIn, &mode)) {
        (void)_getch();
        return;
    }
#endif
    std::cin.clear();
    std::cin.ignore((std::numeric_limits<std::streamsize>::max)(), '\n');
}

// 转义 std::regex_replace format 串中的 $ (否则 $&/$`/$n 会被展开或吞掉): 用于把捕获文本安全回填到模板
inline auto EscapeDollar(const std::string& s) -> std::string {
    std::string out;
    out.reserve(s.size());
    for(char c : s) {
        if(c == '$') out += "$$";
        else out += c;
    }
    return out;
}

#ifdef _WIN32
BOOL WINAPI ConsoleHandler(DWORD dwCtrlType) {
    // 仅拦截窗口关闭事件
    if(dwCtrlType == CTRL_CLOSE_EVENT) {
        // 弹出提示对话框（使用 Unicode 字符串）
        // MB_OK 只会返回 IDOK, 展示后返回 TRUE 交给系统完成关闭(约五秒后进程结束)
        MessageBoxW(NULL,
                    L"程序中止， 汉化可能失败， 请重新运行后按照程序的流程走，汉化完成后会自动关闭，不建议手动关闭窗口\n开发者:“按任意键继续”是继续的意思，不是已完成的意思",
                    L"汉化程序已关闭，本提示五秒后自动关闭",
                    MB_OK | MB_ICONQUESTION);
        return TRUE;
    }
    // 其他事件（Ctrl+C、Ctrl+Break、关机等）不处理，让系统默认执行
    return FALSE;
}
#endif // _WIN32


// ── 单文件处理(汉化替换 / 失效检测): main.js 与 renderer.js 共用的唯一实现(2026-10 审计 M2 去重) ──
// 返回 0 成功; 返回 1 中止(错误已记录, 异常路径已从备份恢复, 调用方负责 PAUSE 与退出码)
// invalid_count: 失效检测计数累计(用于 --invalidcheck 退出码)
// fileTag: "main.js"/"renderer.js"; logTag: "main"/"renderer"; baseKey: 已按 dev 模式选择的映射数组键
static int ProcessJsFile(const char* fileTag, const char* logTag, const char* baseKey,
                         bool errorCheckMode, int& invalid_count) {
    const fs::path target = Base / fileTag;
    const fs::path bak = Base / (std::string(fileTag) + ".bak");

    spdlog::info("正在处理{}文件,{}请勿关闭...{}", fileTag, "\033[33m", "\033[0m");
    int out = 0;
    // 如果"从备份文件中汉化"选项打开 则判断备份文件是否存在,以便尝试从备份文件中读取
    std::string js_str = ((_debug_translation_from_bak || _debug_invalid_check_mode) && fs::exists(bak))
        ? utils::ReadFile(bak.string())
        : utils::ReadFile(target.string());
    if(js_str.empty()) {
        spdlog::error("读取 {} 失败或内容为空, 已中止以免覆盖源文件; 若文件损坏可从 {}.bak 恢复", fileTag, fileTag);
        return 1;
    }
    bool process_failed = false;
    try {
        for(auto& item : localization[baseKey].items())
        {
#if NO_REPLACE
            continue;
#endif // NO_REPLACE
            // E2: 条目前置校验, 畸形条目跳过而不是抛异常中断剩余替换
            if(!item.value().is_array() || item.value().empty() || !item.value()[0].is_string()) {
                spdlog::warn("[{}] 跳过格式无效的条目 {}", logTag, item.key());
                continue;
            }
            std::string rege = item.value()[0].get<std::string>();
            if(rege.empty() || rege == "\"\"") {
                continue;
            }
            std::regex pattern(rege, std::regex::optimize);

            // 开发者选项 失效检测
            if(_debug_invalid_check_mode) {
                bool found = std::regex_search(js_str, pattern);
                if(!found) {
                    spdlog::warn("[{}] 检测到失效项: {}", logTag, rege);
                    invalid_count++;
                }
                if(item.value().size() >= 3) {
                    std::regex pattern3(item.value()[2].get<std::string>(), std::regex::optimize);
                    found = std::regex_search(js_str, pattern3);
                    if(!found) {
                        spdlog::warn("[{}] 检测到失效项: {}", logTag, item.value()[2].get<std::string>());
                        invalid_count++;
                    }
                }
                continue;
            }
            if(item.value().size() < 2 || !item.value()[1].is_string()) {
                spdlog::warn("[{}] 跳过缺少替换文本的条目 {}", logTag, item.key());
                continue;
            }
            if(item.value().size() >= 3) {
                // 在全文中查找数组第三项(pattern3), 用首个匹配的捕获组回填替换文本中的 #{} 占位符
                std::string regex_str = item.value()[2].is_string() ? item.value()[2].get<std::string>() : std::string();
                std::regex pattern3(regex_str, std::regex::optimize);
                std::sregex_iterator it = std::sregex_iterator(js_str.begin(), js_str.end(), pattern3);
                if(it != std::sregex_iterator()) {
                    const std::smatch& match = *it;
                    for(size_t i = 1; i < match.size(); i++) {
                        if(!match[i].matched) continue;
                        // 逐字面查找 #{i} 并回填; 捕获文本先转义 $, 防止其作为 format 串时 $&/$n 被展开
                        const std::string ph = "#{" + std::to_string(i) + "}";
                        const std::string val = EscapeDollar(match[i].str());
                        std::string text = item.value()[1].get<std::string>();
                        size_t pos = 0;
                        while((pos = text.find(ph, pos)) != std::string::npos) {
                            text.replace(pos, ph.size(), val);
                            pos += val.size();
                        }
                        item.value()[1] = text;
                    }
                    // 捕获组数不足导致的残留占位符若写入 JS 会造成语法破坏, 必须跳过该条
                    static const std::regex leftover_ph(R"(#\{\d+\})", std::regex::optimize);
                    if(std::regex_search(item.value()[1].get<std::string>(), leftover_ph)) {
                        spdlog::warn("[{}] 替换文本存在未回填的占位符, 此项将跳过: {}", logTag, regex_str);
                        continue;
                    }
                }
                else {
                    // 如果没有找到，则应该进行提示并跳过此项，以免进行错误的字符插入，造成程序无法打开
                    spdlog::warn("[{}] 出现一处失效项,此项将跳过: {}", logTag, regex_str);
                    continue;
                }
            }

            // 替换
            js_str = std::regex_replace(js_str, pattern, item.value()[1].get<std::string>());
            if(errorCheckMode) {
                // 控制台已设为 UTF-8, 直接输出原文, 不再经 GBK 转码造成乱码
                spdlog::info("[{}][out:{}]已经替换:{}->{}", logTag, out, rege, item.value()[1].get<std::string>());
                out--;
                if(out <= 0) {
                    if(!utils::WriteFile(target.string(), js_str)) {
                        spdlog::error("写入 {} 失败(原文件未改动), 已中止", fileTag);
                        return 1;
                    }
                    spdlog::info("已写入. 你希望下次替换多少条后写入:");
                    if(!(std::cin >> out)) {
                        // 输入流已结束(非交互运行): 置为极大值, 等价于剩余项全部替换后一次性写入
                        out = (std::numeric_limits<int>::max)();
                    }
                }
            }
        }

        // 循环select 如果是非测试替换
        if(!_debug_dev_replace) {
            // 循环select 列表
            for(auto& item_select : localization["select"].items()) {
                // 判断此项 是否是 对应js， 并且enable项是否开启 (value() 带默认值: 键缺失不抛异常)
                if(!item_select.value().is_object()) continue;
                const auto& sel = item_select.value();
                if(sel.value("replaceFile", std::string()) == fileTag && sel.value("enable", false)) {
                    // 拿到替换项，双层容器 (手动解析: 跳过缺列/非字符串的坏行, 防止 get 抛异常中断整个 select 段)
                    std::vector<std::vector<std::string>> replaces;
                    if(sel.contains("replace") && sel.at("replace").is_array()) {
                        for(const auto& row : sel.at("replace")) {
                            std::vector<std::string> vrow;
                            bool bad = !row.is_array() || row.size() < 2;
                            if(!bad) {
                                for(const auto& e : row) {
                                    if(!e.is_string()) { bad = true; break; }
                                    vrow.push_back(e.get<std::string>());
                                }
                            }
                            if(bad || vrow.size() < 2) {
                                spdlog::warn("[select {}] 跳过格式无效的替换行: {}", logTag, item_select.key());
                                continue;
                            }
                            replaces.push_back(std::move(vrow));
                        }
                    }
                    if(_debug_invalid_check_mode) { // 开启了失效项检测
                        // 遍历循环外层替换项
                        for(auto& v_item : replaces) {
                            if(v_item.size() < 2) continue; // F2 防御: 解析已过滤, 此处兜底防越界
                            // 如果此替换字符串第一个是空字符串，如果是空 则跳出此次循环
                            std::string rege = v_item[0];
                            if(rege.empty() || rege == "\"\"") {
                                continue;
                            }
                            std::regex pattern(rege, std::regex::optimize);
                            // 搜索第一项 是否存在
                            if(!std::regex_search(js_str, pattern)) {
                                spdlog::warn("[select {}] 检测到失效项: {}", logTag, rege.c_str());
                                invalid_count++;
                            }
                            //如果二层数组字符串有三项
                            if(v_item.size() >= 3) {
                                std::regex pattern3(v_item[2], std::regex::optimize);
                                // 搜索第三项是否存在
                                if(!std::regex_search(js_str, pattern3)) {
                                    spdlog::warn("[select {} 3] 检测到失效项: {}", logTag, v_item[2].c_str());
                                    invalid_count++;
                                }
                            }
                        }
                    }
                    else {  // 正常替换
                        // 询问提示 输出json中的输出提示字符串
                        spdlog::info(">>>>>> {}", sel.value("tooltip", std::string()));
                        // 读取用户输入
                        if(utils::ReadUserInput_bool({ "n","y" }, 1)) {
                            // 循环两层数组的外层数组
                            for(auto& v_item : replaces) {
                                if(v_item.size() < 2) continue; // F2 防御: 解析已过滤, 此处兜底防越界
                                // 如果此替换字符串第一个是空字符串，如果是空 则跳出此次循环
                                std::string rege = v_item[0];
                                if(rege.empty() || rege == "\"\"") {
                                    continue;
                                }
                                std::regex pattern(rege, std::regex::optimize);
                                // 如果有第三个字符串
                                if(v_item.size() >= 3) {
                                    // 预备用第三个字符串查找关键字 用来替换第二个字符串
                                    std::regex pattern3(v_item[2], std::regex::optimize);
                                    std::sregex_iterator it = std::sregex_iterator(js_str.begin(), js_str.end(), pattern3);
                                    if(it != std::sregex_iterator()) {
                                        const std::smatch& match = *it;
                                        for(size_t i = 1; i < match.size(); i++) {
                                            if(!match[i].matched) continue;
                                            // 逐字面回填; 捕获文本转义 $ 防止 format 特殊序列展开
                                            const std::string ph = "#{" + std::to_string(i) + "}";
                                            const std::string val = EscapeDollar(match[i].str());
                                            size_t pos = 0;
                                            while((pos = v_item[1].find(ph, pos)) != std::string::npos) {
                                                v_item[1].replace(pos, ph.size(), val);
                                                pos += val.size();
                                            }
                                        }
                                        static const std::regex leftover_ph(R"(#\{\d+\})", std::regex::optimize);
                                        if(std::regex_search(v_item[1], leftover_ph)) {
                                            spdlog::warn("[select {}] 替换文本存在未回填的占位符, 此项将跳过: {}", logTag, v_item[2]);
                                            continue;
                                        }
                                    }
                                    else {
                                        // 如果没有找到，则应该进行提示并跳过此项，以免进行错误的字符插入，造成程序无法打开
                                        spdlog::warn("[select {} 3] 出现一处失效项,此项将跳过: {}", logTag, v_item[2].c_str());
                                        continue;
                                    }
                                }
                                // 最终替换
                                js_str = std::regex_replace(js_str, pattern, v_item[1]);
                            }
                        }
                    }
                }
            }
        }
    }
    catch(const std::regex_error& re) {
        spdlog::error("处理{}时匹配正则出现错误 code:{}, message:{}", fileTag, re.code(), re.what());
        process_failed = true;
    }
    catch(const std::exception& e) {
        // 覆盖 runtime_error 及 json 类型异常等, 避免未捕获导致进程终止
        spdlog::error("处理{}时出现错误: {}", fileTag, e.what());
        process_failed = true;
    }

    if(process_failed) {
        // E1: 异常时不写入半成品, 从备份恢复目标文件, 与 README"汉化异常后从备份恢复"一致
        spdlog::error("{} 处理异常, 已中止且不写入部分汉化结果", fileTag);
        if(!_debug_invalid_check_mode && !_debug_no_replace_res && fs::exists(bak)) {
            std::error_code ec;
            fs::copy_file(bak, target, fs::copy_options::overwrite_existing, ec);
            if(ec) {
                spdlog::error("从备份恢复 {} 失败: {}", fileTag, ec.message());
            }
            else {
                spdlog::info("{} 已从备份恢复", fileTag);
            }
        }
        return 1;
    }

    if(!_debug_invalid_check_mode && !_debug_no_replace_res) {
        // 写入
        if(!utils::WriteFile(target.string(), js_str)) {
            spdlog::error("写入 {} 失败(原文件未改动), 已中止", fileTag);
            return 1;
        }
    }
    spdlog::info("{} 文件汉化结束.", fileTag);
    return 0;
}

#ifdef _WIN32
// wmain: 使用宽字符命令行, 避免非UTF-8代码页(如中文GBK)下参数在CLI11内部转换时崩溃
int wmain(int argc, wchar_t* wargv[])
{
    // 设置控制台的输入 输出编码：
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if(hOut != INVALID_HANDLE_VALUE) {
        DWORD mode = 0;
        GetConsoleMode(hOut, &mode);
        SetConsoleMode(hOut, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }

    // 自身可执行文件路径(宽字符, 非ASCII安装目录下无损)
    fs::path self_path(wargv[0]);
    // 宽字符命令行 -> UTF-8 供CLI11解析; 转换完成前不修改wargv, 转换后字符串不再变动以保证char*稳定
    std::vector<std::string> utf8_args;
    std::vector<char*> argv;
    utf8_args.reserve(argc);
    argv.reserve(argc);
    for(int i = 0; i < argc; i++) {
        utf8_args.push_back(utils::to_byte_string(wargv[i]));
    }
    for(auto& arg : utf8_args) {
        argv.push_back(arg.data());
    }
#else
// POSIX 入口: argv 本身即 UTF-8, 无需转换
int main(int argc, char* argv[])
{
    fs::path self_path(argv[0]);
#endif // _WIN32

    FileVer = versionparse::Version(FILEVERSION);
    // 设置控制台打印日志输出等级
    if(FileVer.status == FileVer.Dev) {
        spdlog::set_level(spdlog::level::debug);
    }
    // 注册命令行
    {
        CLI::App app{ "汉化GitHub Desktop管理、替换资源" };
        //app.require_subcommand(1);
        // 子命令 开发者选项
        auto dev_cmd = app.add_subcommand("dev", "开发者选项");
        dev_cmd->add_flag("-d,--dev", _debug_goto_devoptions,                       "进入开发者选项调整功能(可在开启程序时按住shift直接进入)");
        dev_cmd->add_flag("--mainerrorcheck", _debug_error_check_mode_main,         "[错误检查模式]对main.js以错误检查模式进行排查");
        dev_cmd->add_flag("--rendererrorcheck", _debug_error_check_mode_renderer,   "[错误检查模式]对renderer.js以错误检查模式进行排查");
        dev_cmd->add_flag("--invalidcheck", _debug_invalid_check_mode,              "[检测失效项]对本地化文件中的失效替换项进行检测");
        dev_cmd->add_flag("--noreplaceres", _debug_no_replace_res,                  "[资源不替换]开启后不会对资源进行替换,但不会阻止[错误检查模式]");
        dev_cmd->add_flag("--translationfrombak", _debug_translation_from_bak,      "[从备份文件中读取替换]优先从备份文件中读取js文件内容进行替换,开启[检测失效项]时建议开启此项");
        dev_cmd->add_flag("--devreplace", _debug_dev_replace,                       "[开发模式替换]仅替换指定映射以节约汉化时间(会影响其他项)");
        if(FileVer.major == 0 && FileVer.minor == 0 && FileVer.revision == 0) {
            dev_cmd->add_flag("--devsetver", _debug_dev_setversion,                "[指定当前程序版本]输入一个版本信息可以指定当前程序版本");
        }

        app.add_flag("--nopause", no_pause,                         "程序在结束前不再暂停等待");
        app.add_option("-g,--githubdesktoppath", Base,              "指定GitHubDesktop要汉化的资源所在目录(js所在目录)");
        app.add_option("-j,--json", LocalizationJSON,               "指定本地化JSON文件的本地路径");
        app.add_flag("-r,--onlyfromremote", only_read_from_remote,  "仅从远程url中读取本地化文件");
        app.add_flag("--rollback", rollback,                        "从备份文件中还原汉化前的文件");
        
        app.callback([&]() {
            // 手动指定了本地化文件目录
            if (!LocalizationJSON.string().empty()) {
                if (!LocalizationJSON.string().ends_with(".json")) {
                    throw CLI::ValidationError("(-j,--json) 指定的本地化文件路径必须以.json结尾");
                }
                if (!fs::exists(LocalizationJSON)) {
                    std::error_code ec;
                    if(LocalizationJSON.has_parent_path()) {
                        fs::create_directories(LocalizationJSON.parent_path(), ec);
                    }
                    std::ofstream io(LocalizationJSON);
                    io << std::setw(4) << localization << std::endl;
                    const bool ok = io.good();
                    io.close();
                    if(!ok || !fs::exists(LocalizationJSON)) {
                        // 权限不足/父目录不可写时不能谎报"已创建"(随后 CLI11 以 105 退出)
                        throw CLI::ValidationError("(-j,--json) 指定位置无法创建本地化文件(权限或路径问题)");
                    }
                    throw CLI::ValidationError("(-j,--json) 指定的本地化文件不存在,已在指定位置创建");
                }
            }
            // 手动指定了GitHubDesktop资源文件目录
            if (!Base.string().empty()) {
                if (!fs::exists(Base)) {
                    throw CLI::ValidationError("(-g,--githubdesktoppath) 指定的资源文件目录不存在");
                }
                if (!fs::exists(Base / "index.html")) {
                    throw CLI::ValidationError("(-g,--githubdesktoppath) 指定的资源文件目录无效,该目录下应该是存放main.js和renderer.js文件的");
                }
            }
        });


        try {
#ifdef _WIN32
            CLI11_PARSE(app, argc, argv.data());   // Windows: argv 为 std::vector<char*>
#else
            CLI11_PARSE(app, argc, argv);          // POSIX: argv 本身即 char**
#endif
        }
        catch(const std::exception& e) {
            // CLI11 内部仅捕获其自身异常, 命令行含非法编码等仍可能抛出其他异常, 此处兜底避免进程直接崩溃
            spdlog::error("命令行参数解析失败: {}", e.what());
            return 2;
        }
        catch(...) {
            spdlog::error("命令行参数解析失败: 发生未知异常");
            return 3;
        }
    }
    if(
#ifdef _WIN32
       // 无消息循环的控制台程序中 GetKeyState 返回的是过期状态, 用 GetAsyncKeyState 查询当前物理键状态
       (GetAsyncKeyState(VK_SHIFT) & 0x8000) ||
#endif
       _debug_goto_devoptions) {
        // 如果Shift按下, 则进入开发者选项
#ifdef _WIN32
        SetConsoleTitleW(L"开发者模式");
#endif
        spdlog::info("您已进入开发者模式");
        DeveloperOptions();
    }

    if(_debug_dev_setversion) {
        for(;;) {
            spdlog::info("请输入版本 如 1.2.3 (exit强制跳出):");
            std::string instr;
            if(!(std::cin >> instr)) {
                // 输入流已结束(管道/重定向运行)或读取失败, 无法继续交互, 跳出避免死循环
                spdlog::error("无法读取输入(输入流已结束), 退出版本设置");
                break;
            }
            if(instr == "exit") {
                break;
            }
            auto ver = versionparse::Version(instr.c_str());
            if(!ver) {
                spdlog::error("你输入的版本无效");
            }
            else {
                FileVer = ver;
                ClearScreen();
                break;
            }
        }
    }
    
#ifdef _WIN32
    if(!SetConsoleCtrlHandler(ConsoleHandler, TRUE)) {
        spdlog::error("注册关闭二次确认弹窗处理失败！");
    }
#endif

    // 开发者声明
    spdlog::info("开发者：Tupig（原作 CNGEGE，项目始于 2024/04/13）");
    spdlog::info("按程序提示流程走，完成后自动会退出，{}请勿手动关闭{}程序，手动关闭可能导致汉化失败", "\033[1;33m", "\033[0m");

    std::pair<std::string, int> proxy = { "", 0 };
    {
        auto p1 = utils::get_proxy_env();
        if(p1) {
            proxy = *p1;
        }
        else {
            auto p2 = utils::GetSystemProxySettings();
            if(p2) {
                proxy = *p2;
            }
        }
    }
    if(proxy.second) {
        spdlog::info("检测到已开启代理: {}:{}", proxy.first, proxy.second);
    }


    // 打印构建平台与版本（仅支持 x64）
    std::string arch_str("x64");

    if(FileVer) {
        spdlog::info("程序架构：- {}  版本: - {}", arch_str,  FileVer.toString(true));
    }
    else {
        spdlog::warn("程序架构：- {}  程序版本解析失败... at {}", arch_str, FILEVERSION);
    }
    


#ifdef _WIN32
    SetConsoleTitle(FileVer.toString(true).c_str());
#endif

    if (LocalizationJSON.empty()) {
        LocalizationJSON = "localization.json";
    }

    if(!fs::exists(LocalizationJSON)) {
        // 读取项目更新时间
        try {
            std::string repoinfo;
            if(utils::ReadHttpDataString("https://api.github.com", "/repos/Tupig/GitHubDesktop2Chinese", repoinfo, proxy)) {
                auto infojson = json::parse(repoinfo);
                std::optional<std::string> info = formatTime(infojson["updated_at"]);
                if(info) {
                    spdlog::info("仓库最新更新时间: {}{}{}", "\033[32m", *info, "\033[0m");
                }
            }
        }
        catch(const std::exception& e) {
            // 装饰性信息: 解析失败不中断主流程, 但保留原因便于诊断
            spdlog::debug("查询仓库更新时间失败: {}", e.what());
        }
        catch(...) {
            spdlog::debug("查询仓库更新时间出现未知异常");
        }
    }

    // 检查更新
    // https://api.github.com/repos/Tupig/GitHubDesktop2Chinese/releases/latest
    {
        if(FileVer.status != versionparse::Version::Dev) {
            spdlog::info("检查更新中..");
            try {
                std::string repoinfo;
                if(utils::ReadHttpDataString("https://api.github.com" , "/repos/Tupig/GitHubDesktop2Chinese/releases/latest", repoinfo, proxy)) {
                    auto infojson = json::parse(repoinfo);
                    auto tag_name = infojson["tag_name"].get<std::string>();
                    versionparse::Version remoteVer(tag_name.c_str());
                    if(!remoteVer) {
                        spdlog::warn("远程仓库中的版本号解析失败, ({})", tag_name);
                    }
                    else {
                        if(FileVer < remoteVer) {
                            spdlog::info("发现新版本: {}", remoteVer.toString());
                            // 在资产中定位可执行文件，避免依赖 assets[0] 的顺序
                            const json* asset = nullptr;
                            if(infojson.contains("assets") && infojson["assets"].is_array()) {
                                for(const auto& a : infojson["assets"]) {
                                    if(a.value("name", std::string()) == "GitHubDesktop2Chinese.exe") { asset = &a; break; }
                                }
                                if(!asset) {
                                    for(const auto& a : infojson["assets"]) {
                                        const std::string name = a.value("name", std::string());
                                        if(name.size() > 4 && name.ends_with(".exe")) { asset = &a; break; }
                                    }
                                }
                            }
                            if(!asset) {
                                spdlog::warn("未在最新 Release 中找到可执行文件资产, 跳过自动更新");
                            }
                            else {
                                std::string browser_download_url = asset->at("browser_download_url").get<std::string>();
                                // 下载使用浏览器下载地址(github.com 直链): 不消耗 API 配额,
                                // 避免共享 IP 触发 60次/小时匿名限流导致下载 403
                                std::string downlink = browser_download_url;
                                int download_count = asset->at("download_count").get<int>();
                                size_t max_size = asset->at("size").get<int64_t>();
                                // 发布资产声明的 SHA256 摘要(形如 sha256:<hex>), 用于下载后完整性校验
                                std::string asset_digest = asset->value("digest", std::string());
                                spdlog::info("下载链接({}次下载): {}", download_count, browser_download_url);
                                spdlog::info("是否自动更新:");
                                bool autoupdate = utils::ReadUserInput_bool({ "n", "y" }, 0);
                                if(autoupdate) {
                                    // 下载地址形如:
                                    //https://github.com/Tupig/GitHubDesktop2Chinese/releases/download/v1.0.14/GitHubDesktop2Chinese.exe
                                    std::regex url_regex(R"(^((?:https?://)[^/]+)(/.*)?$)", std::regex::optimize);
                                    std::smatch matches;
                                    if(std::regex_match(downlink, matches, url_regex)) {
                                        auto result = utils::UpdateProgram(matches[1].str(), matches[2].str(), self_path, max_size, proxy, asset_digest);
                                        if(!result) {
                                            spdlog::error("失败 更新过程出现异常");
                                        }
                                        else {
                                            // 立马退出 等待替换
                                            return 0;
                                        }
                                    }
                                    else {
                                        spdlog::error("下载地址可能变更, 正则表达式无法捕获");
                                    }
                                }
                            }
                        }
                        else {
                            spdlog::info("当前版本已经是最新版..");
                        }
                    }
                }
                else {
                    spdlog::warn("远程信息读取失败..");
                }
            }
            catch(const std::exception& e) {
                spdlog::warn("检查更新时出现异常: {}", e.what());
            }
            catch(...) {
                spdlog::warn("检查更新时出现未知异常");
            }
        }
    }

    // ── 目录解析(前置于 json 加载): 回滚只需目录+备份, 不应被 json 损坏/断网阻断(F4) ──
    // Github Desktop 存在目录没有提前设置
    if (!fs::exists(Base) || !fs::exists(Base / "index.html")) {
#ifdef _WIN32
        //	检查注册表中是否存在GithubDesktop
        winreg::RegKey key;
        winreg::RegResult result = key.TryOpen(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\GitHubDesktop");
        if (!result)
        {
            spdlog::warn("注册表中未发现GitHubDesktop相关条目, Reg ErrorMessage: {}" ,utils::to_byte_string(result.ErrorMessage()));
            spdlog::warn("你可能没有安装GithubDesktop，请先安装然后打开此程序或者手动指定main.js所在的文件夹目录");
            Base = utils::to_path(LoopGetBasePath());
            if(Base.empty()) {
                spdlog::error("未能获取资源目录, 请使用 -g 参数手动指定");
                PAUSE
                return 1;
            }
        }
        else {
            try
            {
                // 首先要从注册表中拿到GithubDesktop相关信息
                // 任务就是将Base 中写入路径
                std::wstring ver = key.GetStringValue(L"DisplayVersion");
                std::wstring path = key.GetStringValue(L"InstallLocation");
                Base = path + L"\\" + L"app-" + ver + L"\\resources\\app";
                std::string desktop_local_ver_str = utils::to_byte_string(ver);


                spdlog::info("正在读取GitHubDesktop最新正式版...");
                std::string httpjson_str;
                // 使用 GitHub Releases API: releases/latest 仅返回最新非预发布(正式版),
                // 避免把 beta 预览版(如 3.6.7-beta)误报为最新正式版
                if(utils::ReadHttpDataString("https://api.github.com", "/repos/desktop/desktop/releases/latest", httpjson_str, proxy)) {
                    // 远程信息仅用于提示, 解析失败不应中断主流程
                    try {
                        auto httpjson = json::parse(httpjson_str);
                        std::string v = httpjson.at("tag_name").get<std::string>();
                        if(v.rfind("release-", 0) == 0) {
                            v = v.substr(8); // 剥离 "release-" 前缀
                        }
                        versionparse::Version desktop_remote_ver(v.c_str());
                        if(desktop_remote_ver) {
                            versionparse::Version desktop_local_ver(desktop_local_ver_str.c_str());
                            spdlog::info("已读取到远程GitHubDesktop最新版:{} {}", v, desktop_remote_ver > desktop_local_ver ? "(\033[1;33m需更新\033[0m)" : "");
                        }
                        else {
                            spdlog::warn("远程GitHubDesktop版本号解析失败: {}", v);
                        }
                    }
                    catch(const std::exception& e) {
                        spdlog::warn("远程GitHubDesktop版本信息解析失败: {}", e.what());
                    }
                }else{
                    spdlog::warn("远程GitHubDesktop版本读取失败.");
                }

                spdlog::info("已从注册表中读取本地GitHubDesktop信息:");
                spdlog::info("本地GitHubDesktop版本: {}", desktop_local_ver_str);
                spdlog::info("安装目录: {}", utils::to_byte_string(path));
                spdlog::info("最后拼接完整目录: {}", utils::to_byte_string(Base.wstring()));


                if (!fs::exists(Base)) {
                    spdlog::warn("注册表最终获取到的目录不存在,请手动指定main.js所在的文件夹目录");
                    Base = utils::to_path(LoopGetBasePath());
                    if(Base.empty()) {
                        spdlog::error("未能获取资源目录, 请使用 -g 参数手动指定");
                        PAUSE
                        return 1;
                    }
                }

            }
            catch (const winreg::RegException& regerr)
            {
                spdlog::error("可能注册表key {} 不存在,请检查注册表目录 {}", "DisplayVersion 或 InstallLocation", "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\GitHubDesktop");
                spdlog::error("RegException {} ,ErrCode:{}  at line: {}", regerr.what(),regerr.code().value(), __LINE__);
                PAUSE
                return 1;
            }
            catch(const std::runtime_error& err) {
                spdlog::error("runtime_error {} at line: {}", err.what(), __LINE__);
                PAUSE
                return 1;
            }
            catch(const std::exception& e) {
                // 兜底: 避免 std::filesystem 等异常未捕获导致进程终止
                spdlog::error("读取GitHubDesktop安装信息时出现错误 {} at line: {}", e.what(), __LINE__);
                PAUSE
                return 1;
            }
        }
        key.Close();
#else
        spdlog::warn("非 Windows 平台无法自动探测 GitHub Desktop 安装目录");
        Base = utils::to_path(LoopGetBasePath());
        if(Base.empty()) {
            spdlog::error("未能获取资源目录, 请使用 -g 参数手动指定");
            PAUSE
            return 1;
        }
#endif // _WIN32
    }

    fs::path mainjs = "main.js";
    fs::path mainjsbak = "main.js.bak";

    fs::path rendererjs = "renderer.js";
    fs::path rendererjsbak = "renderer.js.bak";

    // 指定还原(短路: 在 json 加载之前执行, json 损坏/断网不阻断回滚)
    if(rollback) {
        int restore_fail = 0;
        auto restore_one = [&](const fs::path& bak, const fs::path& dst) {
            if(!fs::exists(bak)) {
                spdlog::warn("{} 回滚失败, {} 文件不存在", dst.string().c_str(), bak.string().c_str());
                restore_fail++;
                return;
            }
            // 原子化: 先复制到临时文件, 成功后再改名覆盖, 中断不会留下缺失/半截的目标文件
            const fs::path tmp = dst.string() + ".restoring";
            std::error_code ec;
            fs::copy_file(bak, tmp, fs::copy_options::overwrite_existing, ec);
            if(ec) {
                spdlog::error("回滚复制 {} -> {} 失败: {}", bak.string().c_str(), tmp.string().c_str(), ec.message());
                restore_fail++;
                return;
            }
            fs::rename(tmp, dst, ec);
            if(ec) {
                spdlog::error("回滚替换 {} -> {} 失败: {}", tmp.string().c_str(), dst.string().c_str(), ec.message());
                std::error_code rmec;
                fs::remove(tmp, rmec);
                restore_fail++;
                return;
            }
            spdlog::info("{} 还原完成", dst.string().c_str());
        };
        restore_one(Base / mainjsbak, Base / mainjs);
        restore_one(Base / rendererjsbak, Base / rendererjs);
        PAUSE
        return restore_fail ? 1 : 0;
    }

    // 如果是仅从远程仓库读取汉化文件
    if (only_read_from_remote) {
        spdlog::info("尝试从远程仓库中获取");
        std::string httpjson;
        if (utils::ReadHttpDataString("https://raw.githubusercontent.com" , "/Tupig/GitHubDesktop2Chinese/main/json/localization.json", httpjson, proxy)) {
            try {
                localization = json::parse(httpjson);
                spdlog::info("远程读取成功");
            }
            catch(const std::exception& e) {
                spdlog::error("远程映射文件解析失败: {}", e.what());
                PAUSE
                return 1;
            }
        }
        else {
            spdlog::warn("远程获取失败,请检查网络和代理,并稍后再试");
            PAUSE
            return 1;
        }
    }
    // 没有指定仅从远程仓库获取汉化文件
    else {
        // 判断汉化映射文件是否存在, 不存在则创建一个
        if (!fs::exists(LocalizationJSON)) {
            // 没有发现json文件,尝试从远程开源项目中获取
            spdlog::warn("没有指定,或从指定位置没有发现 {} 文件", "localization.json");
            spdlog::info("尝试从远程仓库中获取");
            std::string httpjson;
            if (utils::ReadHttpDataString("https://raw.githubusercontent.com" , "/Tupig/GitHubDesktop2Chinese/main/json/localization.json", httpjson, proxy)) {
                try {
                    localization = json::parse(httpjson);
                    spdlog::info("远程读取成功");
                }
                catch(const std::exception& e) {
                    spdlog::error("远程映射文件解析失败: {}", e.what());
                    PAUSE
                    return 1;
                }
            }
            else {
                spdlog::warn("远程获取失败 - 请{}重试", !proxy.second ? "尝试开启代理或":"");
                PAUSE
                return 1;
            }
        }
        else
        {
            // 本地读取汉化文件到json中
            std::ifstream config(LocalizationJSON);
            if (!config) {
                spdlog::error("localization.json 打开失败,无法读取");
                PAUSE
                return 1;
            }
            try
            {
                config >> localization;
            }
            catch (const std::exception& e)
            {
                spdlog::error("{} at line {}", e.what(), __LINE__);
                PAUSE
                return 1;
            }
        }
    }

    // 映射文件顶层必须为 JSON 对象, 否则后续下标访问是未定义行为
    if(!localization.is_object()) {
        spdlog::error("映射文件格式无效, 顶层必须为 JSON 对象");
        PAUSE
        return 1;
    }

    // E3: 必需顶层键缺失时 nlohmann 迭代空区间, 会 0 条替换仍报成功, 必须显式校验
    {
        const char* main_key = _debug_dev_replace ? "main_dev" : "main";
        const char* renderer_key = _debug_dev_replace ? "renderer_dev" : "renderer";
        for(const char* k : { main_key, renderer_key }) {
            if(!localization.contains(k) || !localization.at(k).is_array() || localization.at(k).empty()) {
                spdlog::error("映射文件缺少或为空的必需条目: {}", k);
                PAUSE
                return 1;
            }
        }
        if(!_debug_dev_replace) {
            if(!localization.contains("select") || !localization.at("select").is_array()) {
                spdlog::error("映射文件缺少必需条目: select");
                PAUSE
                return 1;
            }
        }
    }

    // 读取映射文件中的提示信息
    if (localization.contains("tip") && localization.at("tip").is_array() && !localization.at("tip").empty()) {
        for(auto& it : localization.at("tip")) {
            if (it.is_string()) {
                spdlog::info(" **通知** {}", it.get<std::string>());
            }
        }
    }



    // 如果没有js文件却有备份文件 则从备份恢复
    if (!fs::exists(Base / mainjs)) {
        if (!fs::exists(Base / mainjsbak)) {
            spdlog::warn("目录有误，找不到目录下的main.js. ");
            PAUSE
            return 1;
        }
        std::error_code ec;
        fs::copy_file(Base / "main.js.bak", Base / "main.js", ec);
        if(ec) {
            spdlog::error("从备份还原 main.js 失败: {}", ec.message());
            PAUSE
            return 1;
        }
        spdlog::warn("main.js 未找到, 但已从备份main.js.bak中还原");
    }

    if (!fs::exists(Base / rendererjs)) {
        if (!fs::exists(Base / rendererjsbak)) {
            spdlog::warn("目录有误，找不到目录下的renderer.js. ");
            PAUSE
            return 1;
        }
        std::error_code ec;
        fs::copy_file(Base / "renderer.js.bak", Base / "renderer.js", ec);
        if(ec) {
            spdlog::error("从备份还原 renderer.js 失败: {}", ec.message());
            PAUSE
            return 1;
        }
        spdlog::warn("renderer.js 未找到, 但已从备份renderer.js.bak中还原");
    }

    // 仅在备份文件不存在时备份
    if (!fs::exists(Base / "main.js.bak")) {
        std::error_code ec;
        fs::copy_file(Base / "main.js", Base / "main.js.bak", ec);
        if(ec) {
            spdlog::error("创建备份 main.js -> main.js.bak 失败: {}, 已中止以免后续无法还原", ec.message());
            PAUSE
            return 1;
        }
        spdlog::info("已新建备份 main.js -> main.js.bak");
    }

    if (!fs::exists(Base / "renderer.js.bak")) {
        std::error_code ec;
        fs::copy_file(Base / "renderer.js", Base / "renderer.js.bak", ec);
        if(ec) {
            spdlog::error("创建备份 renderer.js -> renderer.js.bak 失败: {}, 已中止以免后续无法还原", ec.message());
            PAUSE
            return 1;
        }
        spdlog::info("已新建备份 renderer.js -> renderer.js.bak");
    }

    // 判断版本
    std::string minver_str;
    if(localization.contains("minversion")) {
        if(localization["minversion"].is_string()) {
            minver_str = localization["minversion"].get<std::string>();
        }
        else {
            spdlog::warn("映射文件中 minversion 不是字符串, 已忽略");
        }
    }
    if(FileVer.status != versionparse::Version::Dev && !minver_str.empty()) {
        versionparse::Version JsonVer(minver_str.c_str());
        if(!JsonVer) {
            // 无法确认最低兼容版本: 中止比带着未知兼容性继续替换更安全
            spdlog::error("映射文件中 minversion 解析失败, 无法确认最低兼容版本, 已中止: {}", minver_str);
            PAUSE
            return 1;
        }
        else {
            if(FileVer < JsonVer) {
                // 不符合要求
                spdlog::warn("文件要求加载器版本至少为: {}, 但加载器版本为: {}", JsonVer.toString(), FileVer.toString());
                spdlog::info("请更新：{}", "https://github.com/Tupig/GitHubDesktop2Chinese/releases");
                // 询问是否强制执行

                if(!no_pause) {
                    spdlog::info("输入(f)强制执行替换(可能会导致无法打开), 其他退出..");
                    std::string input;
                    std::cin >> input;
                    if(!std::cin || (input != "f" && input != "F")) {
                        PAUSE
                        return 1;
                    }
                }
                else {
                    spdlog::warn("版本不满足要求, 因 --nopause 已跳过询问并强制执行");
                }
            }
            else {
                PAUSE
            }
        }
    }
    else {
        PAUSE
    }
    
    int ret_num = 0;
    // 处理 main[_dev].js 映射（与 renderer 共用 ProcessJsFile 单一实现, 2026-10 审计 M2 去重）
    {
        const char* target_key = _debug_dev_replace ? "main_dev" : "main";
        if(ProcessJsFile("main.js", "main", target_key, _debug_error_check_mode_main, ret_num) != 0) {
            PAUSE
            return 1;
        }
    }

    // 处理 renderer[_dev].js 映射
    {
        const char* target_key = _debug_dev_replace ? "renderer_dev" : "renderer";
        if(ProcessJsFile("renderer.js", "renderer", target_key, _debug_error_check_mode_renderer, ret_num) != 0) {
            PAUSE
            return 1;
        }
    }

    // 获取项目参与者:
    // https://api.github.com/repos/Tupig/GitHubDesktop2Chinese/contributors
    try {
        spdlog::info("正在获取项目参与者");
        std::string contributors;
        if(utils::ReadHttpDataString("https://api.github.com" , "/repos/Tupig/GitHubDesktop2Chinese/contributors", contributors, proxy)) {
            auto contributorsjson = json::parse(contributors);
            spdlog::info("人数: {}", contributorsjson.size());
            int num = 0;
            for(auto& item : contributorsjson.items()) {
                num++;
                if(num > 10) break;
                spdlog::info("{}/10 名称:{} 贡献:{}{}{} 主页:{}", num, item.value()["login"].get<std::string>(),"\033[37m\033[5m", item.value()["contributions"].get<int>(),"\033[0m", item.value()["html_url"].get<std::string>());
            }
        }
    }
    catch(const std::exception& e) {
        spdlog::warn("读取解析数据出现异常: {}", e.what());
    }
    catch(...) {
        spdlog::warn("读取解析数据出现未知异常.");
    }


    PAUSE
    // invalidcheck 的失败计数作为退出码, 钳制到 255 避免截断歧义
    return ret_num > 255 ? 255 : ret_num;
}

bool GetBasePath(std::string& out) {
    if(!getline(std::cin, out)) {
        spdlog::error("读取输入失败(输入流已结束)");
        return false;
    }
    // 去除首尾空白与成对引号: 支持从资源管理器拖拽文件夹到控制台(终端会为路径加引号)
    {
        const size_t first = out.find_first_not_of(" \t\r\n");
        const size_t last = out.find_last_not_of(" \t\r\n");
        out = (first == std::string::npos) ? std::string() : out.substr(first, last - first + 1);
        if(out.size() >= 2 && out.front() == '"' && out.back() == '"') {
            out = out.substr(1, out.size() - 2);
        }
    }
    // 控制台输入为UTF-8, 直接按窄字符串构造路径会被错误地按ANSI代码页解释
    fs::path base = utils::to_path(out);
    if (!fs::exists(base)) {
        spdlog::warn("你输入的目录不存在. ");
        return false;
    }
    fs::path mainjs = "main.js";
    fs::path mainjsbak = "main.js.bak";
    if (!fs::exists(base / mainjs)) {
        if (!fs::exists(base / mainjsbak)) {
            spdlog::warn("目录有误，找不到目录下的main.js. ");
            return false;
        }
        std::error_code ec;
        fs::copy_file(base / "main.js.bak", base / "main.js", ec);
        if(ec) {
            spdlog::warn("从备份还原 main.js 失败: {}", ec.message());
            return false;
        }
        spdlog::warn("main.js 未找到, 但已从备份main.js.bak中还原");
    }

    fs::path rendererjs = "renderer.js";
    fs::path rendererjsbak = "renderer.js.bak";
    if (!fs::exists(base / rendererjs)) {
        if (!fs::exists(base / rendererjsbak)) {
            spdlog::warn("目录有误，找不到目录下的renderer.js. ");
            return false;
        }
        std::error_code ec;
        fs::copy_file(base / "renderer.js.bak", base / "renderer.js", ec);
        if(ec) {
            spdlog::warn("从备份还原 renderer.js 失败: {}", ec.message());
            return false;
        }
        spdlog::warn("renderer.js 未找到, 但已从备份renderer.js.bak中还原");
    }
    return true;
}

std::string LoopGetBasePath() {
    spdlog::info("请输入目录.");
    std::string tempDir;
    while (true)
    {
        if (GetBasePath(tempDir)) {
            return tempDir;
        }
        if (std::cin.eof() || std::cin.fail()) {
            // 输入流已结束/出错(管道/重定向运行), 继续循环只会死循环, 返回空由调用方处理
            spdlog::error("无可用输入, 请使用 -g 参数手动指定资源目录");
            return "";
        }
        spdlog::info("重新输入.");
    }
}

void DeveloperOptions() {
    while (true)
    {
        ClearScreen();
        spdlog::info("选择你要修改的功能");
        spdlog::info("0) 跳出.");
        spdlog::info("1) [{}] main崩溃调试.", _debug_error_check_mode_main);
        spdlog::info("2) [{}] renderer崩溃调试.", _debug_error_check_mode_renderer);
        spdlog::info("3) [{}] 翻译项失效检测.", _debug_invalid_check_mode);
        spdlog::info("4) [{}] 不替换资源.不干预其他开发者选项.", _debug_no_replace_res);
        spdlog::info("5) [{}] 优先从备份文件中汉化(会直接改变资源文件的来源,影响其他选项).", _debug_translation_from_bak);
        spdlog::info("6) [{}] 仅替换指定映射项，以优化汉化作者替换时间", _debug_dev_replace);
        if(FileVer.major == 0 && FileVer.minor == 0 && FileVer.revision == 0) {
            spdlog::info("20) [{}] 手动指定程序版本仅限于调试", _debug_dev_setversion);
        }
        std::cout << std::endl;

        int sys = 0;
        spdlog::info("请输入你要修改的功能:");
        if(!(std::cin >> sys)) {
            // 注意: 必须先判断eof再clear, clear会同时清除eofbit导致EOF无法识别
            if(std::cin.eof()) {
                spdlog::error("输入流已结束, 退出开发者菜单");
                return;
            }
            // 非数字输入: 清理流状态与剩余字符后重新询问
            std::cin.clear();
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            spdlog::warn("输入无效, 请输入数字");
            continue;
        }
        switch (sys)
        {
        case 0:
            return;
        case 1:
            _debug_error_check_mode_main = utils::ReadUserInput_bool({ "false", "true" });
            break;
        case 2:
            _debug_error_check_mode_renderer = utils::ReadUserInput_bool({ "false", "true" });
            break;
        case 3:
            _debug_invalid_check_mode = utils::ReadUserInput_bool({ "false", "true" });
            break;
        case 4:
            _debug_no_replace_res = utils::ReadUserInput_bool({ "false", "true" });
            break;
        case 5:
            _debug_translation_from_bak = utils::ReadUserInput_bool({ "false", "true" });
            break;
        case 6:
            _debug_dev_replace = utils::ReadUserInput_bool({ "false", "true" });
            break;
        case 20:
            // 与菜单显示/CLI注册一致: release 版本不可开启指定版本(仅 dev 0.0.0 可用)
            if(FileVer.major == 0 && FileVer.minor == 0 && FileVer.revision == 0) {
                _debug_dev_setversion = utils::ReadUserInput_bool({ "false", "true" });
            }
            else {
                spdlog::warn("该选项仅限开发者版本使用");
            }
            break;
        default:
            spdlog::warn("无效选项");
            break;
        }
    }
    
}


std::optional<std::string> formatTime(std::string time_str) {
    std::optional<std::string> ret;
    // GitHub API 时间形如 2024-05-01T12:34:56Z; 手动解析各字段,
    // 避免依赖 C++20 std::chrono::parse (libc++ 与部分 libstdc++ 尚未实现, 跨平台编译失败)
    std::tm tm{};
    int year = 0, mon = 0, day = 0, hour = 0, min = 0, sec = 0;
    if(std::sscanf(time_str.c_str(), "%d-%d-%dT%d:%d:%d", &year, &mon, &day, &hour, &min, &sec) == 6) {
        tm.tm_year = year - 1900;
        tm.tm_mon = mon - 1;
        tm.tm_mday = day;
        tm.tm_hour = hour;
        tm.tm_min = min;
        tm.tm_sec = sec;
        // 解析结果为 UTC, 转 epoch 后用 localtime 显示本地时间
#ifdef _WIN32
        const std::time_t cftime = _mkgmtime(&tm);
#else
        const std::time_t cftime = timegm(&tm);
#endif
        if(cftime != static_cast<std::time_t>(-1)) {
            if(const std::tm* local = std::localtime(&cftime)) {
                std::ostringstream oss;
                oss << std::put_time(local, "%Y年%m月%d日 %H时%M分%S秒");
                ret = oss.str();
            }
        }
    }
    return ret;
}
