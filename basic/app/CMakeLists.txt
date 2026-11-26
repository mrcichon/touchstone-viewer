set(third_party ${CMAKE_SOURCE_DIR}/third_party)
set(imgui ${third_party}/imgui)
set(implot ${third_party}/implot)

if(APPLE)
  enable_language(OBJC OBJCXX)  # OBJC for GLFW, OBJCXX for platform_mac.mm
endif()
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(${third_party}/glfw ${CMAKE_BINARY_DIR}/glfw EXCLUDE_FROM_ALL)

if(WIN32)
  set(platform_source platform_windows.cpp graphics_d3d11.cpp line_renderer_d3d11.cpp windows.rc)
  set(imgui_renderer ${imgui}/backends/imgui_impl_dx11.cpp)
  set(platform_libraries d3d11 dxgi d3dcompiler ole32 uuid shell32 psapi)
elseif(APPLE)
  set(platform_source platform_mac.mm graphics_gl.cpp line_renderer_gl.cpp)
  set(imgui_renderer ${imgui}/backends/imgui_impl_opengl3.cpp)
  set(platform_libraries "-framework OpenGL")
else()
  message(FATAL_ERROR "The window program is for Windows and macOS. Use -DCORE_ONLY=ON for the numerical code and its tests.")
endif()

add_library(gui_libraries STATIC
  ${imgui}/imgui.cpp ${imgui}/imgui_draw.cpp ${imgui}/imgui_tables.cpp ${imgui}/imgui_widgets.cpp
  ${imgui}/misc/cpp/imgui_stdlib.cpp
  ${imgui}/backends/imgui_impl_glfw.cpp ${imgui_renderer}
  ${implot}/implot.cpp ${implot}/implot_items.cpp
  stb_image_write.cpp)
target_include_directories(gui_libraries SYSTEM PUBLIC ${imgui} ${imgui}/backends ${implot} ${third_party}/stb ${third_party}/glfw/include)
# The default 16-bit indices permit only 65k vertices in one draw call.
target_compile_definitions(gui_libraries PUBLIC "ImDrawIdx=unsigned int")
target_link_libraries(gui_libraries PUBLIC glfw ${platform_libraries})
target_compile_options(gui_libraries PRIVATE -w)

add_executable(touchstone_viewer
  main.cpp session.cpp palette.cpp plot_curves.cpp legend.cpp file_panel.cpp panel_edge.cpp
  frequency_view.cpp time_view.cpp log.cpp order_highlight.cpp session_file.cpp ${platform_source}
  ${CMAKE_SOURCE_DIR}/tests/window/play_script.cpp ${CMAKE_SOURCE_DIR}/tests/window/stress_test.cpp)
target_include_directories(touchstone_viewer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/tests/window)
target_link_libraries(touchstone_viewer PRIVATE core gui_libraries)
use_strict_flags(touchstone_viewer)
execute_process(COMMAND git log -1 --format=%cd-%h --date=format:%Y.%m.%d WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE viewer_version OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT viewer_version)
  set(viewer_version "unknown")
endif()
set_source_files_properties(main.cpp PROPERTIES COMPILE_DEFINITIONS "VIEWER_VERSION=\"${viewer_version}\"")
if(MINGW)
  # No DLLs of the compiler, no console window. The debug information goes to a second file, for addr2line.
  target_compile_options(touchstone_viewer PRIVATE -g)
  target_link_options(touchstone_viewer PRIVATE -static -mwindows)
  add_custom_command(TARGET touchstone_viewer POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} --only-keep-debug $<TARGET_FILE:touchstone_viewer> $<TARGET_FILE:touchstone_viewer>.debug
    COMMAND ${CMAKE_STRIP} $<TARGET_FILE:touchstone_viewer> VERBATIM)
elseif(APPLE)
  target_link_options(touchstone_viewer PRIVATE -Wl,-exported_symbol,_main)
  add_custom_command(TARGET touchstone_viewer POST_BUILD
    COMMAND strip -x $<TARGET_FILE:touchstone_viewer> VERBATIM)
endif()

# These need a display.
add_test(NAME window_legend_click
  COMMAND touchstone_viewer ${CMAKE_SOURCE_DIR}/tests/reference/sample.s2p --size 1500x900 --play ${CMAKE_SOURCE_DIR}/tests/window/legend_click.txt)
add_test(NAME window_markers
  COMMAND touchstone_viewer ${CMAKE_SOURCE_DIR}/tests/reference/sample.s2p --size 1500x900 --play ${CMAKE_SOURCE_DIR}/tests/window/markers.txt)
add_test(NAME window_session_save WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  COMMAND touchstone_viewer ${CMAKE_SOURCE_DIR}/tests/reference/sample.s2p --size 1500x900 --play ${CMAKE_SOURCE_DIR}/tests/window/session_save.txt)
add_test(NAME window_session_open WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  COMMAND touchstone_viewer test.tvsession --size 1500x900 --play ${CMAKE_SOURCE_DIR}/tests/window/session_open.txt)
set_tests_properties(window_session_open PROPERTIES DEPENDS window_session_save)
add_test(NAME window_short_names
  COMMAND touchstone_viewer ${CMAKE_SOURCE_DIR}/tests/reference/short_names --size 1500x900 --play ${CMAKE_SOURCE_DIR}/tests/window/short_names.txt)
