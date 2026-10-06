cmake_minimum_required(VERSION 3.15)
project(GamePreloader CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W3 /utf-8 /EHsc)
    add_compile_definitions(UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

# ---------------------------------------------------------------
# V1.8: UE5 解析库 -> park/UE5PakParser.dll（专门放置引擎解析库）
#   导出 PakParser 类（编译 DLL 时定义 GAMEPAK_EXPORTS）
# ---------------------------------------------------------------
add_library(UE5PakParser SHARED
    src/PakParser.cpp
    src/Common.cpp
)
target_compile_definitions(UE5PakParser PRIVATE GAMEPAK_EXPORTS)
target_link_libraries(UE5PakParser PRIVATE shell32 dxgi)
set_target_properties(UE5PakParser PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/park"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/park"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    OUTPUT_NAME "UE5PakParser"
)

# ---------------------------------------------------------------
# 主程序（不再编译 PakParser.cpp，解析符号来自 UE5PakParser 导入库）
# ---------------------------------------------------------------
add_executable(GamePreloader WIN32
    src/main.cpp
    src/Common.cpp
    src/Config.cpp
    src/ProcessMonitor.cpp
    src/GameDetector.cpp
    src/Preprocessor.cpp
    src/FloatingWindow.cpp
    app.rc
)

target_link_libraries(GamePreloader PRIVATE
    UE5PakParser        # 解析库导入库
    delayimp            # delay-load 支持
    psapi shell32 user32 gdi32 advapi32 ole32 comdlg32 powrprof dxgi
)

# 延迟加载解析 DLL：启动时不立即依赖，首次调用时加载（配合 SetDllDirectory(park) 与全路径预载）
if(MSVC)
    target_link_options(GamePreloader PRIVATE "/DELAYLOAD:UE5PakParser.dll")
endif()

set_target_properties(GamePreloader PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
