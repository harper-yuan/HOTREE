add_library(HOTREE
    tree.cpp
    Branch.cpp
    DataReader.cpp
    cryptor.cpp
    client.cpp
    OHT.cpp
    hotree.cpp)
target_include_directories(HOTREE PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(HOTREE PUBLIC OpenMP::OpenMP_CXX)
