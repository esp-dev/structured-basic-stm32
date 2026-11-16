cmake_minimum_required(VERSION 3.20)
project(structured_basic_stm32 LANGUAGES C ASM)

set(SBASIC_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/basic" CACHE PATH "Structured BASIC repository")
if(NOT EXISTS "${SBASIC_CORE_DIR}/cmake/SBasicCore.cmake")
  message(FATAL_ERROR "Initialize the basic submodule or point SBASIC_CORE_DIR to the core repository")
endif()
include("${SBASIC_CORE_DIR}/cmake/SBasicCore.cmake")
sbasic_add_core(sbasic_core)
target_compile_definitions(sbasic_core PUBLIC SB_POOL_SIZE=16384u)

set(PORT "${CMAKE_CURRENT_SOURCE_DIR}/platform/stm32")
if(NOT CMAKE_CROSSCOMPILING)
  enable_testing()
  add_executable(console_tests tests/console_tests.c "${PORT}/console.c")
  target_include_directories(console_tests PRIVATE "${PORT}")
  target_link_libraries(console_tests PRIVATE sbasic_core)
  target_compile_options(console_tests PRIVATE -Wall -Wextra -Wconversion -Wshadow -Werror)
  add_test(NAME console COMMAND console_tests)
  return()
endif()

set(STM32CUBE_F4 "" CACHE PATH "STM32CubeF4 1.27.1 SDK")
set(CMSIS "${STM32CUBE_F4}/Drivers/CMSIS")
set(DEVICE "${CMSIS}/Device/ST/STM32F4xx")
set(HAL "${STM32CUBE_F4}/Drivers/STM32F4xx_HAL_Driver")
set(FAT "${STM32CUBE_F4}/Middlewares/Third_Party/FatFS/src")
if(NOT EXISTS "${FAT}/ff.c")
  set(FAT "${STM32CUBE_F4}/Middlewares/Third_Party/FatFs/src")
endif()
if(NOT EXISTS "${DEVICE}/Source/Templates/gcc/startup_stm32f412zx.s" OR
   NOT EXISTS "${FAT}/ff.c")
  message(FATAL_ERROR "Point STM32CUBE_F4 at a full STM32CubeF4 SDK")
endif()

target_compile_options(sbasic_core PRIVATE -Os -ffunction-sections -fdata-sections -fstack-usage)
add_library(stm32_sdk STATIC
  "${DEVICE}/Source/Templates/system_stm32f4xx.c"
  "${HAL}/Src/stm32f4xx_hal.c"
  "${HAL}/Src/stm32f4xx_hal_cortex.c"
  "${HAL}/Src/stm32f4xx_hal_rcc.c"
  "${HAL}/Src/stm32f4xx_hal_rcc_ex.c"
  "${HAL}/Src/stm32f4xx_hal_gpio.c"
  "${HAL}/Src/stm32f4xx_hal_adc.c"
  "${HAL}/Src/stm32f4xx_hal_dma.c"
  "${HAL}/Src/stm32f4xx_hal_sd.c"
  "${HAL}/Src/stm32f4xx_ll_sdmmc.c"
  "${FAT}/ff.c"
  "${FAT}/option/ccsbcs.c")
target_include_directories(stm32_sdk PUBLIC "${PORT}")
target_include_directories(stm32_sdk SYSTEM PUBLIC
  "${CMSIS}/Include" "${DEVICE}/Include" "${HAL}/Inc" "${FAT}")
target_compile_definitions(stm32_sdk PUBLIC STM32F412Zx USE_HAL_DRIVER)
target_compile_options(stm32_sdk PRIVATE -Os -ffunction-sections -fdata-sections -fstack-usage)
add_executable(sbasic-nucleo
  "${DEVICE}/Source/Templates/gcc/startup_stm32f412zx.s"
  "${PORT}/board.c" "${PORT}/console.c" "${PORT}/card.c"
  "${PORT}/sd_disk.c" "${PORT}/newlib_io.c")
target_compile_options(sbasic-nucleo PRIVATE -Os -ffunction-sections -fdata-sections -fstack-usage
  -Wall -Wextra -Wshadow -Wconversion -Werror)
target_compile_features(sbasic-nucleo PRIVATE c_std_99)
target_link_libraries(sbasic-nucleo PRIVATE sbasic_core stm32_sdk)
set_target_properties(sbasic-nucleo PROPERTIES SUFFIX ".elf" LINK_DEPENDS "${PORT}/stm32f412zg.ld")
target_link_options(sbasic-nucleo PRIVATE --specs=nano.specs --specs=nosys.specs
  -u _printf_float -T "${PORT}/stm32f412zg.ld" -Wl,--gc-sections
  "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/sbasic-nucleo.map")
add_custom_command(TARGET sbasic-nucleo POST_BUILD
  COMMAND "${CMAKE_OBJCOPY}" -O ihex $<TARGET_FILE:sbasic-nucleo> sbasic-nucleo.hex
  COMMAND "${CMAKE_OBJCOPY}" -O binary $<TARGET_FILE:sbasic-nucleo> sbasic-nucleo.bin
  COMMAND "${CMAKE_SIZE}" -A $<TARGET_FILE:sbasic-nucleo>
  WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" VERBATIM)
