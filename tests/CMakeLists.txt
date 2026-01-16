add_executable(json_tests
	parser_tests.cpp
)

target_link_libraries(json_tests PRIVATE json)

add_test(NAME json_tests COMMAND json_tests)
