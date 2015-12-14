# -*- mode: cmake -*-

######################
# Find Elemental
######################

if(ENABLE_ELEMENTAL AND DEFINED ELEMENTAL_TAG)

  include(ExternalProject)
  include(ConvertIncludesListToCompilerArgs)
  include(ConvertLibrariesListToCompilerArgs)

  find_package(Git REQUIRED)
  
  if(NOT DEFINED ELEMENTAL_URL)
    set(ELEMENTAL_URL https://github.com/elemental/Elemental.git)
  endif()
  message(STATUS "Will pull Elemental from ${ELEMENTAL_URL}")
  
  # Create a cache entry for Elemental build variables.
  # Note: This will not overwrite user specified values.
  set(ELEMENTAL_SOURCE_DIR "${PROJECT_BINARY_DIR}/external/source/elemental" CACHE PATH 
        "Path to the Elemental source directory")
  set(ELEMENTAL_BINARY_DIR "${PROJECT_BINARY_DIR}/external/build/elemental" CACHE PATH 
        "Path to the Elemental build directory")
  
  # Set compile flags
  append_flags(ELEMENTAL_CFLAGS "${CMAKE_C_FLAGS}")
  append_flags(ELEMENTAL_CXXFLAGS "${CMAKE_CXX_FLAGS}")
  append_flags(ELEMENTAL_LDFLAGS "${CMAKE_EXE_LINKER_FLAGS}")
  
  if(CMAKE_BUILD_TYPE)
    string(TOLOWER ELEMENTAL_BUILD_TYPE "${CMAKE_BUILD_TYPE}")
    append_flags(ELEMENTAL_CFLAGS "${CMAKE_C_FLAGS_${ELEMENTAL_BUILD_TYPE}}")
    append_flags(ELEMENTAL_CXXFLAGS "${CMAKE_CXX_FLAGS_${ELEMENTAL_BUILD_TYPE}}")
  endif()
  
  #
  # Obtain Elemental source **only** if needed (that's why not using ExternalProject)
  #
  # make directory
  message(STATUS "Checking Elemental source directory: ${ELEMENTAL_SOURCE_DIR}")
  if(EXISTS "${ELEMENTAL_SOURCE_DIR}")
    message(STATUS "Checking Elemental source directory: ${ELEMENTAL_SOURCE_DIR} - found")
  else(EXISTS "${ELEMENTAL_SOURCE_DIR}")
    # Create the external source directory
    if(NOT EXISTS ${PROJECT_BINARY_DIR}/external/source)
      set(error_code 1)
      execute_process(
          COMMAND "${CMAKE_COMMAND}" -E make_directory "${PROJECT_BINARY_DIR}/external/source"
          RESULT_VARIABLE error_code)
      if(error_code)
        message(FATAL_ERROR "Failed to create the external source directory in build tree.")
      endif()
    endif()
  endif()
  # checkout if needed
  if(NOT EXISTS ${ELEMENTAL_SOURCE_DIR}/.git)
    message(STATUS "Pulling Elemental from: ${ELEMENTAL_URL}")
    set(error_code 1)
    set(number_of_tries 0)
    while(error_code AND number_of_tries LESS 3)
      execute_process(
          COMMAND ${GIT_EXECUTABLE}
                  clone ${ELEMENTAL_URL} elemental
          WORKING_DIRECTORY ${PROJECT_BINARY_DIR}/external/source
          OUTPUT_FILE git.log
          ERROR_FILE git.log
          RESULT_VARIABLE error_code)
      math(EXPR number_of_tries "${number_of_tries} + 1")
    endwhile()
    if(number_of_tries GREATER 1)
      message(STATUS "Had to git clone more than once: ${number_of_tries} times.")
    endif()
    if(error_code)
      message(FATAL_ERROR "Failed to clone repository: '${ELEMENTAL_URL}'")
    endif()
  endif()
  # reset to the desired Elemental tag
  if(EXISTS ${ELEMENTAL_SOURCE_DIR}/.git)
    set(error_code 1)
    execute_process(
      COMMAND "${GIT_EXECUTABLE}" fetch
      COMMAND "${GIT_EXECUTABLE}" checkout ${ELEMENTAL_TAG}
      WORKING_DIRECTORY "${ELEMENTAL_SOURCE_DIR}"
      RESULT_VARIABLE error_code)
    if(error_code)
      message(FATAL_ERROR "Failed to checkout tag: '${ELEMENTAL_TAG}'")
    endif()        
  endif()
  
  # Create or clean the build directory
  if(EXISTS "${ELEMENTAL_BINARY_DIR}")
    set(error_code 1)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E remove -f "./*"
        WORKING_DIRECTORY ${ELEMENTAL_BINARY_DIR}
        RESULT_VARIABLE error_code)
    if(error_code)
      message(FATAL_ERROR "Failed to delete existing files the Elemental build directory.")
    endif()
  else()
    set(error_code 1)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${ELEMENTAL_BINARY_DIR}"
        RESULT_VARIABLE error_code)
    if(error_code)
      message(FATAL_ERROR "Failed to create the Elemental build directory.")
    endif()
  endif()
  
  # since 0.85 package name is 'El', before that it was 'elemental'
  # detect the version by searching the main header
  message(STATUS "Looking for the top Elemental header")
  if(EXISTS ${ELEMENTAL_SOURCE_DIR}/include/El.hpp)
    message(STATUS "Looking for the top Elemental header - found El.hpp")
    set(HAVE_EL_H 1)
    set(ELEMENTAL_PACKAGE_NAME El)
  elseif(EXISTS ${ELEMENTAL_SOURCE_DIR}/include/elemental.hpp)
    message(STATUS "Looking for the top Elemental header - found elemental.hpp")
    set(HAVE_ELEMENTAL_H 1)
    set(ELEMENTAL_PACKAGE_NAME elemental)
  else()
    message(FATAL_ERROR "Looking for the top Elemental header - not found")
  endif()
  
  set(error_code 1)
  message (STATUS "** Configuring Elemental")
  execute_process(
      COMMAND ${CMAKE_COMMAND}
      ARGS
      ${ELEMENTAL_SOURCE_DIR}
      -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}
      -DBUILD_SHARED_LIBS=${BUILD_SHARED_LIBS}
      -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
      -DMPI_CXX_COMPILER=${MPI_CXX_COMPILER}
      -DMPI_C_COMPILER=${MPI_C_COMPILER}
      -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
      "-DC_FLAGS=${ELEMENTAL_CFLAGS}"
      -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
      "-DCXX_FLAGS=${ELEMENTAL_CXXFLAGS}"
      -DCMAKE_EXE_LINKER_FLAGS=${ELEMENTAL_LDFLAGS}
      WORKING_DIRECTORY "${ELEMENTAL_BINARY_DIR}"
      RESULT_VARIABLE error_code)
  if(error_code)
    message(FATAL_ERROR "The Elemental cmake configuration failed.")
  else(error_code)
    message (STATUS "** Done configuring Elemental")
  endif(error_code)

  set(ELEMENTAL_DIR ${ELEMENTAL_BINARY_DIR})
  find_package(${ELEMENTAL_PACKAGE_NAME} REQUIRED
               COMPONENTS REQUIRED ${ELEMENTAL_PACKAGE_NAME} pmrrr ElSuiteSparse)
  set(ELEMENTAL_FOUND 1)

  # Add elemental-update target that will pull updates to the Elemental source
  # from the git repository. This is done outside ExternalProject_add to prevent
  # Elemental from doing a full pull, configure, and build everytime the project
  # is built.
  add_custom_target(elemental-update
    COMMAND ${GIT_EXECUTABLE} pull --rebase origin master
    COMMAND ${CMAKE_COMMAND} -E touch_nocreate ${ELEMENTAL_BINARY_DIR}/stamp/elemental-configure
    WORKING_DIRECTORY ${ELEMENTAL_SOURCE_DIR}
    COMMENT "Updating source for 'elemental' from ${ELEMENTAL_URL}")

  add_custom_target(elemental-build ALL
      COMMAND ${CMAKE_COMMAND} --build . --target El
      WORKING_DIRECTORY ${ELEMENTAL_BINARY_DIR}
      COMMENT "Building elemental")

  # Add elemental-clean target that will delete files generated by Elemental build.
  add_custom_target(elemental-clean
    COMMAND $(MAKE) clean
    COMMAND ${CMAKE_COMMAND} -E touch_nocreate ${ELEMENTAL_BINARY_DIR}/stamp/elemental-configure
    WORKING_DIRECTORY ${ELEMENTAL_BINARY_DIR}
    COMMENT Cleaning build directory for 'elemental')

  # Since 'elemental-install' target cannot be linked to the 'install' target,
  # we will do it manually here.
  install(CODE
      "
      execute_process(
          COMMAND \"${CMAKE_MAKE_PROGRAM}\" \"install\" 
          WORKING_DIRECTORY \"${ELEMENTAL_BINARY_DIR}\"
          RESULT_VARIABLE error_code)
      if(error_code)
        message(FATAL_ERROR \"Failed to install 'elemental'\")
      endif()
      "
      )
  
  # Set build dependencies and compiler arguments
  add_dependencies(El elemental-build)
  
  # Set the build variables
  set(ELEMENTAL_INCLUDE_DIRS "${ELEMENTAL_BINARY_DIR}/include")
  set(ELEMENTAL_LIBRARIES 
      "${ELEMENTAL_BINARY_DIR}/${CMAKE_STATIC_LIBRARY_PREFIX}elemental${CMAKE_STATIC_LIBRARY_SUFFIX}"
      "${ELEMENTAL_BINARY_DIR}/external/pmrrr/${CMAKE_STATIC_LIBRARY_PREFIX}pmrrr${CMAKE_STATIC_LIBRARY_SUFFIX}")

  set(MADNESS_HAS_ELEMENTAL 1)
  set(MADNESS_HAS_ELEMENTAL_EMBEDDED 0)
  
  list(APPEND MADNESS_EXPORT_TARGETS ${ELEMENTAL_PACKAGE_NAME} pmrrr ElSuiteSparse)
endif(ENABLE_ELEMENTAL AND DEFINED ELEMENTAL_TAG)