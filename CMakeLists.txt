cmake_minimum_required(VERSION 3.20)
option(BUILD_HOST_TESTS "Build host-only parser unit tests" OFF)
option(PICO_BRIDGE_VERBOSE "Enable raw HID map/report and sampled state logging" OFF)
option(BRIDGE_GUIDE_MSC "Add immutable PICO_GUIDE USB mass-storage manual" OFF)
set(BRIDGE_USB_MODE "HID" CACHE STRING "USB output backend: HID or XINPUT")
set_property(CACHE BRIDGE_USB_MODE PROPERTY STRINGS HID XINPUT)
set(BRIDGE_INPUT_MODE "BLE" CACHE STRING "Bluetooth input: BLE or CLASSIC_8BITDO")
set_property(CACHE BRIDGE_INPUT_MODE PROPERTY STRINGS BLE CLASSIC_8BITDO)
set(BRIDGE_XINPUT_VID "0x2e8a" CACHE STRING "Lab XUSB VID; obtain an assigned VID/PID before distribution")
set(BRIDGE_XINPUT_PID "0x000a" CACHE STRING "Lab XUSB PID; obtain an assigned VID/PID before distribution")
set(BRIDGE_XINPUT_BCD_DEVICE "0x0301" CACHE STRING "XUSB device revision for Windows descriptor cache")
set(BRIDGE_XINPUT_SLOTS "1" CACHE STRING "Number of XUSB interfaces (1 or 2)")
if(NOT BRIDGE_XINPUT_SLOTS STREQUAL "1" AND NOT BRIDGE_XINPUT_SLOTS STREQUAL "2")
  message(FATAL_ERROR "BRIDGE_XINPUT_SLOTS must be 1 or 2")
endif()
if(BRIDGE_XINPUT_SLOTS STREQUAL "2" AND NOT
   (BRIDGE_USB_MODE STREQUAL "XINPUT" AND BRIDGE_INPUT_MODE STREQUAL "CLASSIC_8BITDO"))
  message(FATAL_ERROR "Two XInput slots require XINPUT USB and the combined BLE/Classic input build")
endif()
if(BRIDGE_GUIDE_MSC AND NOT
   (BRIDGE_USB_MODE STREQUAL "XINPUT" AND BRIDGE_XINPUT_SLOTS STREQUAL "2"))
  message(FATAL_ERROR "PICO_GUIDE MSC requires the dual XInput build")
endif()
if(BUILD_HOST_TESTS)
 project(pico_hid_parser_tests C NONE)
 set(CMAKE_C_STANDARD 11)
 enable_testing()
 add_executable(test_hid_gamepad tests/test_hid_gamepad.c src/controllers/hid_gamepad.c src/core/controller_state.c)
 target_include_directories(test_hid_gamepad PRIVATE src)
 add_test(NAME hid_gamepad_parser COMMAND test_hid_gamepad)
 add_executable(test_usb_gamepad tests/test_usb_gamepad.c
   src/usb/gamepad_report.c src/usb/gamepad_output.c
   src/core/controller_state.c src/controllers/hid_gamepad.c
   src/led/status_led_pattern.c)
 target_include_directories(test_usb_gamepad PRIVATE src)
 add_test(NAME usb_gamepad_pipeline COMMAND test_usb_gamepad)
 add_executable(test_reconnect_policy tests/test_reconnect_policy.c
   src/bluetooth/reconnect_policy.c)
 target_include_directories(test_reconnect_policy PRIVATE src)
 add_test(NAME reconnect_policy COMMAND test_reconnect_policy)
 add_executable(test_xinput tests/test_xinput.c src/usb/xinput_report.c
   src/usb/xinput_output.c src/usb/xinput_protocol.c src/core/controller_state.c)
 target_include_directories(test_xinput PRIVATE src)
 add_test(NAME xinput_backend COMMAND test_xinput)
 add_executable(test_switch_8bitdo tests/test_switch_8bitdo.c
   src/controllers/switch_8bitdo.c src/core/controller_state.c
   src/usb/xinput_report.c)
 target_include_directories(test_switch_8bitdo PRIVATE src)
 add_test(NAME switch_8bitdo_parser COMMAND test_switch_8bitdo)
 add_executable(test_dual_routes tests/test_dual_routes.c
   src/core/input_owner.c src/core/autofire.c src/core/controller_state.c)
 target_include_directories(test_dual_routes PRIVATE src)
 target_compile_definitions(test_dual_routes PRIVATE BRIDGE_USB_XINPUT=1 BRIDGE_XINPUT_SLOTS=2 BRIDGE_HOST_TEST=1)
 add_test(NAME dual_routes COMMAND test_dual_routes)
 add_executable(test_switch_player_led tests/test_switch_player_led.c
   src/controllers/switch_player_led.c)
 target_include_directories(test_switch_player_led PRIVATE src)
 add_test(NAME switch_player_led COMMAND test_switch_player_led)
 add_executable(test_autofire tests/test_autofire.c src/core/autofire.c
   src/core/controller_state.c)
 target_include_directories(test_autofire PRIVATE src)
 add_test(NAME autofire_engine COMMAND test_autofire)
 add_executable(test_guide_fat tests/test_guide_fat.c src/usb/guide_fat.c
   src/usb/guide_scsi.c)
 target_include_directories(test_guide_fat PRIVATE src)
 add_test(NAME guide_fat_read_only COMMAND test_guide_fat)
 add_test(NAME guide_assets COMMAND ${CMAKE_COMMAND}
   -DROOT=${CMAKE_CURRENT_LIST_DIR} -P ${CMAKE_CURRENT_LIST_DIR}/tests/check_guide_assets.cmake)
 add_executable(test_msc_protection tests/test_msc_protection.c
   src/usb/guide_msc.c src/usb/guide_fat.c src/usb/guide_scsi.c)
 target_include_directories(test_msc_protection PRIVATE tests/stubs src)
 add_test(NAME msc_write_protection COMMAND test_msc_protection)
 add_executable(test_dual_descriptors tests/test_dual_descriptors.c
   src/usb/xinput_dual_protocol.c)
 target_include_directories(test_dual_descriptors PRIVATE src)
 target_compile_definitions(test_dual_descriptors PRIVATE BRIDGE_GUIDE_MSC=1
   BRIDGE_XINPUT_VID=0x2e8a BRIDGE_XINPUT_PID=0x000a BRIDGE_XINPUT_BCD_DEVICE=0x0403)
 add_test(NAME dual_xinput_msc_descriptors COMMAND test_dual_descriptors)
 return()
