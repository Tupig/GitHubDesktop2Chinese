# CMakeList.txt: GitHubDesktop2Chinese 的 CMake 项目，在此处包括源代码并定义
# 项目特定的逻辑。
#
cmake_minimum_required (VERSION 3.30)

# 如果支持，请为 MSVC 编译器启用热重载。
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project ("GitHubDesktop2Chinese")

# ── #134: 裸 cmake 无 preset 时兜底 Release（否则得到无优化无 NDEBUG 的裸构建）──
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "构建类型" FORCE)
endif()

# ── MSVC 专用编译/链接选项（必须在 project() 之后：GCC/Clang 会把 /xxx 当输入文件，不能无条件加）──
if(MSVC)
    # 中文源码与执行字符集按 UTF-8 处理
    add_compile_options(/utf-8)
    # #134: 标准一致性模式（两阶段名字查找等诊断开启，模板重库误用更易暴露）
    add_compile_options(/permissive-)
    # #134: fmt 9.1(spdlog 1.11 bundled) 在 _SECURE_SCL=1 时启用 stdext::checked_array_iterator，
    # 而 VS2026 STL 已将其移除、yvals.h 在 Debug 下默认 _SECURE_SCL=1 → Debug 必然编译失败。
    # 预定义 0 让 fmt 走裸指针分支(= fmt 10+ 行为)；_ITERATOR_DEBUG_LEVEL 由
    # _HAS_ITERATOR_DEBUGGING 推导不受影响，Debug 迭代器校验完整保留
    add_compile_definitions(_SECURE_SCL=0)
    # #134: 提升告警级别；/external:W0 将 SYSTEM 头(third_party/openssl)降回 W0 不刷屏
    add_compile_options(/W4 /external:W0)
    # #134: /bigobj 全配置（Release 模板实例化膨胀同样可能触发 C1169）；Release 生成 PDB
    add_compile_options(/bigobj $<$<CONFIG:Release>:/Zi>)
    add_link_options($<$<CONFIG:Release>:/DEBUG> $<$<CONFIG:Release>:/OPT:REF> $<$<CONFIG:Release>:/OPT:ICF>)
    # OpenSSL 静态库不含 PDB，LNK4099 为已知噪音：显式忽略，保证构建零告警（#92）
    add_link_options(/ignore:4099)
else()
    # #134: GCC/Clang 高告警级别（third_party SYSTEM 头按 -isystem 天然抑制）
    add_compile_options(-Wall -Wextra)
endif()

# 本项目仅支持 64 位架构（x64 / arm64 / x86_64 均可，指针宽度为 8）。
# 指针宽度由 project() 完成编译器探测后判定，对 VS(-A x64/-A Win32) 与
# Ninja 预设(external 策略下 CMAKE_GENERATOR_PLATFORM 为空)两条路径均生效
if (NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "GitHubDesktop2Chinese 仅支持 64 位架构（x64/arm64）构建，请通过 -A x64 或对应预设进行配置")
endif()
message(STATUS "目标架构: ${CMAKE_SYSTEM_PROCESSOR} (64 位)")

if(DEFINED FILE_VERSION)
add_definitions(-DFILEVERSION="${FILE_VERSION}")
else()
add_definitions(-DFILEVERSION="0.0.0-Dev.0")
endif()

if(WIN32)
    # ── Windows: OpenSSL 3.5.9 LTS 静态库（随仓库提交于 third_party/openssl/{include,lib}）──
    # 库由官方源码构建：github.com/openssl/openssl（tag openssl-3.5.9），
    # perl Configure VC-WIN64A no-shared no-tests no-apps no-zlib no-asm（MSVC x64 静态库），
    # 构建与更新步骤见 third_party/README.md；CMake 仅做存在性校验，不从任何外部地址下载。
    # 源码目录只读等场景可通过 -DGHD2C_OPENSSL_ROOT=<目录> 覆盖查找与链接路径。
    set(GHD2C_OPENSSL_ROOT "${PROJECT_SOURCE_DIR}/third_party/openssl" CACHE PATH "OpenSSL 静态库根目录（含 include/ 与 lib/）")
    SET(OPENSSL_SSL_NAME "libssl.lib")
    SET(OPENSSL_CRYPTO_NAME "libcrypto.lib")

    # 完整性校验：任一组件缺失即报错并给出恢复指引（git 恢复优先，其次按 third_party/README.md 自行构建）
    set(GHD2C_OPENSSL_REQUIRED
        "include/openssl/ssl.h"
        "lib/${OPENSSL_SSL_NAME}"
        "lib/${OPENSSL_CRYPTO_NAME}")
    foreach(_oss_comp IN LISTS GHD2C_OPENSSL_REQUIRED)
        if(NOT EXISTS "${GHD2C_OPENSSL_ROOT}/${_oss_comp}")
            message(FATAL_ERROR
                "OpenSSL 静态库组件缺失: ${GHD2C_OPENSSL_ROOT}/${_oss_comp}\n"
                "third_party/openssl/{include,lib} 应随仓库提交。若被误删，请执行:\n"
                "  git checkout -- third_party/openssl\n"
                "或参考 third_party/README.md 用官方源码自行构建后重试。")
        endif()
    endforeach()

