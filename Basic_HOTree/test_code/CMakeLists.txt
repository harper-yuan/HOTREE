set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/test_code)

macro(add_unit_test TEST_NAME SOURCE_FILE)
    add_executable(${TEST_NAME} ${SOURCE_FILE})
    target_link_libraries(${TEST_NAME} Boost::unit_test_framework HOTREE cryptopp)
endmacro()

# If you want to add test to dictionary "test_code"，add your test name and filename to following function
set(TEST_LIST
    plaintext_test:plaintext_test.cpp
    cryptor_test:cryptor_test.cpp
    hash_test:hash_test.cpp
    ciphertext_test:ciphertext_test.cpp
)

add_custom_target(test_code)

get_filename_component(BUILD_DIR_ROOT "${CMAKE_CURRENT_BINARY_DIR}" DIRECTORY)
set_property(TEST PROPERTY WORKING_DIRECTORY "${BUILD_DIR_ROOT}")

foreach(TEST_ITEM ${TEST_LIST})
    string(REPLACE ":" ";" TEST_PARTS ${TEST_ITEM})
    list(GET TEST_PARTS 0 TEST_NAME)
    list(GET TEST_PARTS 1 SOURCE_FILE)
    
    add_executable(${TEST_NAME} ${SOURCE_FILE})
    target_link_libraries(${TEST_NAME} Boost::unit_test_framework HOTREE cryptopp)
    
    add_dependencies(test_code ${TEST_NAME})
    
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME} WORKING_DIRECTORY "${BUILD_DIR_ROOT}")
endforeach()