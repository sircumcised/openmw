# FSR 2.2 OpenGL backend (see VERSION.txt). The port's own CMake requires a Visual Studio x64 generator,
# so the library and its SPIR-V shader permutations are described here instead.

set(FFX_SC_EXECUTABLE "" CACHE FILEPATH "AMD FidelityFX_SC.exe (tools/sc of GPUOpen-Effects/FidelityFX-FSR2 v2.2.1)")
if (NOT EXISTS "${FFX_SC_EXECUTABLE}")
    message(FATAL_ERROR "NM_FSR2 needs FFX_SC_EXECUTABLE pointing to AMD's FidelityFX_SC.exe")
endif()

set(FSR2_API_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/ffx-fsr2-api)
set(FSR2_SHADER_OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/shaders/gl)
file(MAKE_DIRECTORY ${FSR2_SHADER_OUTPUT})

# arguments copied from the port's src/ffx-fsr2-api/CMakeLists.txt and gl/CMakeLists.txt
set(FFX_SC_BASE_ARGS
    -reflection -deps=gcc -DFFX_GPU=1
    -DFFX_FSR2_OPTION_UPSAMPLE_SAMPLERS_USE_DATA_HALF=0
    -DFFX_FSR2_OPTION_ACCUMULATE_SAMPLERS_USE_DATA_HALF=0
    -DFFX_FSR2_OPTION_REPROJECT_SAMPLERS_USE_DATA_HALF=1
    -DFFX_FSR2_OPTION_POSTPROCESSLOCKSTATUS_SAMPLERS_USE_DATA_HALF=0
    -DFFX_FSR2_OPTION_UPSAMPLE_USE_LANCZOS_TYPE=2)
set(FFX_SC_PERMUTATION_ARGS
    -DFFX_FSR2_OPTION_REPROJECT_USE_LANCZOS_TYPE={0,1}
    -DFFX_FSR2_OPTION_HDR_COLOR_INPUT={0,1}
    -DFFX_FSR2_OPTION_LOW_RESOLUTION_MOTION_VECTORS={0,1}
    -DFFX_FSR2_OPTION_JITTERED_MOTION_VECTORS={0,1}
    -DFFX_FSR2_OPTION_INVERTED_DEPTH={0,1}
    -DFFX_FSR2_OPTION_APPLY_SHARPENING={0,1})
set(FFX_SC_GL_BASE_ARGS
    -compiler=glslang -e main --target-env opengl --target-env spirv1.3 --amb --stb comp 8 --ssb comp 8
    --sib comp 0 --suavb comp 0 -Os -S comp -DFFX_GLSL=1)

set(FSR2_PASSES
    ffx_fsr2_tcr_autogen_pass
    ffx_fsr2_autogen_reactive_pass
    ffx_fsr2_accumulate_pass
    ffx_fsr2_compute_luminance_pyramid_pass
    ffx_fsr2_depth_clip_pass
    ffx_fsr2_lock_pass
    ffx_fsr2_reconstruct_previous_depth_pass
    ffx_fsr2_rcas_pass)

set(FSR2_PERMUTATION_HEADERS)
foreach(PASS ${FSR2_PASSES})
    set(PASS_SOURCE ${FSR2_API_DIR}/shaders/${PASS}.glsl2)
    set(PASS_HEADER ${FSR2_SHADER_OUTPUT}/${PASS}_permutations.h)
    # the luminance pyramid pass has no 16-bit permutations
    if (PASS STREQUAL "ffx_fsr2_compute_luminance_pyramid_pass")
        set(PASS_HALF -DFFX_HALF=0)
    else()
        set(PASS_HALF -DFFX_HALF={0,1})
    endif()
    add_custom_command(
        OUTPUT ${PASS_HEADER}
        COMMAND ${FFX_SC_EXECUTABLE} ${FFX_SC_BASE_ARGS} ${FFX_SC_GL_BASE_ARGS} ${FFX_SC_PERMUTATION_ARGS} ${PASS_HALF}
            -name=${PASS} -I${FSR2_API_DIR}/gl/shaders -output=${FSR2_SHADER_OUTPUT} ${PASS_SOURCE}
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        DEPENDS ${PASS_SOURCE}
        VERBATIM)
    list(APPEND FSR2_PERMUTATION_HEADERS ${PASS_HEADER})
endforeach()

add_library(fsr2gl STATIC
    ${FSR2_API_DIR}/ffx_fsr2.cpp
    ${FSR2_API_DIR}/ffx_assert.cpp
    ${FSR2_API_DIR}/gl/ffx_fsr2_gl.cpp
    ${FSR2_API_DIR}/gl/shaders/ffx_fsr2_shaders_gl.cpp
    ${FSR2_PERMUTATION_HEADERS})

target_include_directories(fsr2gl
    PUBLIC ${FSR2_API_DIR} ${FSR2_API_DIR}/gl/external/glad/include
    PRIVATE ${FSR2_SHADER_OUTPUT})
target_compile_definitions(fsr2gl PRIVATE _UNICODE UNICODE)
set_target_properties(fsr2gl PROPERTIES UNITY_BUILD OFF)
