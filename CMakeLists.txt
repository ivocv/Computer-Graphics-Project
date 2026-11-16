cmake_minimum_required(VERSION 3.20)

project(MechanicalSafe LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

# The project deliberately uses only a window/input library, an OpenGL loader,
# and GLM for vector and matrix arithmetic.  Rendering and application logic
# are implemented in src/main.cpp.
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(glfw)

FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(glm)

# GLAD 0.1 generates the OpenGL 3.3 core loader during configuration.
FetchContent_Declare(
    glad
    GIT_REPOSITORY https://github.com/Dav1dde/glad.git
    GIT_TAG v0.1.36
    GIT_SHALLOW TRUE
)
FetchContent_GetProperties(glad)
if(NOT glad_POPULATED)
    FetchContent_Populate(glad)
    # GLAD 0.1.36 predates CMake 4.  Visual Studio 2026 bundles CMake 4,
    # so retain compatibility while adding this third-party dependency.
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)
    set(GLAD_PROFILE "core" CACHE STRING "" FORCE)
    set(GLAD_API "gl=3.3" CACHE STRING "" FORCE)
    set(GLAD_GENERATOR "c" CACHE STRING "" FORCE)
    add_subdirectory("${glad_SOURCE_DIR}" "${glad_BINARY_DIR}" EXCLUDE_FROM_ALL)
endif()

set(SHADER_FILES
    shaders/safe.vert
    shaders/safe.frag
)
set_source_files_properties(${SHADER_FILES} PROPERTIES HEADER_FILE_ONLY TRUE)
source_group("Shaders" FILES ${SHADER_FILES})

add_executable(MechanicalSafe
    src/main.cpp
    ${SHADER_FILES}
)

# Make F5 start the actual application rather than CMake's helper ALL_BUILD
# target when the generated solution is opened in Visual Studio.
set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT MechanicalSafe)

file(TO_CMAKE_PATH "${CMAKE_SOURCE_DIR}/shaders" SAFE_SHADER_DIR)
target_compile_definitions(MechanicalSafe PRIVATE SAFE_SHADER_DIR=\"${SAFE_SHADER_DIR}\")
target_link_libraries(MechanicalSafe PRIVATE glad glfw glm::glm)

if(MSVC)
    target_compile_options(MechanicalSafe PRIVATE /W4 /permissive-)
else()
    target_compile_options(MechanicalSafe PRIVATE -Wall -Wextra -Wpedantic)
endif()
