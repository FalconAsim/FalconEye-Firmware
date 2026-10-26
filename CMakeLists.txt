# FalconEye kumanda paneli firmware'i
# Ana bilgisayar (PC) derlemesi: çekirdek + simülatör + testler
# STM32 derlemesi: -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake (şematikten sonra eklenecek hedef)
cmake_minimum_required(VERSION 3.16)
project(falconeye_fw C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion)
endif()

# Donanımdan bağımsız çekirdek
add_library(fe_core STATIC
  core/src/tube.c
  core/src/exposure_sm.c
  core/src/error_codes.c
  core/src/calib.c
  core/src/nvstore.c
  core/src/detector.c
  core/src/proto.c
  core/src/hs_line.c
  core/src/service.c
)
target_include_directories(fe_core PUBLIC core/include)
if(NOT CMAKE_CROSSCOMPILING)
  target_link_libraries(fe_core PUBLIC m)
endif()

# Yedek zamanlayıcı (STM32G031 için ayrı firmware; mantığı burada test edilir)
add_library(fe_backup STATIC backup/backup_timer.c)
target_include_directories(fe_backup PUBLIC backup)

if(NOT CMAKE_CROSSCOMPILING)
  enable_testing()
  add_library(fe_sim STATIC sim/hal_sim.c)
  target_include_directories(fe_sim PUBLIC sim)
  target_link_libraries(fe_sim PUBLIC fe_core)

  add_executable(test_tube tests/test_tube.c)
  target_link_libraries(test_tube fe_core)
  add_test(NAME tup_korumasi COMMAND test_tube)

  add_executable(test_exposure tests/test_exposure.c)
  target_link_libraries(test_exposure fe_sim)
  add_test(NAME isinlama_durum_makinesi COMMAND test_exposure)

  add_executable(test_errors tests/test_errors.c)
  target_link_libraries(test_errors fe_core)
  add_test(NAME hata_kodlari COMMAND test_errors)

  add_executable(test_calib tests/test_calib.c)
  target_link_libraries(test_calib fe_core)
  add_test(NAME kalibrasyon_ve_saklama COMMAND test_calib)

  add_executable(test_detector tests/test_detector.c)
  target_link_libraries(test_detector fe_sim)
  add_test(NAME dedektor_profilleri COMMAND test_detector)

  add_executable(test_proto tests/test_proto.c)
  target_link_libraries(test_proto fe_sim)
  add_test(NAME jenerator_protokolu COMMAND test_proto)

  add_executable(test_service tests/test_service.c)
  target_link_libraries(test_service fe_sim)
  add_test(NAME servis_menusu COMMAND test_service)

  add_executable(test_hs_line tests/test_hs_line.c)
  target_link_libraries(test_hs_line fe_sim)
  add_test(NAME el_butonu_hatti COMMAND test_hs_line)

  add_executable(test_backup tests/test_backup.c)
  target_link_libraries(test_backup fe_backup)
  add_test(NAME yedek_zamanlayici COMMAND test_backup)

  # Kullanım kılavuzu hata kodu tablosunu üretir
  add_executable(gen_error_table tools/gen_error_table.c)
  target_link_libraries(gen_error_table fe_core)
endif()
