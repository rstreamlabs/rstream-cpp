# See LICENSE file in the project root for license information.

install(DIRECTORY ${PROJECT_NAME} DESTINATION ${CMAKE_INSTALL_Python3_SITEPACKAGES} FILES_MATCHING REGEX ".*\\.py" PATTERN "__pycache__" EXCLUDE)
