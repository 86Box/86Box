if(BUILD_TESTING)
    find_package(ZLIB REQUIRED)
    function(add_modem_test target test_source)
        add_executable(${target} ${test_source} ${ARGN})
        target_include_directories(${target} PRIVATE
            ${PROJECT_SOURCE_DIR}/src/include
            ${PROJECT_BINARY_DIR}/src/include
        )
        target_link_libraries(${target} PRIVATE GTest::gtest_main)
        gtest_discover_tests(${target})
    endfunction()

    add_executable(modem_crypto_tests
        modem_crypto_test.cpp
        ../../../src/device/modem/modem_crypto.c
    )
    target_include_directories(modem_crypto_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/src/include
        ${PROJECT_BINARY_DIR}/src/include
    )
    target_link_libraries(modem_crypto_tests PRIVATE GTest::gtest_main)
    include(GoogleTest)
    gtest_discover_tests(modem_crypto_tests)

    add_modem_test(modem_profile_tests modem_profile_test.cpp
        ../../../src/device/modem/modem_profile.c
    )
    add_modem_test(modem_pnp_tests modem_pnp_test.cpp
        ../../../src/device/modem/modem_pnp.c
    )
    add_modem_test(modem_flow_tests modem_flow_test.cpp
        ../../../src/device/modem/modem_flow.c
    )

    add_executable(modem_eap_tests
        modem_eap_test.cpp
        ../../../src/device/modem/modem_eap.c
        ../../../src/device/modem/modem_crypto.c
    )
    target_include_directories(modem_eap_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/src/include
        ${PROJECT_BINARY_DIR}/src/include
    )
    target_link_libraries(modem_eap_tests PRIVATE GTest::gtest_main)
    gtest_discover_tests(modem_eap_tests)

    add_modem_test(modem_ppp_tests modem_ppp_test.cpp
        ../../../src/device/modem/modem_ppp.c
        ../../../src/device/modem/modem_pap.c
        ../../../src/device/modem/modem_chap.c
        ../../../src/device/modem/modem_ccp.c
        ../../../src/device/modem/modem_ccp_codec.c
        ../../../src/device/modem/modem_eap.c
        ../../../src/device/modem/modem_ipcp.c
        ../../../src/device/modem/modem_ipv6cp.c
        ../../../src/device/modem/modem_nbfcp.c
        ../../../src/device/modem/modem_ipxcp.c
        ../../../src/device/modem/modem_lapb.c
        ../../../src/device/modem/modem_cslip.c
        ../../../src/device/modem/modem_mppe.c
        ../../../src/device/modem/modem_crypto.c
        ../../../src/device/modem/modem_mppp.c
    )
    target_link_libraries(modem_ppp_tests PRIVATE ZLIB::ZLIB)
    if(WIN32)
        target_link_libraries(modem_ppp_tests PRIVATE bcrypt)
    endif()

    add_modem_test(modem_ras_tests modem_ras_test.cpp
        ../../../src/device/modem/modem_ras.c
        ../../../src/device/modem/modem_crypto.c
    )

    add_modem_test(modem_mppp_tests modem_mppp_test.cpp
        ../../../src/device/modem/modem_mppp.c
    )

    add_modem_test(modem_pap_tests modem_pap_test.cpp
        ../../../src/device/modem/modem_pap.c
        ../../../src/device/modem/modem_crypto.c
    )
    add_modem_test(modem_chap_tests modem_chap_test.cpp
        ../../../src/device/modem/modem_chap.c
        ../../../src/device/modem/modem_mppe.c
        ../../../src/device/modem/modem_crypto.c
    )
    add_modem_test(modem_mppe_tests modem_mppe_test.cpp
        ../../../src/device/modem/modem_mppe.c
        ../../../src/device/modem/modem_crypto.c
    )
    add_modem_test(modem_ccp_tests modem_ccp_test.cpp
        ../../../src/device/modem/modem_ccp.c
        ../../../src/device/modem/modem_ccp_codec.c
        ../../../src/device/modem/modem_mppe.c
        ../../../src/device/modem/modem_crypto.c
    )
    target_link_libraries(modem_ccp_tests PRIVATE ZLIB::ZLIB)
    add_modem_test(modem_ipcp_tests modem_ipcp_test.cpp
        ../../../src/device/modem/modem_ipcp.c
    )
    add_modem_test(modem_nbns_tests modem_nbns_test.cpp
        ../../../src/network/services/net_nbns.c
    )
    add_modem_test(modem_nbf_tests modem_nbf_test.cpp
        ../../../src/network/services/net_nbf.c
        ../../../src/device/modem/modem_ras.c
        ../../../src/network/services/net_browser.c
        ../../../src/network/services/net_smb.c
        ../../../src/network/services/net_smb_fs.c
        ../../../src/network/services/net_smb_rap.c
        ../../../src/device/modem/modem_crypto.c
    )
    add_modem_test(modem_ipx_tests modem_ipx_test.cpp
        ../../../src/network/services/net_ipx.c
        ../../../src/network/services/net_browser.c
        ../../../src/network/services/net_smb.c
        ../../../src/network/services/net_smb_fs.c
        ../../../src/network/services/net_smb_rap.c
        ../../../src/device/modem/modem_crypto.c
    )
    add_modem_test(modem_pptp_tests modem_pptp_test.cpp
        ../../../src/device/modem/modem_pptp.c
        ../../../src/device/modem/modem_ras.c
        ../../../src/network/services/net_vserver.c
        ../../../src/network/services/net_nbns.c
        ../../../src/network/services/net_nbf.c
        ../../../src/device/modem/modem_nbfcp.c
        ../../../src/network/services/net_ipx.c
        ../../../src/device/modem/modem_ipxcp.c
        ../../../src/network/services/net_browser.c
        ../../../src/network/services/net_smb.c
        ../../../src/network/services/net_smb_fs.c
        ../../../src/network/services/net_smb_rap.c
        ../../../src/device/modem/modem_ppp.c
        ../../../src/device/modem/modem_pap.c
        ../../../src/device/modem/modem_chap.c
        ../../../src/device/modem/modem_ccp.c
        ../../../src/device/modem/modem_ccp_codec.c
        ../../../src/device/modem/modem_eap.c
        ../../../src/device/modem/modem_ipcp.c
        ../../../src/device/modem/modem_ipv6cp.c
        ../../../src/device/modem/modem_lapb.c
        ../../../src/device/modem/modem_cslip.c
        ../../../src/device/modem/modem_mppe.c
        ../../../src/device/modem/modem_crypto.c
        ../../../src/device/modem/modem_mppp.c
    )
    target_link_libraries(modem_pptp_tests PRIVATE ZLIB::ZLIB)
    if(WIN32)
        target_link_libraries(modem_pptp_tests PRIVATE bcrypt)
    endif()
    add_modem_test(modem_vserver_eth_tests modem_vserver_eth_test.cpp
        ../../../src/device/modem/modem_ras.c
        ../../../src/network/services/net_vserver_eth.c
        ../../../src/network/services/net_ipx.c
        ../../../src/network/services/net_vserver.c
        ../../../src/network/services/net_nbns.c
        ../../../src/network/services/net_nbf.c
        ../../../src/network/services/net_browser.c
        ../../../src/network/services/net_smb.c
        ../../../src/network/services/net_smb_fs.c
        ../../../src/network/services/net_smb_rap.c
        ../../../src/device/modem/modem_crypto.c
    )
    add_modem_test(modem_smb_tests modem_smb_test.cpp
        ../../../src/network/services/net_smb.c
        ../../../src/network/services/net_smb_fs.c
        ../../../src/network/services/net_smb_rap.c
        ../../../src/device/modem/modem_crypto.c
    )
    add_modem_test(modem_vserver_tests modem_vserver_test.cpp
        ../../../src/network/services/net_vserver.c
        ../../../src/network/services/net_nbns.c
        ../../../src/network/services/net_browser.c
        ../../../src/network/services/net_smb.c
        ../../../src/network/services/net_smb_fs.c
        ../../../src/network/services/net_smb_rap.c
        ../../../src/device/modem/modem_crypto.c
    )
    add_modem_test(modem_file_services_tests modem_file_services_test.cpp
        ../../../src/network/services/net_vserver.c
        ../../../src/network/services/net_nbns.c
        ../../../src/network/services/net_browser.c
        ../../../src/network/services/net_smb.c
        ../../../src/network/services/net_smb_fs.c
        ../../../src/network/services/net_smb_rap.c
        ../../../src/network/services/net_ftp.c
        ../../../src/network/services/net_tftp.c
        ../../../src/device/modem/modem_crypto.c
    )
    target_compile_definitions(modem_file_services_tests PRIVATE ENABLE_VSERVER_FTP ENABLE_VSERVER_TFTP)
    add_modem_test(modem_cslip_tests modem_cslip_test.cpp
        ../../../src/device/modem/modem_cslip.c
    )
    add_modem_test(modem_slip_tests modem_slip_test.cpp
        ../../../src/device/modem/modem_slip.c
    )
    add_modem_test(modem_slip_auth_tests modem_slip_auth_test.cpp
        ../../../src/device/modem/modem_slip_auth.c
        ../../../src/device/modem/modem_crypto.c
    )

    add_modem_test(modem_voice_tests modem_voice_test.cpp
        ../../../src/device/modem/modem_voice.c
    )
    if(NOT MSVC)
        target_link_libraries(modem_voice_tests PRIVATE m)
    endif()

    # LTO and section GC discard unrelated emulator code from the included modem.
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        foreach(frontend IN ITEMS char net)
            foreach(config IN ITEMS normal advanced)
                set(target ${frontend}_modem_tests)
                if(config STREQUAL "advanced")
                    string(APPEND target "_advanced")
                endif()
                add_executable(${target} ${frontend}_modem_test.c
                    ../../../src/utils/fifo8.c
                    ../../../src/device/modem/modem_pnp.c
                    ../../../src/device/modem/modem_profile.c
                    ../../../src/device/modem/modem_flow.c
                )
                target_include_directories(${target} PRIVATE
                    ${PROJECT_SOURCE_DIR}/src/include
                    ${PROJECT_BINARY_DIR}/src/include
                )
                target_compile_definitions(${target} PRIVATE ENABLE_MODEM_LOG ENABLE_MODEM_RAW_CAPTURE)
                if(config STREQUAL "advanced")
                    target_compile_definitions(${target} PRIVATE ENABLE_MODEM_ADVANCED_CONFIG)
                endif()
                target_compile_options(${target} PRIVATE -O1 -flto -ffunction-sections -fdata-sections)
                target_link_options(${target} PRIVATE
                    -flto
                    $<$<PLATFORM_ID:Darwin>:-Wl,-dead_strip>
                    $<$<NOT:$<PLATFORM_ID:Darwin>>:-Wl,--gc-sections>
                )
                add_test(NAME ${target} COMMAND ${target})
            endforeach()
        endforeach()
    endif()

    foreach(ring_count IN ITEMS default 0 3)
        set(target modem_sound_tests_${ring_count})
        add_executable(${target} modem_sound_test.cpp
            ../../../src/device/modem/modem_sound.c
        )
        target_include_directories(${target} PRIVATE
            ${PROJECT_SOURCE_DIR}/src/include
            ${PROJECT_BINARY_DIR}/src/include
        )
        target_link_libraries(${target} PRIVATE GTest::gtest_main)
        if(NOT ring_count STREQUAL "default")
            target_compile_definitions(${target} PRIVATE MODEM_SOUND_RING_COUNT=${ring_count})
        endif()
        if(NOT MSVC)
            target_link_libraries(${target} PRIVATE m)
        endif()
        gtest_discover_tests(${target} TEST_PREFIX "${ring_count}.")
    endforeach()
endif()
