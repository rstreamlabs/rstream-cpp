# See LICENSE file in the project root for license information.

# executables

add_executable(${PROJECT_NAME}-example-external-boost-url-sanitizer example_external_boost_url_sanitizer.cpp)
target_link_libraries(${PROJECT_NAME}-example-external-boost-url-sanitizer Boost::url)
