# CMakeList.txt: GitHubDesktop2Chinese 的 CMake 项目，在此处包括源代码并定义
# 项目特定的逻辑。
#
cmake_minimum_required (VERSION 3.30)

# 如果支持，请为 MSVC 编译器启用热重载。
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()


#不要放在 if(MSVC) 中
add_compile_options(/utf-8)


project ("GitHubDesktop2Chinese")

# 本项目仅支持 64 位（x64）构建。
# 指针宽度由 project() 完成编译器探测后判定，对 VS(-A x64/-A Win32) 与
# Ninja 预设(external 策略下 CMAKE_GENERATOR_PLATFORM 为空)两条路径均生效
if (NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "GitHubDesktop2Chinese 仅支持 64 位（x64）构建，请通过 -A x64 或 x64 预设进行配置")
endif()
message("当前是以x64模式编译")

# 按配置附加标志（生成器表达式对单配置与多配置生成器均生效）:
# Debug 需要 /bigobj；Release 生成 PDB 并启用链接器调试信息
add_compile_options($<$<CONFIG:Debug>:/bigobj> $<$<CONFIG:Release>:/Zi>)
add_link_options($<$<CONFIG:Release>:/DEBUG> $<$<CONFIG:Release>:/OPT:REF> $<$<CONFIG:Release>:/OPT:ICF>)



if(DEFINED FILE_VERSION)
add_definitions(-DFILEVERSION="${FILE_VERSION}")
#add_definitions(-DFILEVERSION="v1.0.2")
else()
add_definitions(-DFILEVERSION="0.0.0-Dev.0")
endif()

include_directories(src)
include_directories(third_party/include)

# OpenSSL 静态库下载: 官方与镜像 URL 内容一致, 均通过 EXPECTED_HASH(SHA256) 校验
# 哈希来源: 自 GitHub 官方 Release URL 下载实测 (2026-10); 上游更新库文件时需同步更新哈希
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
    HINTS "third_party/openssl/lib"
    NO_DEFAULT_PATH)
if(NOT EXISTS "third_party/openssl/lib")
    file(MAKE_DIRECTORY "third_party/openssl/lib")
    SET(LOCAL_OPENSSL_LIBRARIES_PATH "third_party/openssl/lib")
    MESSAGE(STATUS "Folder “third_party/openssl/lib” created successfully.")
endif()

# /////////////////////////////////////ssl库lib名称////////////////////////////////////////////////
SET(OPENSSL_SSL_NAME "libssl.lib")

# ssl远程下载地址
SET(SSL_DOWNLOAD_URL ${LIBSSL_URL})
MESSAGE(STATUS "Checking ${OPENSSL_SSL_NAME}...")
# 判断库文件是否已经存在
FIND_FILE(LOCAL_OPENSSL_SSL_LIB
    NAMES ${OPENSSL_SSL_NAME}
    HINTS "${PROJECT_SOURCE_DIR}/third_party/openssl/lib"
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
    HINTS "${PROJECT_SOURCE_DIR}/third_party/openssl/lib"
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
include_directories(third_party/openssl/include)

# 将源代码添加到此项目的可执行文件。
add_executable (GitHubDesktop2Chinese "src/GitHubDesktop2Chinese.cpp")


# 链接OpenSSL库  
target_link_libraries(GitHubDesktop2Chinese PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party/openssl/lib/${OPENSSL_SSL_NAME}
    ${CMAKE_SOURCE_DIR}/third_party/openssl/lib/${OPENSSL_CRYPTO_NAME})

# CLI11 已内置到 third_party/include/CLI（header-only，无需额外链接）

set_property(TARGET GitHubDesktop2Chinese PROPERTY CXX_STANDARD 20)