elseif(APPLE)
    # ── macOS: OpenSSL 3.5.9 LTS 静态库（随仓库提交于 third_party/openssl-macos-{x64,arm64}）──
    # 库由主工作流 ghdesktop2chinese.yml 的 vendor 任务从官方源码构建（tag openssl-3.5.9，
    # darwin64-{x86_64,arm64}-cc no-shared no-tests no-apps no-zlib，MACOSX_DEPLOYMENT_TARGET=11.0），
    # 构建与更新步骤见 third_party/README.md；CMake 按 CMAKE_OSX_ARCHITECTURES（回退
    # CMAKE_SYSTEM_PROCESSOR）自动选架构目录，构建期零下载零编译。
    # 本地自建库可通过 -DGHD2C_OPENSSL_ROOT=<目录> 覆盖查找与链接路径。
    if("${CMAKE_OSX_ARCHITECTURES}" STREQUAL "arm64")
        set(GHD2C_OPENSSL_DEFAULT_DIR "${PROJECT_SOURCE_DIR}/third_party/openssl-macos-arm64")
    elseif("${CMAKE_OSX_ARCHITECTURES}" STREQUAL "x86_64")
        set(GHD2C_OPENSSL_DEFAULT_DIR "${PROJECT_SOURCE_DIR}/third_party/openssl-macos-x64")
    elseif("${CMAKE_SYSTEM_PROCESSOR}" MATCHES "^(arm64|aarch64)$")
        set(GHD2C_OPENSSL_DEFAULT_DIR "${PROJECT_SOURCE_DIR}/third_party/openssl-macos-arm64")
    else()
        set(GHD2C_OPENSSL_DEFAULT_DIR "${PROJECT_SOURCE_DIR}/third_party/openssl-macos-x64")
    endif()
    set(GHD2C_OPENSSL_ROOT "" CACHE PATH "OpenSSL 静态库根目录覆盖（留空=按架构自动选择 third_party/openssl-macos-*）")
    if("${GHD2C_OPENSSL_ROOT}" STREQUAL "")
        set(GHD2C_OPENSSL_ROOT "${GHD2C_OPENSSL_DEFAULT_DIR}")
    endif()

    # 完整性校验：任一组件缺失即报错并给出恢复指引（与 Windows 分支一致）
    set(GHD2C_OPENSSL_REQUIRED
        "include/openssl/ssl.h"
        "lib/libssl.a"
        "lib/libcrypto.a")
    foreach(_oss_comp IN LISTS GHD2C_OPENSSL_REQUIRED)
        if(NOT EXISTS "${GHD2C_OPENSSL_ROOT}/${_oss_comp}")
            message(FATAL_ERROR
                "OpenSSL 静态库组件缺失: ${GHD2C_OPENSSL_ROOT}/${_oss_comp}\n"
                "third_party/openssl-macos-{x64,arm64} 应随仓库提交。若被误删，请执行:\n"
                "  git checkout -- third_party/openssl-macos-x64 third_party/openssl-macos-arm64\n"
                "或参考 third_party/README.md 用主工作流 vendor 任务重新产出后提交。")
        endif()
    endforeach()

    find_package(Threads REQUIRED)
else()
    # ── Linux: OpenSSL 3.5.9 LTS 静态库（随仓库提交于 third_party/openssl-linux-x64）──
    # 库由主工作流 ghdesktop2chinese.yml 的 vendor 任务从官方源码构建（tag openssl-3.5.9，
    # linux-x86_64 -fPIC no-shared no-tests no-apps no-zlib --libdir=lib），构建与更新
    # 步骤见 third_party/README.md；CMake 仅做存在性校验，构建期零下载零编译。
    # 本地自建库可通过 -DGHD2C_OPENSSL_ROOT=<目录> 覆盖查找与链接路径。
    set(GHD2C_OPENSSL_ROOT "${PROJECT_SOURCE_DIR}/third_party/openssl-linux-x64" CACHE PATH "OpenSSL 静态库根目录（含 include/ 与 lib/）")

    # 完整性校验：任一组件缺失即报错并给出恢复指引（与 Windows/macOS 分支一致）
    set(GHD2C_OPENSSL_REQUIRED
        "include/openssl/ssl.h"
        "lib/libssl.a"
        "lib/libcrypto.a")
    foreach(_oss_comp IN LISTS GHD2C_OPENSSL_REQUIRED)
        if(NOT EXISTS "${GHD2C_OPENSSL_ROOT}/${_oss_comp}")
            message(FATAL_ERROR
                "OpenSSL 静态库组件缺失: ${GHD2C_OPENSSL_ROOT}/${_oss_comp}\n"
                "third_party/openssl-linux-x64 应随仓库提交。若被误删，请执行:\n"
                "  git checkout -- third_party/openssl-linux-x64\n"
                "或参考 third_party/README.md 用主工作流 vendor 任务重新产出后提交。")
        endif()
    endforeach()

    find_package(Threads REQUIRED)
