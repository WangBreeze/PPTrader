cmake_minimum_required(VERSION 3.24)
project(QtDeclarativeScrollBarFix LANGUAGES CXX C)

# 只在独立构建目录生成供应商源副本，不改已安装的 Qt 或用户源目录。
set(PPTRADER_QT_DECLARATIVE_SOURCE "" CACHE PATH "与运行库同版本的 QtDeclarative 源目录")
find_package(Qt6 6.4 CONFIG REQUIRED
    COMPONENTS BuildInternals Core Gui Qml QmlModels Quick)
if(Qt6_VERSION VERSION_GREATER_EQUAL "6.11")
    find_package(Qt6CorePrivate ${Qt6_VERSION} EXACT CONFIG REQUIRED)
    find_package(Qt6GuiPrivate ${Qt6_VERSION} EXACT CONFIG REQUIRED)
    find_package(Qt6QmlPrivate ${Qt6_VERSION} EXACT CONFIG REQUIRED)
    find_package(Qt6QuickPrivate ${Qt6_VERSION} EXACT CONFIG REQUIRED)
    find_package(Qt6QuickTemplates2Private ${Qt6_VERSION} EXACT CONFIG REQUIRED)
endif()
set(PROJECT_VERSION "${Qt6_VERSION}")
set(PROJECT_VERSION_MAJOR "${Qt6_VERSION_MAJOR}")
set(PROJECT_VERSION_MINOR "${Qt6_VERSION_MINOR}")
set(PROJECT_VERSION_PATCH "${Qt6_VERSION_PATCH}")

if(Qt6_VERSION VERSION_EQUAL "6.4.3")
    set(quicktemplates_source_dir
        "${PPTRADER_QT_DECLARATIVE_SOURCE}/src/quicktemplates2")
    set(expected_source_sha256
        "17797d8bcfc797ec5bf45d16fa64ce917a5459c5ac89ec411c8be8b665925b3e")
elseif(Qt6_VERSION VERSION_EQUAL "6.11.2")
    set(quicktemplates_source_dir
        "${PPTRADER_QT_DECLARATIVE_SOURCE}/src/quicktemplates")
    set(expected_source_sha256
        "64b4c06235f32c52857b570862289dc3ef95aeaa138563d1f3de70dc30e5340d")
else()
    message(FATAL_ERROR
        "Qt ${Qt6_VERSION} 尚未审计，滚动条修复配方只接受 6.4.3 或 6.11.2")
endif()
if(NOT EXISTS "${quicktemplates_source_dir}/qquickscrollbar.cpp")
    message(FATAL_ERROR "需要与 Qt ${Qt6_VERSION} SDK 匹配的 QtDeclarative 源目录")
endif()
file(SHA256 "${quicktemplates_source_dir}/qquickscrollbar.cpp" source_sha256)
if(NOT source_sha256 STREQUAL expected_source_sha256)
    message(FATAL_ERROR
        "滚动条源文件不是已审计的 Qt ${Qt6_VERSION} 原件；需重新审计，禁止套用补丁")
endif()
set(QT_BUILD_TESTS OFF)
set(QT_BUILD_EXAMPLES OFF)
set(QT_BUILD_BENCHMARKS OFF)
qt_build_repo_begin()

set(patched_source "${CMAKE_CURRENT_BINARY_DIR}/vendor/quicktemplates2")
file(COPY "${quicktemplates_source_dir}/"
    DESTINATION "${patched_source}")
if(Qt6_VERSION VERSION_GREATER_EQUAL "6.11")
    # SDK 的 lean-headers 构建不会通过 qqmlregistration.h 间接暴露
    # QQmlParserStatus；独立重建时为 moc 补齐其直接声明头。
    set(context_menu_header "${patched_source}/qquickcontextmenu_p.h")
    file(READ "${context_menu_header}" context_menu_source)
    string(REPLACE "#include <QtQml/qqmlregistration.h>"
        "#include <QtQml/qqmlregistration.h>\n#include <QtQml/qqmlparserstatus.h>"
        context_menu_source "${context_menu_source}")
    file(WRITE "${context_menu_header}" "${context_menu_source}")
