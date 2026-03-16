cmake_minimum_required(VERSION 3.10)
project(MempoolMonitor)

# Use modern C++
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find the packages vcpkg is downloading
find_package(nlohmann_json CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Boost REQUIRED COMPONENTS system)

# Create the executable
add_executable(MempoolMonitor mempool_monitor.cpp)

# Link the libraries to your code
target_link_libraries(MempoolMonitor PRIVATE
    nlohmann_json::nlohmann_json
    OpenSSL::SSL
    OpenSSL::Crypto
    Boost::system
)