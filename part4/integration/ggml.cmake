# Included only for the private Lab 3 build when SOC_DOT_HAL_DIR is supplied.
foreach(feature GGML_LLAMAFILE GGML_BLAS GGML_METAL GGML_CPU_REPACK GGML_CPU_KLEIDIAI)
    if(${feature})
        message(FATAL_ERROR "Lab 3 hardware dispatch requires ${feature}=OFF")
    endif()
endforeach()
find_package(Threads REQUIRED)
target_sources(${GGML_CPU_NAME} PRIVATE
    "${SOC_DOT_HAL_DIR}/hal/dot_hal.c"
    "${SOC_DOT_HAL_DIR}/integration/dot-ggml.c")
target_include_directories(${GGML_CPU_NAME} PRIVATE
    "${SOC_DOT_HAL_DIR}/include"
    "${SOC_DOT_HAL_DIR}/hal"
    "${SOC_DOT_HAL_DIR}/integration")
target_compile_definitions(${GGML_CPU_NAME} PRIVATE LAB3_DOT_HAL)
target_compile_options(${GGML_CPU_NAME} PRIVATE -ffp-contract=off)
target_link_libraries(${GGML_CPU_NAME} PRIVATE Threads::Threads)