endif()

# 将源代码添加到此项目的可执行文件。
add_executable (GitHubDesktop2Chinese "src/GitHubDesktop2Chinese.cpp")

# ── #134: include 全部 target 级（原 include_directories 目录级会污染后续 target）──
# 私有源码目录
target_include_directories(GitHubDesktop2Chinese PRIVATE ${PROJECT_SOURCE_DIR}/src)
# SYSTEM：第三方头（spdlog/fmt/CLI11/openssl）告警不归本仓管控；
# MSVC 按 /external:I + /external:W0、GCC/Clang 按 -isystem 抑制（#92/#134）
target_include_directories(GitHubDesktop2Chinese SYSTEM PRIVATE
    ${PROJECT_SOURCE_DIR}/third_party/include
    "${GHD2C_OPENSSL_ROOT}/include")

if(WIN32)
    # 链接OpenSSL静态库（third_party/openssl 仓库内 x64 静态库）
    target_link_libraries(GitHubDesktop2Chinese PRIVATE
        ${GHD2C_OPENSSL_ROOT}/lib/${OPENSSL_SSL_NAME}
        ${GHD2C_OPENSSL_ROOT}/lib/${OPENSSL_CRYPTO_NAME})
    if(MSVC)
        # 进程级 UTF-8 主动代码页清单(见 src/app.manifest):
        # 缺少时窄字符路径按系统 ANSI 代码页解释, 非中文系统上中文路径参数会解析失败
        # SHELL: 前缀保证含空格的源码路径作为单个链接选项传递
        target_link_options(GitHubDesktop2Chinese PRIVATE
            "SHELL:/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/src/app.manifest")
    endif()
elseif(APPLE)
    # 链接内置 OpenSSL 静态库（third_party/openssl-macos-*，配置段已按架构选定；
    # libssl 依赖 libcrypto，顺序不可颠倒）
    target_link_libraries(GitHubDesktop2Chinese PRIVATE
        ${GHD2C_OPENSSL_ROOT}/lib/libssl.a
        ${GHD2C_OPENSSL_ROOT}/lib/libcrypto.a
        Threads::Threads)
else()
    # 链接内置 OpenSSL 静态库（third_party/openssl-linux-x64，配置段已校验存在；
    # libssl 依赖 libcrypto，顺序不可颠倒；CMAKE_DL_LIBS 补齐 dlopen 动态加载
    # 依赖（glibc < 2.34 时 dlopen 在 libdl.so 中，≥ 2.34 已并入 libc）
    target_link_libraries(GitHubDesktop2Chinese PRIVATE
        ${GHD2C_OPENSSL_ROOT}/lib/libssl.a
        ${GHD2C_OPENSSL_ROOT}/lib/libcrypto.a
        Threads::Threads
        ${CMAKE_DL_LIBS})
endif()

# CLI11 已内置到 third_party/include/CLI（header-only，无需额外链接）

# #130: spdlog 宽字符文件名(Windows): 中文用户名的 %TEMP% 日志路径无损传给 _wfopen
if(WIN32)
    target_compile_definitions(GitHubDesktop2Chinese PRIVATE SPDLOG_WCHAR_FILENAMES)
endif()

set_property(TARGET GitHubDesktop2Chinese PROPERTY CXX_STANDARD 20)

# ── 单元测试(#123): Version 表驱动解析/比较测试, 无外部依赖 ──
# 构建: cmake --build . --target version_test;  运行: ctest --output-on-failure
enable_testing()
add_executable(version_test tests/version_test.cpp)
set_property(TARGET version_test PROPERTY CXX_STANDARD 20)
target_include_directories(version_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
add_test(NAME version_test COMMAND version_test)

# ── 替换引擎等价性测试(#129): 字面量快路径/单遍正则 与 regex_replace 逐字节等价 ──
# 内嵌用例无外部依赖(CI 必跑); 真实数据对比可选传 argv[1]=json argv[2]=js 本地运行
add_executable(replace_engine_test tests/replace_engine_test.cpp)
set_property(TARGET replace_engine_test PROPERTY CXX_STANDARD 20)
target_include_directories(replace_engine_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
add_test(NAME replace_engine_test COMMAND replace_engine_test)
