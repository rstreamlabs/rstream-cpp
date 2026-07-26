# See LICENSE file in the project root for license information.

# process tests

add_test_target(${PROJECT_NAME}-test-io-http-upgrade test_io_http_upgrade.cpp ${PROJECT_NAME}::io)
