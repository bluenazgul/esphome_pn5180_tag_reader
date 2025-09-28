# esphome_pn5180_tag_reader
PN5180 ESPhome Tag Reader for Home Assistant

Used Hardware: https://github.com/Hasenpups/PN5180_ESP_Reader/tree/main

PN5180 Library: https://github.com/tueddy/PN5180-Library

Since there is no official Library for PN5180 in ESPHome i needed to get an external Library, this used Library is optimized for ESP32 so it was needed to patch it (see /esphome/fix_pn5180_spi_esp8266.py) for details
