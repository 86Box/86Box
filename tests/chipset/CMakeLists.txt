if(BUILD_TESTING)
    add_executable(intel_386ex_dma_tests
        intel_386ex_dma_test.c
        ../../src/dma.c
        ../../src/chipset/intel_386ex_dma.c
    )
    target_include_directories(intel_386ex_dma_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/src/include
        ${PROJECT_SOURCE_DIR}/src/cpu
        ${PROJECT_BINARY_DIR}/src/include
    )
    add_test(NAME Intel386EX.DMA COMMAND intel_386ex_dma_tests)
endif()
