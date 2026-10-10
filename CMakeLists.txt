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
    # ── Windows: OpenSSL 静态库下载（预编译 x64 库）──
    # 官方与镜像 URL 内容一致, 均通过 EXPECTED_HASH(SHA256) 校验
    # 哈希来源: 自 GitHub 官方 Release URL 下载实测 (2026-10); 上游更新库文件时需同步更新哈希
    # OpenSSL 预编译库根目录: 默认位于源码树; 只读源码目录场景可通过
    # -DGHD2C_OPENSSL_ROOT=<可写目录> 覆盖(下载/查找/头文件/链接均使用该目录)
    set(GHD2C_OPENSSL_ROOT "${PROJECT_SOURCE_DIR}/third_party/openssl" CACHE PATH "OpenSSL 预编译库根目录")
    if(DEFINED GitHubActions)
        set(LIBSSL_URL https://github.com/cngege/cngege.github.io/releases/download/v0.1-beta.1/libssl.lib)
        set(CRYPTO_URL https://github.com/cngege/cngege.github.io/releases/download/v0.1-beta.1/libcrypto.lib)
    else()
        set(LIBSSL_URL https://www.ghproxy.cc/https://github.com/cngege/cngege.github.io/releases/download/v0.1-beta.1/libssl.lib)
        set(CRYPTO_URL https://www.ghproxy.cc/https://github.com/cngege/cngege.github.io/releases/download/v0.1-beta.1/libcrypto.lib)
    endif()



    # 下载libssl.lib

    # 设置库远程下载超时时间
    SET(DOWNLOAD_OPENSSL_TIMEOUT 300 CACHE STRING "Timeout in seconds when downloading openssl_lib.")
    FIND_FILE(LOCAL_OPENSSL_LIBRARIES_PATH
        NAMES ""
        HINTS "${GHD2C_OPENSSL_ROOT}/lib"
        NO_DEFAULT_PATH)
    if(NOT EXISTS "${GHD2C_OPENSSL_ROOT}/lib")
        file(MAKE_DIRECTORY "${GHD2C_OPENSSL_ROOT}/lib")
        SET(LOCAL_OPENSSL_LIBRARIES_PATH "${GHD2C_OPENSSL_ROOT}/lib")
        MESSAGE(STATUS "Folder \"${GHD2C_OPENSSL_ROOT}/lib\" created successfully.")
    endif()
    # FIND_FILE(NAMES "") 在 lib 目录已存在但库文件缺失时返回 NOTFOUND,
    # 会展开成 "xxx-NOTFOUND/libssl.lib" 这样的错误下载目标; 回退到固定路径
    if(NOT LOCAL_OPENSSL_LIBRARIES_PATH)
        SET(LOCAL_OPENSSL_LIBRARIES_PATH "${GHD2C_OPENSSL_ROOT}/lib")
    endif()

    # /////////////////////////////////////ssl库lib名称////////////////////////////////////////////////
    SET(OPENSSL_SSL_NAME "libssl.lib")

    # ssl远程下载地址
    SET(SSL_DOWNLOAD_URL ${LIBSSL_URL})
    MESSAGE(STATUS "Checking ${OPENSSL_SSL_NAME}...")
    # 判断库文件是否已经存在
    FIND_FILE(LOCAL_OPENSSL_SSL_LIB
        NAMES ${OPENSSL_SSL_NAME}
        HINTS "${GHD2C_OPENSSL_ROOT}/lib"
        #NO_CACHE
        NO_DEFAULT_PATH)
    MESSAGE(STATUS "Checking url link ${LIBSSL_URL} ...")
    # 如果路径下没有目标文件
    IF(NOT LOCAL_OPENSSL_SSL_LIB)
        MESSAGE(STATUS "Downloading ${OPENSSL_SSL_NAME} to ${LOCAL_OPENSSL_LIBRARIES_PATH} ...")
            # 从设定的 URL 地址下载相应的压缩包
        FILE(DOWNLOAD ${LIBSSL_URL}
            ${LOCAL_OPENSSL_LIBRARIES_PATH}/${OPENSSL_SSL_NAME}
            EXPECTED_HASH SHA256=80c92c42796d4cde173916debae464c9e7e428fed07ee313b64ff1845c08736f
            TIMEOUT ${DOWNLOAD_OPENSSL_TIMEOUT}
            STATUS ERR
            SHOW_PROGRESS)
        # 判断下载是否存在错误
        IF(ERR EQUAL 0)
            # 如果下载无错误则设置压缩包名称标记下载成功
            SET(LOCAL_OPENSSL_SSL_LIB "${LOCAL_OPENSSL_LIBRARIES_PATH}/${OPENSSL_SSL_NAME}")
        #下载错误输出相关的提示信息
        ELSE()
            FILE(REMOVE ${LOCAL_OPENSSL_LIBRARIES_PATH}/${OPENSSL_SSL_NAME})
            MESSAGE(STATUS "Download failed, error: ${ERR}")
            MESSAGE(FATAL_ERROR
            "You can try downloading ${LIBSSL_URL} manually "
            "using curl/wget or a similar tool")
        ENDIF()
    ENDIF()
    # 如果路径下存在文件
    IF(LOCAL_OPENSSL_SSL_LIB)
        MESSAGE(STATUS "Checking the direction of ${LOCAL_OPENSSL_LIBRARIES_PATH}/${OPENSSL_SSL_NAME}")
    ENDIF()


    # /////////////////////////////////////crypto库lib名称////////////////////////////////////////////////
    SET(OPENSSL_CRYPTO_NAME "libcrypto.lib")
    # ssl远程下载地址
    SET(CRYPTO_DOWNLOAD_URL ${CRYPTO_URL})
    MESSAGE(STATUS "Checking ${OPENSSL_CRYPTO_NAME}...")
    # 判断库文件是否已经存在
    FIND_FILE(LOCAL_OPENSSL_CRYPTO_LIB
        NAMES ${OPENSSL_CRYPTO_NAME}
        HINTS "${GHD2C_OPENSSL_ROOT}/lib"
        #NO_CACHE
        NO_DEFAULT_PATH)
    MESSAGE(STATUS "Checking url link ${CRYPTO_URL} ...")
    # 如果路径下没有目标文件
    IF(NOT LOCAL_OPENSSL_CRYPTO_LIB)
        MESSAGE(STATUS "Downloading ${OPENSSL_CRYPTO_NAME} to ${LOCAL_OPENSSL_LIBRARIES_PATH} ...")
            # 从设定的 URL 地址下载相应的压缩包
        FILE(DOWNLOAD ${CRYPTO_URL}
            ${LOCAL_OPENSSL_LIBRARIES_PATH}/${OPENSSL_CRYPTO_NAME}
            EXPECTED_HASH SHA256=1e32b094bf4d9c5d9eb62c46b3b258c23bf6078e25f48b3cc869b86fed33d87c
            TIMEOUT ${DOWNLOAD_OPENSSL_TIMEOUT}
            STATUS ERR
            SHOW_PROGRESS)
        # 判断下载是否存在错误
        IF(ERR EQUAL 0)
            # 如果下载无错误则设置压缩包名称标记下载成功
            SET(LOCAL_OPENSSL_CRYPTO_LIB "${LOCAL_OPENSSL_LIBRARIES_PATH}/${OPENSSL_CRYPTO_NAME}")
        #下载错误输出相关的提示信息
        ELSE()
            FILE(REMOVE ${LOCAL_OPENSSL_LIBRARIES_PATH}/${OPENSSL_CRYPTO_NAME})
            MESSAGE(STATUS "Download failed, error: ${ERR}")
            MESSAGE(FATAL_ERROR
            "You can try downloading ${CRYPTO_URL} manually "
            "using curl/wget or a similar tool")
        ENDIF()
    ENDIF()
    # 如果路径下存在压缩文件
    IF(LOCAL_OPENSSL_CRYPTO_LIB)
        MESSAGE(STATUS "Checking the direction of ${LOCAL_OPENSSL_LIBRARIES_PATH}/${OPENSSL_CRYPTO_NAME}")
    ENDIF()

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
    # 链接OpenSSL静态库（Windows 预编译 x64）
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
