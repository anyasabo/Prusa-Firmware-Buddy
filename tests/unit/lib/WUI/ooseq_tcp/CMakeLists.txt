# Buddy's vendored lwIP TCP input path + the real lib/WUI/ooseq_limit.cpp on the host, with a test
# lwipopts.h that copies Buddy's TCP settings.
set(LWIP_DIR ${CMAKE_SOURCE_DIR}/lib/Middlewares/Third_Party/LwIP/src)

add_executable(
  ooseq_tcp_tests
  ${CMAKE_SOURCE_DIR}/lib/WUI/ooseq_limit.cpp
  ${LWIP_DIR}/core/def.c
  ${LWIP_DIR}/core/inet_chksum.c
  ${LWIP_DIR}/core/init.c
  ${LWIP_DIR}/core/ip.c
  ${LWIP_DIR}/core/ipv4/ip4.c
  ${LWIP_DIR}/core/ipv4/ip4_addr.c
  ${LWIP_DIR}/core/mem.c
  ${LWIP_DIR}/core/memp.c
  ${LWIP_DIR}/core/netif.c
  ${LWIP_DIR}/core/pbuf.c
  ${LWIP_DIR}/core/stats.c
  ${LWIP_DIR}/core/tcp.c
  ${LWIP_DIR}/core/tcp_in.c
  ${LWIP_DIR}/core/tcp_out.c
  ${LWIP_DIR}/core/timeouts.c
  ooseq_tcp_test.cpp
  tcp_helper.c
  )

# The test dir comes first so its buddy/lwipopts.h and arch/cc.h win.
target_include_directories(
  ooseq_tcp_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${LWIP_DIR}/include
                          ${CMAKE_SOURCE_DIR}/lib/WUI
  )
target_compile_definitions(ooseq_tcp_tests PRIVATE OOSEQ_LIMIT_OVERRIDABLE)
# Third-party C; keep the output readable.
set_source_files_properties(
  tcp_helper.c ${LWIP_DIR}/core/tcp_in.c PROPERTIES COMPILE_OPTIONS "-Wno-sign-compare"
  )

add_catch_test(ooseq_tcp_tests)
