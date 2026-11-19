# 解析并锁定 PPTrader 使用的 Qt 版本与安装前缀。
#
# 目的：由项目自己决定用哪套 Qt，而不是依赖全局环境变量（PATH / LD_LIBRARY_PATH /
# QML_IMPORT_PATH / Qt6_DIR 等）。机器上的发行版 Qt 会随滚动更新漂移，项目只认自己
# 安装的那一份：Linux 为 ~/ProgramData/Qt/<版本>/gcc_64，Windows 为
# <盘符>:/ProgramData/Qt/<版本>/msvc2022_64（Qt 在线安装器的默认落点）。
#
# 解析顺序（前者优先）：
#   1. -DPPTRADER_QT_ROOT=<前缀>（显式指定，完全信任，包括 /usr）
#   2. 项目约定安装位置（见下方候选列表），按 PPTRADER_QT_VERSION 精确匹配
#   3. 仅当 -DPPTRADER_QT_ALLOW_SYSTEM=ON 时，才把发行版 /usr 作为最后兜底
#
# 找不到时直接 FATAL_ERROR，不交给 CMake 默认搜索：默认搜索会命中 /usr 里的发行版
# Qt，构建出的二进制、平台插件与 QML 插件会随系统升级漂移，正是要避免的冲突。
#
# 解析完成后必须调用 pptrader_verify_qt_version()：实际 Qt 与 PPTRADER_QT_VERSION
# 的次版本号不一致、或实际使用的前缀不是本文件解析出的前缀时，直接配置失败。

# 2026-11-09 基线切换：默认目标由 Qt 6.11.2 改为 6.12.0。
# 原因：本机（Arch 滚动更新）发行版 Qt 已是 6.12.0，且 6.11.2 安装失败，
# 项目无法再用 6.11.2 配置（版本守卫会拒绝）。6.11.2 与 6.8.3 保留为可选工具链，
# 用 -DPPTRADER_QT_VERSION / 对应 preset 显式指定。各版本仍须各自 preset 与构建目录，
# 且 6.12.0 尚未跑完整验收（CTest/qmllint），不得据此宣称 6.12 已通过。
#
# 2026-11-09 起 6.12.0 固定使用在线安装器装到 ~/ProgramData/Qt 的副本，不再隐式使用
# 发行版 /usr 里的同名版本（Arch 会持续滚动到 6.13/6.14，项目不跟随）。
set(PPTRADER_QT_VERSION "6.12.0" CACHE STRING
    "PPTrader 目标 Qt 版本；默认 6.12.0，另可选 6.11.2 / 6.8.3（最低 6.5，6.4.3 已退役）")
set(PPTRADER_QT_ROOT "" CACHE PATH
    "显式指定 Qt 安装前缀；留空则按 PPTRADER_QT_VERSION 在项目约定位置解析")
option(PPTRADER_QT_ALLOW_SYSTEM
    "找不到项目安装的 Qt 时是否回退到发行版 Qt（Linux 的 /usr）；默认 OFF" OFF)

# 读取某个前缀下 Qt6 的版本号；目录不存在或无法解析时返回空字符串。
function(pptrader_qt_version_at prefix out)
    set(${out} "" PARENT_SCOPE)
    set(impl "${prefix}/lib/cmake/Qt6/Qt6ConfigVersionImpl.cmake")
    if(NOT EXISTS "${impl}")
        return()
    endif()
    file(STRINGS "${impl}" lines REGEX "^set\\(PACKAGE_VERSION ")
    if(NOT lines)
        return()
    endif()
    string(REGEX REPLACE "^set\\(PACKAGE_VERSION \"([^\"]+)\".*$" "\\1" version "${lines}")
    set(${out} "${version}" PARENT_SCOPE)
endfunction()

set(_pptrader_qt_prefix "")
set(_pptrader_qt_origin "")
set(_pptrader_qt_searched "")

if(PPTRADER_QT_ROOT)
    set(_pptrader_qt_prefix "${PPTRADER_QT_ROOT}")
    set(_pptrader_qt_origin "PPTRADER_QT_ROOT")
