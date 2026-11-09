# 解析并锁定 PPTrader 使用的 Qt 版本与安装前缀。
#
# 目的：由项目自己决定用哪套 Qt，而不是依赖全局环境变量。这样 Qt 6.8.3 可以作为
# 项目内可选工具链与 Qt 6.11.2 并存，也不会污染机器上其它程序的 Qt。
#
# 覆盖顺序（前者优先）：
#   1. -DPPTRADER_QT_ROOT=<前缀>
#   2. 调用方显式给出的 -DCMAKE_PREFIX_PATH / -DQt6_DIR
#   3. 按 PPTRADER_QT_VERSION 在平台约定位置自动查找
#
# 解析完成后必须调用 pptrader_verify_qt_version()：实际 Qt 与 PPTRADER_QT_VERSION
# 的次版本号不一致时直接配置失败。Qt 的 QML 插件、平台插件与运行库不能跨次版本
# 混装，静默用错版本会得到难排查的加载期故障。

set(PPTRADER_QT_VERSION "6.11.2" CACHE STRING
    "PPTrader 目标 Qt 版本；默认 6.11.2，另支持 6.8.3（最低 6.5，6.4.3 已退役）")
set(PPTRADER_QT_ROOT "" CACHE PATH
    "显式指定 Qt 安装前缀；留空则按 PPTRADER_QT_VERSION 自动解析")

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
if(PPTRADER_QT_ROOT)
    set(_pptrader_qt_prefix "${PPTRADER_QT_ROOT}")
    set(_pptrader_qt_origin "PPTRADER_QT_ROOT")
elseif(CMAKE_PREFIX_PATH OR Qt6_DIR)
    # 调用方（preset、命令行，或上一次 configure 留下的 Qt6_DIR 缓存）已经指明了位置，
    # 不要再去猜，也不要重复追加 CMAKE_PREFIX_PATH。
    set(_pptrader_qt_origin "调用方已指定 CMAKE_PREFIX_PATH/Qt6_DIR")
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
            "$ENV{USERPROFILE}/ProgramData/Qt/${PPTRADER_QT_VERSION}/msvc2022_64")
    else()
        # 发行版 Qt（Arch 的 qt6-* 包）前缀就是 /usr；只有版本完全匹配才会被选中，
        # 因此 /usr 排在自定义安装之后，避免抢走项目指定的 6.8.3。
        list(APPEND _pptrader_qt_candidates
            "$ENV{HOME}/ProgramData/Qt/${PPTRADER_QT_VERSION}/gcc_64"
            "/opt/Qt/${PPTRADER_QT_VERSION}/gcc_64"
            "/usr")
    endif()
    foreach(_candidate IN LISTS _pptrader_qt_candidates)
        pptrader_qt_version_at("${_candidate}" _found)
        if(_found STREQUAL PPTRADER_QT_VERSION)
            set(_pptrader_qt_prefix "${_candidate}")
            break()
        endif()
    endforeach()
    if(_pptrader_qt_prefix)
        set(_pptrader_qt_origin "按平台约定位置自动解析")
    endif()
endif()

if(_pptrader_qt_prefix)
    list(PREPEND CMAKE_PREFIX_PATH "${_pptrader_qt_prefix}")
    message(STATUS
        "PPTrader Qt：请求 ${PPTRADER_QT_VERSION}，使用前缀 ${_pptrader_qt_prefix}（${_pptrader_qt_origin}）")
elseif(_pptrader_qt_origin)
    message(STATUS "PPTrader Qt：请求 ${PPTRADER_QT_VERSION}，沿用${_pptrader_qt_origin}")
else()
    message(WARNING
        "PPTrader Qt：未按 Qt ${PPTRADER_QT_VERSION} 找到安装前缀，交给 CMake 默认搜索；"
        "如搜到的版本不符会在 find_package 之后直接失败。可用 -DPPTRADER_QT_ROOT=<前缀> 指定。")
endif()

function(pptrader_verify_qt_version)
    if(NOT Qt6_VERSION)
        return()
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
            "  - 改用对应 preset：vs2022-qt611 / vs2022-qt683 / linux-qt611-* / linux-qt683-*\n"
            "  - 显式指定：-DPPTRADER_QT_ROOT=<Qt 前缀> -DPPTRADER_QT_VERSION=<版本>")
    endif()
    # 同一系列内的补丁差异允许构建，但必须显式记录：APP-001 等按补丁版本取证的结论
    # 不能套用到另一个补丁版本。
    message(WARNING
        "PPTrader 请求 Qt ${PPTRADER_QT_VERSION}，实际是同一系列的 Qt ${Qt6_VERSION}；"
        "按补丁版本验收的结论（例如 APP-001）必须按实际版本重新取证。")
endfunction()
