cmake_minimum_required(VERSION 3.10)
project(ProfessionalInstaller)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTKMM REQUIRED IMPORTED_TARGET gtkmm-4.0)

add_executable(installer main.cpp)
target_link_libraries(installer PkgConfig::GTKMM stdc++fs)