else()
    set(_pptrader_qt_candidates "")
    if(WIN32)
        # Qt 6.8+ 安装器把 MSVC 套件改名为 msvc2022_64，6.4/6.5 仍叫 msvc2019_64；
        # 两个都列出来，具体哪套存在由候选匹配决定。
        list(APPEND _pptrader_qt_candidates
            "D:/ProgramData/Qt/${PPTRADER_QT_VERSION}/msvc2022_64"
            "D:/ProgramData/Qt/${PPTRADER_QT_VERSION}/msvc2019_64"
            "C:/Qt/${PPTRADER_QT_VERSION}/msvc2022_64"
            "C:/Qt/${PPTRADER_QT_VERSION}/msvc2019_64"
            "$ENV{USERPROFILE}/ProgramData/Qt/${PPTRADER_QT_VERSION}/msvc2022_64"
            "$ENV{USERPROFILE}/Qt/${PPTRADER_QT_VERSION}/msvc2022_64")
    else()
        # 只认项目安装的副本。$ENV{HOME} 仅用于展开 ~/ProgramData（家目录基准），
        # 与 Qt 相关的环境变量（PATH / LD_LIBRARY_PATH / QML_IMPORT_PATH / QT_*）
        # 一律不参与解析。
        list(APPEND _pptrader_qt_candidates
            "$ENV{HOME}/ProgramData/Qt/${PPTRADER_QT_VERSION}/gcc_64"
            "$ENV{HOME}/Qt/${PPTRADER_QT_VERSION}/gcc_64"
            "/opt/Qt/${PPTRADER_QT_VERSION}/gcc_64")
    endif()
    if(PPTRADER_QT_ALLOW_SYSTEM)
        if(WIN32)
            list(APPEND _pptrader_qt_candidates
                "C:/Qt/${PPTRADER_QT_VERSION}/msvc2022_64")
        else()
            # 显式开启后才允许的兜底：发行版 Qt（Arch 的 qt6-* 包，前缀 /usr）。
            list(APPEND _pptrader_qt_candidates "/usr")
        endif()
    endif()
    foreach(_pptrader_candidate IN LISTS _pptrader_qt_candidates)
        list(APPEND _pptrader_qt_searched "${_pptrader_candidate}")
        pptrader_qt_version_at("${_pptrader_candidate}" _pptrader_found)
        if(_pptrader_found STREQUAL PPTRADER_QT_VERSION)
            set(_pptrader_qt_prefix "${_pptrader_candidate}")
            set(_pptrader_qt_origin "项目约定安装位置")
            break()
        endif()
    endforeach()
    if(NOT _pptrader_qt_prefix)
        string(REPLACE ";" "\n    - " _pptrader_qt_searched_text "${_pptrader_qt_searched}")
        message(FATAL_ERROR
            "PPTrader 未找到 Qt ${PPTRADER_QT_VERSION} 的项目安装副本。\n"
            "已查找：\n    - ${_pptrader_qt_searched_text}\n"
            "项目不使用系统发行版 Qt（/usr 会随 Arch 滚动更新漂移），也不读取 Qt 相关环境变量。\n"
            "请先安装匹配版本，或用 -DPPTRADER_QT_ROOT=<前缀> 指定安装位置；\n"
            "确实要用发行版 Qt 时显式加 -DPPTRADER_QT_ALLOW_SYSTEM=ON。")
    endif()
endif()

if(_pptrader_qt_prefix)
    # 缓存变量优先于 CMAKE_PREFIX_PATH，上一次 configure 留下的 Qt6_DIR / Qt6*_DIR
    # 会把构建拉回旧前缀（本机曾因此从系统 /usr 的 Qt 6.12.0 构建）。前缀不一致时
    # 清掉这些缓存项，让 find_package 只能落在本文件解析出的前缀上。
    if(Qt6_DIR)
        string(FIND "${Qt6_DIR}" "${_pptrader_qt_prefix}" _pptrader_qt_dir_pos)
        if(_pptrader_qt_dir_pos EQUAL -1)
            message(STATUS
                "PPTrader Qt：缓存的 Qt6_DIR=${Qt6_DIR} 与目标前缀不符，已忽略")
            get_cmake_property(_pptrader_cache_vars CACHE_VARIABLES)
            foreach(_pptrader_var IN LISTS _pptrader_cache_vars)
                if(_pptrader_var MATCHES "^Qt6.*_DIR$")
                    unset(${_pptrader_var} CACHE)
                    unset(${_pptrader_var})
                endif()
            endforeach()
        endif()
    endif()
    list(PREPEND CMAKE_PREFIX_PATH "${_pptrader_qt_prefix}")
    message(STATUS
        "PPTrader Qt：请求 ${PPTRADER_QT_VERSION}，使用前缀 ${_pptrader_qt_prefix}（${_pptrader_qt_origin}）")
endif()

function(pptrader_verify_qt_version)
    if(NOT Qt6_VERSION)
        return()
    endif()
    # 解析阶段已经限定了前缀，这里再确认 find_package 真的落在该前缀上：
    # CMAKE_PREFIX_PATH 里若还有别的条目（例如环境变量带进来的 /usr），不能让它生效。
    if(_pptrader_qt_prefix)
        string(FIND "${Qt6_DIR}" "${_pptrader_qt_prefix}" _pptrader_verify_pos)
        if(_pptrader_verify_pos EQUAL -1)
            message(FATAL_ERROR
                "PPTrader 解析出的 Qt 前缀是 ${_pptrader_qt_prefix}，但 find_package 实际用了 ${Qt6_DIR}。\n"
                "请删除构建目录后重新配置，或显式 -DPPTRADER_QT_ROOT=${_pptrader_qt_prefix}。")
        endif()
    endif()
    if(Qt6_VERSION VERSION_EQUAL PPTRADER_QT_VERSION)
        message(STATUS "PPTrader Qt：已确认 Qt ${Qt6_VERSION}（${Qt6_DIR}）")
        return()
    endif()
    string(REGEX REPLACE "^([0-9]+\\.[0-9]+).*$" "\\1" _want_series "${PPTRADER_QT_VERSION}")
    string(REGEX REPLACE "^([0-9]+\\.[0-9]+).*$" "\\1" _got_series "${Qt6_VERSION}")
    if(NOT _want_series STREQUAL _got_series)
        message(FATAL_ERROR
            "PPTrader 要求 Qt ${PPTRADER_QT_VERSION}，实际找到 Qt ${Qt6_VERSION}（${Qt6_DIR}）。\n"
            "Qt 的 QML 插件、平台插件与运行库不能跨次版本混装，请二选一：\n"
            "  - 改用对应 preset：vs2022-qt612 / vs2022-qt683 / linux-qt612-* / linux-qt683-*\n"
            "  - 显式指定：-DPPTRADER_QT_ROOT=<Qt 前缀> -DPPTRADER_QT_VERSION=<版本>")
    endif()
    # 同一系列内的补丁差异允许构建，但必须显式记录：APP-001 等按补丁版本取证的结论
    # 不能套用到另一个补丁版本。
    message(WARNING
        "PPTrader 请求 Qt ${PPTRADER_QT_VERSION}，实际是同一系列的 Qt ${Qt6_VERSION}；"
        "按补丁版本验收的结论（例如 APP-001）必须按实际版本重新取证。")
endfunction()
