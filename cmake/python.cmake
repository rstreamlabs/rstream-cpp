# See LICENSE file in the project root for license information.

# process python stdlib / site package path

execute_process(
  COMMAND ${Python3_EXECUTABLE} -c
    "import sys\nfrom distutils import sysconfig\nsys.stdout.write(sysconfig.get_python_lib(plat_specific=False,standard_lib=True,prefix=\"${CMAKE_INSTALL_PREFIX}\"))\n"
  OUTPUT_VARIABLE CMAKE_INSTALL_Python3)

set(CMAKE_INSTALL_Python3_STDLIB "${CMAKE_INSTALL_Python3}/stdlib")

execute_process(
  COMMAND ${Python3_EXECUTABLE} -c
    "import sys\nfrom distutils import sysconfig\nsys.stdout.write(sysconfig.get_python_lib(plat_specific=False,standard_lib=False,prefix=\"${CMAKE_INSTALL_PREFIX}\"))\n"
  OUTPUT_VARIABLE CMAKE_INSTALL_Python3_SITEPACKAGES)

if(PYTHON_INSTALL_DEPENDENCIES)
  install(CODE "execute_process(COMMAND ${Python3_EXECUTABLE} -m pip -v --no-input install -t ${CMAKE_INSTALL_Python3_SITEPACKAGES}/${PROJECT_NAME}/deps -r ${PROJECT_SOURCE_DIR}/requirements.txt)")
endif()
  
if(PYTHON_INSTALL_STDLIB)
  install(DIRECTORY "${Python3_STDLIB}/" DESTINATION ${CMAKE_INSTALL_Python3_STDLIB} PATTERN "__pycache__" EXCLUDE REGEX "site-packages" EXCLUDE REGEX "config-.*" EXCLUDE)
endif()
