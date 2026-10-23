# Run independently with:
# cmake -DTEST_BINARY_DIR=<build-directory> -P tests/cmake/qt-selection.cmake
cmake_minimum_required(VERSION 3.16)

if(NOT TEST_BINARY_DIR)
    message(FATAL_ERROR "Set TEST_BINARY_DIR to an out-of-source test directory")
endif()
get_filename_component(TEST_BINARY_DIR "${TEST_BINARY_DIR}" ABSOLUTE)
set(_prefix "${TEST_BINARY_DIR}/packages")

foreach(_major 5 6)
    set(_package_dir "${_prefix}/qt${_major}/lib/cmake/Qt${_major}")
    file(MAKE_DIRECTORY "${_package_dir}")
    file(WRITE "${_package_dir}/Qt${_major}Config.cmake"
        "set(Qt${_major}_FOUND TRUE)\n"
        "set(QT_TEST_PREFIX_${_major} \"\${CMAKE_PREFIX_PATH}\")\n"
        "if(MISSING_QT${_major}_COMPONENT)\n"
        "  set(Qt${_major}_FOUND FALSE)\n"
        "  set(Qt${_major}_NOT_FOUND_MESSAGE \"Missing OpenGL component\")\n"
        "endif()\n")
    set(_tools_dir "${_prefix}/qt${_major}/lib/cmake/Qt${_major}LinguistTools")
    file(MAKE_DIRECTORY "${_tools_dir}")
    file(WRITE "${_tools_dir}/Qt${_major}LinguistToolsConfig.cmake"
        "set(Qt${_major}LinguistTools_FOUND TRUE)\n")
    file(COPY "${_prefix}/qt${_major}/" DESTINATION "${_prefix}/qt${_major}-static")
endforeach()
set(ENV{MSYSTEM_PREFIX} "${_prefix}")

function(check_selection name expected)
    set(_build_dir "${TEST_BINARY_DIR}/${name}")
    set(_expected_override OFF)
    foreach(_arg IN LISTS ARGN)
        if(_arg STREQUAL "-DUSE_QT6=ON")
            set(_expected_override ON)
        endif()
    endforeach()
    execute_process(
        COMMAND "${CMAKE_COMMAND}"
            -S "${CMAKE_CURRENT_LIST_DIR}/qt-selection" -B "${_build_dir}"
            "-DTEST_PREFIX=${_prefix}"
            "-DEXPECTED_MAJOR=${expected}"
            "-DEXPECTED_OVERRIDE=${_expected_override}"
            ${ARGN}
        RESULT_VARIABLE _result OUTPUT_VARIABLE _stdout ERROR_VARIABLE _stderr)
    if(expected STREQUAL "failure")
        if(_result EQUAL 0 OR NOT "${_stdout}${_stderr}" MATCHES "No usable Qt installation found")
            message(FATAL_ERROR "${name}: expected a Qt discovery error\n${_stdout}\n${_stderr}")
        endif()
    elseif(NOT _result EQUAL 0)
        message(FATAL_ERROR "${name}: configure failed\n${_stdout}\n${_stderr}")
    endif()
    message(STATUS "${name}: passed")
endfunction()

check_selection(qt5-only 5 -DCMAKE_DISABLE_FIND_PACKAGE_Qt6=TRUE -DCMAKE_DISABLE_FIND_PACKAGE_Qt6LinguistTools=TRUE)
check_selection(qt6-only 6 -DCMAKE_DISABLE_FIND_PACKAGE_Qt5=TRUE -DCMAKE_DISABLE_FIND_PACKAGE_Qt5LinguistTools=TRUE)
check_selection(both 5)
check_selection(force-qt6 6 -DUSE_QT6=ON)
check_selection(force-qt6-unavailable failure -DUSE_QT6=ON -DCMAKE_DISABLE_FIND_PACKAGE_Qt6=TRUE)
check_selection(neither failure -DCMAKE_DISABLE_FIND_PACKAGE_Qt5=TRUE -DCMAKE_DISABLE_FIND_PACKAGE_Qt6=TRUE)
check_selection(incomplete-qt5 6 -DMISSING_QT5_COMPONENT=TRUE)
check_selection(missing-qt5-tools 6 -DCMAKE_DISABLE_FIND_PACKAGE_Qt5LinguistTools=TRUE)
check_selection(incomplete-both failure -DMISSING_QT5_COMPONENT=TRUE -DMISSING_QT6_COMPONENT=TRUE)
check_selection(missing-both-tools failure -DCMAKE_DISABLE_FIND_PACKAGE_Qt5LinguistTools=TRUE -DCMAKE_DISABLE_FIND_PACKAGE_Qt6LinguistTools=TRUE)
check_selection(explicit-off 6 -DUSE_QT6=OFF -DCMAKE_DISABLE_FIND_PACKAGE_Qt5=TRUE)
check_selection(reconfigure 6 -DCMAKE_DISABLE_FIND_PACKAGE_Qt5=TRUE)
check_selection(reconfigure 5 -DCMAKE_DISABLE_FIND_PACKAGE_Qt5=FALSE)
check_selection(disabled 0 -DQT=OFF -DCMAKE_DISABLE_FIND_PACKAGE_Qt5=TRUE -DCMAKE_DISABLE_FIND_PACKAGE_Qt6=TRUE)
check_selection(vcpkg-qt5 5 -DVCPKG_TOOLCHAIN=TRUE -DVCPKG_USE_HOST_TOOLS=TRUE "-DVCPKG_INSTALLED_DIR=${_prefix}" -DVCPKG_HOST_TRIPLET=test-host)
check_selection(vcpkg-qt6 6 -DVCPKG_TOOLCHAIN=TRUE -DVCPKG_USE_HOST_TOOLS=TRUE "-DVCPKG_INSTALLED_DIR=${_prefix}" -DVCPKG_HOST_TRIPLET=test-host -DCMAKE_DISABLE_FIND_PACKAGE_Qt5=TRUE)
check_selection(apple-qt5 5 -DAPPLE=TRUE)
check_selection(apple-qt6 6 -DAPPLE=TRUE -DCMAKE_DISABLE_FIND_PACKAGE_Qt5=TRUE)
check_selection(apple-moltenvk 5 -DAPPLE=TRUE -DMOLTENVK=TRUE)
check_selection(msys-static-qt5 5 -DMINGW=TRUE -DSTATIC_BUILD=ON)
check_selection(msys-static-qt6 6 -DMINGW=TRUE -DSTATIC_BUILD=ON -DCMAKE_DISABLE_FIND_PACKAGE_Qt5=TRUE)
check_selection(msys-dynamic-qt6 6 -DMINGW=TRUE -DSTATIC_BUILD=ON -DQT6_FORCE_DYNAMIC=ON -DCMAKE_DISABLE_FIND_PACKAGE_Qt5=TRUE)
