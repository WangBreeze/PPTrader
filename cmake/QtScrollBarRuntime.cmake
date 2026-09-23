# 已复现缺陷的6.4.3 Windows构建默认要求受控修复库；其他Qt版本必须另行验收。
set(scrollbar_fix_default OFF)
if(WIN32 AND (Qt6_VERSION VERSION_EQUAL "6.4.3"
        OR Qt6_VERSION VERSION_EQUAL "6.11.2"))
    set(scrollbar_fix_default ON)
endif()
option(PPTRADER_REQUIRE_QT_SCROLLBAR_FIX "构建时校验并部署APP-001滚动条修复库" ${scrollbar_fix_default})
set(PPTRADER_QT_SCROLLBAR_FIX_DIR
    "${CMAKE_SOURCE_DIR}/out/build/qt-scrollbar-fix-${Qt6_VERSION}/bin"
    CACHE PATH "同版本受控Qt滚动条库及清单所在目录")

if(PPTRADER_REQUIRE_QT_SCROLLBAR_FIX)
    if(NOT WIN32 OR NOT (Qt6_VERSION VERSION_EQUAL "6.4.3"
            OR Qt6_VERSION VERSION_EQUAL "6.11.2"))
        message(FATAL_ERROR
            "当前修复配方仅验收 Qt 6.4.3/6.11.2 Windows，不能混用到其他版本")
    endif()
    add_custom_target(pptrader_qt_scrollbar_runtime ALL
        COMMAND ${CMAKE_COMMAND}
            "-DFIX_DIR=${PPTRADER_QT_SCROLLBAR_FIX_DIR}"
            "-DTARGET_DIR=$<TARGET_FILE_DIR:PPTrader>"
            "-DQT_VERSION=${Qt6_VERSION}" "-DCONFIG=$<CONFIG>"
            -P "${CMAKE_CURRENT_LIST_DIR}/qt-scrollbar-fix/DeployRuntime.cmake"
        VERBATIM)
    foreach(gui_target IN ITEMS PPTrader tst_accessible_scrollbar_lifecycle
            tst_application_exit tst_custom_strategy_wizard_qml
            tst_strategy_plugin_panel_qml tst_backtest_archive_panel_qml)
        if(TARGET ${gui_target})
            add_dependencies(${gui_target} pptrader_qt_scrollbar_runtime)
        endif()
    endforeach()
else()
    message(WARNING "未启用APP-001运行库保护；退出与无障碍回归必须通过，不能据此宣称已修复")
endif()
