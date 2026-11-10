# FalconEye devreye alma firmware'i – STM32 hedefleri
#   Kart B : cmake -S . -B build-b -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake     -DFE_MCU=G474
#   Kart A, M: cmake -S . -B build-g0 -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi-m0p.cmake -DFE_MCU=G0B1
# CMSIS başlıkları: tools/fetch_cmsis.sh (bir kez; third_party/ altına)
set(FE_CMSIS_DIR "${CMAKE_SOURCE_DIR}/third_party" CACHE PATH "CMSIS başlıklarının kökü")
if(NOT EXISTS "${FE_CMSIS_DIR}/CMSIS_5/CMSIS/Core/Include/core_cm4.h")
  message(FATAL_ERROR "CMSIS bulunamadı: önce tools/fetch_cmsis.sh çalıştırın (FE_CMSIS_DIR=${FE_CMSIS_DIR})")
endif()

set(BU_STM32 ${CMAKE_CURRENT_SOURCE_DIR}/stm32/hw.c ${CMAKE_CURRENT_SOURCE_DIR}/stm32/startup.c)

function(bu_target NAME DEVICE CMSIS_DEV LD)
  add_executable(${NAME} ${ARGN} ${BU_STM32})
  target_compile_definitions(${NAME} PRIVATE ${DEVICE})
  target_include_directories(${NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/stm32
    ${FE_CMSIS_DIR}/CMSIS_5/CMSIS/Core/Include
    ${FE_CMSIS_DIR}/${CMSIS_DEV}/Include)
  target_link_libraries(${NAME} PRIVATE fe_bringup fe_core m)
  target_link_options(${NAME} PRIVATE -T${CMAKE_CURRENT_SOURCE_DIR}/stm32/${LD} -L${CMAKE_CURRENT_SOURCE_DIR}/stm32
    -nostartfiles -specs=nosys.specs -Wl,-Map=${NAME}.map -Wl,--print-memory-usage)
  set_target_properties(${NAME} PROPERTIES SUFFIX ".elf")
  add_custom_command(TARGET ${NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${NAME}> ${NAME}.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${NAME}> ${NAME}.hex
    COMMENT "${NAME}: .bin / .hex")
endfunction()

if(FE_MCU STREQUAL "G474")
  bu_target(bringup_kart_b STM32G474xx cmsis_device_g4 g474.ld board_b/main_b.c)
elseif(FE_MCU STREQUAL "G0B1")
  bu_target(bringup_kart_a STM32G0B1xx cmsis_device_g0 g0b1.ld board_a/main_a.c board_a/eve.c)
  bu_target(bringup_kart_m STM32G0B1xx cmsis_device_g0 g0b1.ld board_m/main_m.c)
else()
  message(FATAL_ERROR "FE_MCU = G474 ya da G0B1")
endif()
