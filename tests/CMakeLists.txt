add_executable(test_loungecat_core
    test_loungecat_core.cpp
)

target_link_libraries(test_loungecat_core PRIVATE
    loungecat_core
    Qt6::Test
)

add_test(NAME test_loungecat_core COMMAND test_loungecat_core)

add_executable(test_loungecat_repairs test_loungecat_repairs.cpp)
target_link_libraries(test_loungecat_repairs PRIVATE loungecat_ui Qt6::Test)
add_test(NAME test_loungecat_repairs COMMAND test_loungecat_repairs)
add_executable(test_irc_reliability test_irc_reliability.cpp)
target_link_libraries(test_irc_reliability PRIVATE loungecat_ui Qt6::Test)
add_test(NAME test_irc_reliability COMMAND test_irc_reliability)
set_tests_properties(test_loungecat_core test_loungecat_repairs PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 120
)
set_tests_properties(test_irc_reliability PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120)