endif()
include(cmake/pico_sdk_import.cmake)
project(pico_bluetooth_joystick C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()
set(picotool_DIR "${CMAKE_CURRENT_LIST_DIR}/third_party/picotool-2.3.1/picotool" CACHE PATH "Official picotool package")
if(BRIDGE_USB_MODE STREQUAL "XINPUT")
  set(BRIDGE_USB_XINPUT 1)
  if(BRIDGE_XINPUT_SLOTS STREQUAL "2")
    set(USB_BACKEND_SOURCES src/usb/xinput_report.c src/usb/xinput_output.c
        src/usb/xinput_protocol.c src/usb/xinput_dual_protocol.c src/usb/usb_xinput_dual.c)
  else()
    set(USB_BACKEND_SOURCES src/usb/xinput_report.c src/usb/xinput_output.c
        src/usb/xinput_protocol.c src/usb/usb_xinput.c)
  endif()
elseif(BRIDGE_USB_MODE STREQUAL "HID")
  set(BRIDGE_USB_XINPUT 0)
  set(USB_BACKEND_SOURCES src/usb/gamepad_report.c src/usb/gamepad_output.c
      src/usb/usb_descriptors.c src/usb/usb_gamepad.c)
else()
  message(FATAL_ERROR "BRIDGE_USB_MODE must be HID or XINPUT")
endif()
if(BRIDGE_INPUT_MODE STREQUAL "CLASSIC_8BITDO")
  set(BRIDGE_CLASSIC_8BITDO 1)
  set(INPUT_HOST_SOURCES src/bluetooth/classic_8bitdo_host.c src/controllers/switch_8bitdo.c src/controllers/switch_player_led.c src/bluetooth/ble_host.c src/core/input_owner.c)
elseif(BRIDGE_INPUT_MODE STREQUAL "BLE")
  set(BRIDGE_CLASSIC_8BITDO 0)
  set(INPUT_HOST_SOURCES src/bluetooth/ble_host.c)
else()
  message(FATAL_ERROR "BRIDGE_INPUT_MODE must be BLE or CLASSIC_8BITDO")
endif()
add_executable(pico_bluetooth_joystick
  src/main.c ${INPUT_HOST_SOURCES} src/bluetooth/gatt_audit.c
  src/bluetooth/reconnect_policy.c
  src/controllers/hid_gamepad.c src/core/controller_state.c src/core/autofire.c src/diagnostics/diag.c
  ${USB_BACKEND_SOURCES} src/led/status_led.c src/led/status_led_pattern.c)
if(BRIDGE_GUIDE_MSC)
  target_sources(pico_bluetooth_joystick PRIVATE src/usb/guide_fat.c
    src/usb/guide_scsi.c src/usb/guide_msc.c)
  set(GUIDE_OBJECT_DIR "${CMAKE_CURRENT_BINARY_DIR}/guide_objects")
  foreach(GUIDE_FILE README.TXT MANUAL.HTM AUTOFIRE.PNG VERSION.TXT)
    set(GUIDE_SOURCE "${CMAKE_CURRENT_LIST_DIR}/assets/guide/${GUIDE_FILE}")
    set(GUIDE_OBJECT "${GUIDE_OBJECT_DIR}/${GUIDE_FILE}.o")
    add_custom_command(OUTPUT "${GUIDE_OBJECT}"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${GUIDE_OBJECT_DIR}"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${GUIDE_SOURCE}" "${GUIDE_OBJECT_DIR}/${GUIDE_FILE}"
      COMMAND ${CMAKE_OBJCOPY} -I binary -O elf32-littlearm -B arm
        --rename-section .data=.rodata.guide,alloc,load,readonly,data,contents
        "${GUIDE_FILE}" "${GUIDE_FILE}.o"
      DEPENDS "${GUIDE_SOURCE}"
      WORKING_DIRECTORY "${GUIDE_OBJECT_DIR}" VERBATIM)
    set_source_files_properties("${GUIDE_OBJECT}" PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)
    target_sources(pico_bluetooth_joystick PRIVATE "${GUIDE_OBJECT}")
  endforeach()
endif()
target_include_directories(pico_bluetooth_joystick PRIVATE src)
target_compile_definitions(pico_bluetooth_joystick PRIVATE
  CFG_TUD_HID=$<NOT:$<BOOL:${BRIDGE_USB_XINPUT}>>
  BRIDGE_USB_XINPUT=${BRIDGE_USB_XINPUT}
  BRIDGE_CLASSIC_8BITDO=${BRIDGE_CLASSIC_8BITDO}
  BRIDGE_XINPUT_SLOTS=${BRIDGE_XINPUT_SLOTS}
  BRIDGE_GUIDE_MSC=$<BOOL:${BRIDGE_GUIDE_MSC}>
  BRIDGE_DIAGNOSTIC_USB=$<AND:$<BOOL:${BRIDGE_CLASSIC_8BITDO}>,$<NOT:$<BOOL:${BRIDGE_USB_XINPUT}>>>
  PICO_STDIO_USB_USE_DEFAULT_DESCRIPTORS=0
  PICO_STDIO_USB_ENABLE_IRQ_BACKGROUND_TASK=0
  PICO_STDIO_USB_ENABLE_TINYUSB_INIT=1
  PICO_STDIO_USB_STDOUT_TIMEOUT_US=1000
  PICO_ENABLE_USB_RESET_VIA_VENDOR_INTERFACE=0
  PICO_ENABLE_USB_RESET_VIA_BAUD_RATE=0
  BRIDGE_DIAG_VERBOSE=$<BOOL:${PICO_BRIDGE_VERBOSE}>)
target_link_libraries(pico_bluetooth_joystick PRIVATE pico_stdlib pico_cyw43_arch_none pico_btstack_cyw43)
if(BRIDGE_CLASSIC_8BITDO)
  # pico_btstack_cyw43's flash-bank shim references the LE DB symbol even
  # when the application uses Classic HID only; no BLE host is instantiated.
  target_link_libraries(pico_bluetooth_joystick PRIVATE pico_btstack_classic pico_btstack_ble)
  # Pico SDK's shared HCI translation unit expects the BLE state members when
  # its default btstack_config enables LE central helpers. No BLE host is used.
  target_compile_definitions(pico_bluetooth_joystick PRIVATE ENABLE_BLE=1 NVM_NUM_LINK_KEYS=2)
else()
  target_link_libraries(pico_bluetooth_joystick PRIVATE pico_btstack_ble)
endif()
if(BRIDGE_USB_XINPUT)
  target_compile_definitions(pico_bluetooth_joystick PRIVATE
    BRIDGE_XINPUT_VID=${BRIDGE_XINPUT_VID}
    BRIDGE_XINPUT_PID=${BRIDGE_XINPUT_PID}
    BRIDGE_XINPUT_BCD_DEVICE=${BRIDGE_XINPUT_BCD_DEVICE})
  target_include_directories(pico_bluetooth_joystick PRIVATE src/usb/xinput)
  target_link_libraries(pico_bluetooth_joystick PRIVATE tinyusb_device)
  pico_enable_stdio_usb(pico_bluetooth_joystick 0)
  pico_enable_stdio_uart(pico_bluetooth_joystick 1)
else()
  pico_enable_stdio_usb(pico_bluetooth_joystick 1)
  pico_enable_stdio_uart(pico_bluetooth_joystick 0)
endif()
pico_add_extra_outputs(pico_bluetooth_joystick)
