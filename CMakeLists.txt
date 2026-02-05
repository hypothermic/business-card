# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(capsense)

target_sources(
    app PRIVATE
        src/main.c
        src/sense.c
        src/ble.c
        src/usbms.c
        src/sample_usbd_init.c
)

set(gen_dir ${ZEPHYR_BINARY_DIR}/include/generated/)

generate_inc_file_for_target(app data/LinkedIn.url ${gen_dir}/LinkedIn.url.inc)
generate_inc_file_for_target(app data/GitHub.url   ${gen_dir}/GitHub.url.inc)
generate_inc_file_for_target(app data/README.txt   ${gen_dir}/README.txt.inc)
generate_inc_file_for_target(app data/cv-2025.pdf  ${gen_dir}/cv.pdf.inc)
