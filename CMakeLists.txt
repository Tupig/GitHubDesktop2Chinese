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

# ── MSVC 专用编译/链接选项（必须在 project() 之后：GCC/Clang 会把 /xxx 当输入文件，不能无条件加）──
if(MSVC)
    # 中文源码与执行字符集按 UTF-8 处理
    add_compile_options(/utf-8)
    # Debug 需要 /bigobj；Release 生成 PDB 并启用链接器调试信息
    add_compile_options($<$<CONFIG:Debug>:/bigobj> $<$<CONFIG:Release>:/Zi>)
    add_link_options($<$<CONFIG:Release>:/DEBUG> $<$<CONFIG:Release>:/OPT:REF> $<$<CONFIG:Release>:/OPT:ICF>)
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
#add_definitions(-DFILEVERSION="v1.0.2")
else()
add_definitions(-DFILEVERSION="0.0.0-Dev.0")
endif()

include_directories(src)
include_directories(third_party/include)

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

    # 包含 openssl 的头文件
    include_directories("${GHD2C_OPENSSL_ROOT}/include")
else()
    # ── macOS/Linux: 系统包管理器提供的 OpenSSL（如 apt install libssl-dev / brew install openssl）──
    find_package(OpenSSL REQUIRED)
    find_package(Threads REQUIRED)
endif()

# 将源代码添加到此项目的可执行文件。
add_executable (GitHubDesktop2Chinese "src/GitHubDesktop2Chinese.cpp")


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
else()
    target_link_libraries(GitHubDesktop2Chinese PRIVATE
        OpenSSL::SSL
        OpenSSL::Crypto
        Threads::Threads)
endif()

# CLI11 已内置到 third_party/include/CLI（header-only，无需额外链接）

set_property(TARGET GitHubDesktop2Chinese PROPERTY CXX_STANDARD 20)
