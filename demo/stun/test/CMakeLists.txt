# See LICENSE file in the project root for license information.

# process tests

add_test_target(${PROJECT_NAME}-test-stun-parsing test_stun_parsing.cpp)
target_link_libraries(${PROJECT_NAME}-test-stun-parsing ${PROJECT_NAME}::stun)
