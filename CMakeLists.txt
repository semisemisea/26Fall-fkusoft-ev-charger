## @file CMakeLists.txt
# @brief 统一配置充电业务后端、普通用户前端和管理员前端的构建。
# @details 三个模块共用 C++20、Qt 自动代码生成和精确的 Qt 6.2.4 依赖。
# 启用 BUILD_TESTING 时，各模块注册自己的 CTest 测试；应用程序由各自的
# 子目录声明安装规则，安装路径使用 GNUInstallDirs 提供的平台约定。
cmake_minimum_required(VERSION 3.16)

project(fkusoft-ev-charger VERSION 0.1.0 LANGUAGES CXX)

# @brief 自动处理 QObject 元对象、Qt 资源文件和 Designer 界面文件。
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# @brief 所有目标必须支持标准 C++20，不依赖编译器专有语言扩展。
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# @brief 核心组件必须齐全且版本恰为 6.2.4；图表和网页组件允许缺省。
# @details 前端通过 Qt6::Charts / Qt6::WebEngineWidgets 目标是否存在，
# 选择增强界面或其已有的降级实现，因此这些可选组件不阻止基本构建。
find_package(Qt6 6.2.4 EXACT REQUIRED COMPONENTS
    Core
    Network
    Sql
    Svg
    Widgets
    OPTIONAL_COMPONENTS Charts WebEngineWidgets
)

include(GNUInstallDirs)
include(CTest)

# @brief 先声明核心目标，再声明两个前端；具体依赖由各模块显式链接。
add_subdirectory(biz-core)
add_subdirectory(user-app)
add_subdirectory(ops-app)
