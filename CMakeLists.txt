cmake_minimum_required(VERSION 3.20)
project(Werkstatt LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Einige Abhängigkeiten verlangen sehr alte CMake-Versionen; CMake 4 braucht diesen Hinweis.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()

find_package(Vulkan REQUIRED COMPONENTS glslc)

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# ---------------------------------------------------------------- GLFW (Fenster + Eingabe)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glfw URL https://github.com/glfw/glfw/archive/refs/tags/3.4.zip)

# ---------------------------------------------------------------- GLM (Mathe)
FetchContent_Declare(glm URL https://github.com/g-truc/glm/archive/refs/tags/1.0.1.zip)

# ---------------------------------------------------------------- Jolt Physics
set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE)
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
set(OVERRIDE_CXX_FLAGS OFF CACHE BOOL "" FORCE)
set(TARGET_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(TARGET_HELLO_WORLD OFF CACHE BOOL "" FORCE)
set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "" FORCE)
set(TARGET_SAMPLES OFF CACHE BOOL "" FORCE)
set(TARGET_VIEWER OFF CACHE BOOL "" FORCE)
set(CPP_RTTI_ENABLED ON CACHE BOOL "" FORCE)
set(DOUBLE_PRECISION ON CACHE BOOL "" FORCE)   # genaue Physik auch weit vom Ursprung
# Prüfhilfe von Jolt: bricht bei Gleitkomma-Überlauf in den Physik-Threads das Programm ab. Im Spiel nicht erwünscht.
set(FLOATING_POINT_EXCEPTIONS_ENABLED OFF CACHE BOOL "" FORCE)
FetchContent_Declare(JoltPhysics
  URL https://github.com/jrouwe/JoltPhysics/archive/refs/tags/v5.3.0.zip
  SOURCE_SUBDIR Build
  # Korrekturen: vorausschauende Kontakte für zusammengesetzte Formen, Rückmeldung der Kontaktimpulse (cmake/patch_jolt.cmake)
  PATCH_COMMAND ${CMAKE_COMMAND} -DWERKSTATT_PATCH=2 -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/patch_jolt.cmake)

# ---------------------------------------------------------------- Dear ImGui (Menüs)
FetchContent_Declare(imgui URL https://github.com/ocornut/imgui/archive/refs/tags/v1.91.9.zip)

FetchContent_MakeAvailable(glfw glm JoltPhysics imgui)

add_library(imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui PUBLIC glfw Vulkan::Vulkan)

# ---------------------------------------------------------------- Shader (GLSL -> SPIR-V, eingebettet)
set(SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(SHADER_OUT ${CMAKE_CURRENT_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${SHADER_OUT})
set(SHADER_SOURCES
  ${SHADER_DIR}/mesh.vert ${SHADER_DIR}/mesh.frag
  ${SHADER_DIR}/shadow.vert
  ${SHADER_DIR}/line.vert ${SHADER_DIR}/line.frag
  ${SHADER_DIR}/sky.vert ${SHADER_DIR}/sky.frag)
set(SHADER_OUTPUTS)
foreach(src ${SHADER_SOURCES})
  get_filename_component(name ${src} NAME)
  set(out ${SHADER_OUT}/${name}.inc)
  add_custom_command(
    OUTPUT ${out}
    COMMAND Vulkan::glslc -O --target-env=vulkan1.3 -mfmt=c -I ${SHADER_DIR} ${src} -o ${out}
    DEPENDS ${src} ${SHADER_DIR}/common.glsl
    COMMENT "Shader ${name}")
  list(APPEND SHADER_OUTPUTS ${out})
endforeach()
add_custom_target(shaders DEPENDS ${SHADER_OUTPUTS})

# ---------------------------------------------------------------- Spielfigur (glTF, eingebettet)
set(AVATAR_SRC ${CMAKE_CURRENT_SOURCE_DIR}/assets/characters/worker.glb)
set(AVATAR_OUT ${SHADER_OUT}/worker.glb.inc)
add_custom_command(
  OUTPUT ${AVATAR_OUT}
  COMMAND ${CMAKE_COMMAND} -DIN=${AVATAR_SRC} -DOUT=${AVATAR_OUT} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_file.cmake
  DEPENDS ${AVATAR_SRC} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_file.cmake
  COMMENT "Spielfigur worker.glb")
add_custom_target(assets DEPENDS ${AVATAR_OUT})

# ---------------------------------------------------------------- Spiel
file(GLOB GAME_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h)
add_executable(Werkstatt ${GAME_SOURCES})
add_dependencies(Werkstatt shaders assets)
target_include_directories(Werkstatt PRIVATE ${SHADER_OUT} ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(Werkstatt PRIVATE Vulkan::Vulkan glfw glm::glm Jolt imgui)
target_compile_definitions(Werkstatt PRIVATE
  GLM_FORCE_DEPTH_ZERO_TO_ONE GLM_FORCE_RADIANS GLM_ENABLE_EXPERIMENTAL
  NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
if(MSVC)
  target_compile_options(Werkstatt PRIVATE /utf-8 /W3 /MP)
  target_compile_options(imgui PRIVATE /utf-8)
  target_link_libraries(Werkstatt PRIVATE comdlg32)
  set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT Werkstatt)
endif()
