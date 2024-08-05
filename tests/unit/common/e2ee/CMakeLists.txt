add_executable(
  decryptor_tests
  ${CMAKE_SOURCE_DIR}/src/common/gcode/gcode_reader_binary.cpp
  ${CMAKE_SOURCE_DIR}/src/common/gcode/gcode_reader_interface.cpp
  ${CMAKE_SOURCE_DIR}/src/common/gcode/gcode_reader_any.cpp
  ${CMAKE_SOURCE_DIR}/src/common/gcode/gcode_reader_plaintext.cpp
  ${CMAKE_SOURCE_DIR}/lib/heatshrink/heatshrink_decoder.c
  ${CMAKE_SOURCE_DIR}/src/common/gcode/meatpack.cpp
  ${CMAKE_SOURCE_DIR}/src/common/e2ee/e2ee.cpp
  ${CMAKE_SOURCE_DIR}/src/common/filename_type.cpp
  ${CMAKE_SOURCE_DIR}/src/common/gcode/gcode_buffer.cpp
  ${CMAKE_SOURCE_DIR}/src/common/base64_stream_decoder.cpp
  ${CMAKE_SOURCE_DIR}/tests/stubs/e2ee/printer_private_key_mock.cpp
  missing_functions.c
  decryptor.cpp
  )
target_include_directories(
  decryptor_tests
  PRIVATE .
          ${CMAKE_CURRENT_BINARY_DIR}
          ${CMAKE_SOURCE_DIR}/src/common
          ${CMAKE_SOURCE_DIR}/src/common/utils
          ${CMAKE_SOURCE_DIR}/src
          ${CMAKE_SOURCE_DIR}/src/common/gcode
          ${CMAKE_SOURCE_DIR}/lib
          ${CMAKE_SOURCE_DIR}/lib/heatshrink
          ${CMAKE_SOURCE_DIR}/tests/stubs
          ${CMAKE_SOURCE_DIR}/include
          ${CMAKE_SOURCE_DIR}/include/usb_host
          ${CMAKE_SOURCE_DIR}/lib/SG14
  )

target_link_libraries(decryptor_tests mbedTLS bgcode_core freertos_tests)

file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/encrypted_text
          ${CMAKE_CURRENT_SOURCE_DIR}/20_bytes_encrypted_text
     DESTINATION ${CMAKE_CURRENT_BINARY_DIR}
     )
add_catch_test(decryptor_tests)
