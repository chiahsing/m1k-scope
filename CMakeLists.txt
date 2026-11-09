cmake_minimum_required(VERSION 3.16)

project(m1k_scope VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUSB_1 REQUIRED libusb-1.0)

add_executable(m1k_scope
    main.cc
    resources/resources.qrc
    core/bode_sweep.cc
    core/device.cc
    core/frame_trigger.cc
    core/generator.cc
    ui/bode_panel.cc
    ui/bode_plot_widget.cc
    ui/common.cc
    ui/function_generator_panel.cc
    ui/main_window.cc
    ui/oscilloscope_panel.cc
    ui/plot_widget.cc
    ui/realtime_panel.cc
    ui/waveform_widget.cc
)

target_compile_options(m1k_scope PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Werror
)

target_include_directories(m1k_scope PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${LIBUSB_1_INCLUDE_DIRS}
)

target_link_libraries(m1k_scope PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    smu
    ${LIBUSB_1_LIBRARIES}
)