endif()
file(READ "${patched_source}/qquickscrollbar.cpp" scrollbar_source)
foreach(action IN ITEMS increase decrease)
    set(original "            connect(accessibleAttached, &QQuickAccessibleAttached::${action}Action, this, &QQuickScrollBar::${action});")
    string(FIND "${scrollbar_source}" "${original}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR
            "供应商源与审计的 Qt ${Qt6_VERSION} 连接形态不一致，拒绝静默修改")
    endif()
    string(REPLACE "${original}"
        "            connect(accessibleAttached, &QQuickAccessibleAttached::${action}Action, this, &QQuickScrollBar::${action}, Qt::UniqueConnection);"
        scrollbar_source "${scrollbar_source}")
endforeach()
file(WRITE "${patched_source}/qquickscrollbar.cpp" "${scrollbar_source}")
# SDK 已导入官方插件目标；这里只构建 ABI 相同的库，测试仍使用官方插件。
file(READ "${patched_source}/CMakeLists.txt" module_cmake)
set(module_target QuickTemplates2)
if(Qt6_VERSION VERSION_GREATER_EQUAL "6.11")
    # Qt 6.11 的 QtQuick 导入链已经定义 Qt::QuickTemplates2；用独立的
    # CMake 目标重建同名运行库，避免与 SDK 的 imported target 冲突。
    set(module_target PPTraderQuickTemplates2)
    string(REPLACE "qt_internal_add_qml_module(QuickTemplates2"
        "qt_internal_add_qml_module(PPTraderQuickTemplates2\n    CONFIG_MODULE_NAME QuickTemplates2"
        module_cmake "${module_cmake}")
    string(REPLACE "qt_internal_extend_target(QuickTemplates2"
        "qt_internal_extend_target(PPTraderQuickTemplates2"
        module_cmake "${module_cmake}")
endif()
string(REPLACE "PLUGIN_TARGET qtquicktemplates2plugin" "PLUGIN_TARGET pptrader_qtquicktemplates2plugin"
    module_cmake "${module_cmake}")
string(REPLACE "qt_internal_extend_Target(qtquicktemplates2plugin" "qt_internal_extend_Target(pptrader_qtquicktemplates2plugin"
    module_cmake "${module_cmake}")
if(Qt6_VERSION VERSION_GREATER_EQUAL "6.11")
    string(APPEND module_cmake
        "\nset_target_properties(PPTraderQuickTemplates2 PROPERTIES OUTPUT_NAME Qt6QuickTemplates2)\n")
endif()
string(APPEND module_cmake "\nadd_custom_command(TARGET ${module_target} POST_BUILD\n"
    "  COMMAND \"${CMAKE_COMMAND}\" \"-DRUNTIME_FILE=$<TARGET_FILE:${module_target}>\"\n"
    "    \"-DRUNTIME_CONFIG=$<CONFIG>\" \"-DQT_VERSION=${Qt6_VERSION}\"\n"
    "    \"-DSOURCE_SHA256=${source_sha256}\"\n"
    "    -P \"${CMAKE_CURRENT_SOURCE_DIR}/WriteManifest.cmake\" VERBATIM)\n")
file(WRITE "${patched_source}/CMakeLists.txt" "${module_cmake}")
add_subdirectory("${patched_source}" quicktemplates2)
if(Qt6_VERSION VERSION_GREATER_EQUAL "6.11")
    get_target_property(qt_quicktemplates_private_includes
        Qt6::QuickTemplates2Private INTERFACE_INCLUDE_DIRECTORIES)
    target_include_directories(${module_target} PRIVATE
        ${qt_quicktemplates_private_includes})
endif()
if(MSVC)
    target_compile_options(${module_target} PRIVATE "$<$<CONFIG:Debug>:/Od>")
endif()
qt_build_repo_end()
