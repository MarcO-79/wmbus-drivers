/*
  Based on: https://github.com/wmbusmeters/wmbusmeters/blob/master/src/driver_qheat.cc
  Copyright (C) 2021-2022 Fredrik Öhrström (gpl-3.0-or-later)
*/

#pragma once

#include "driver.h"
#include <vector>
#include <string>

struct Qheat : Driver
{
  Qheat(std::string key = "") : Driver(std::string("qheat"), key){};

  virtual esphome::optional<std::map<std::string, double>> get_values(std::vector<unsigned char> &telegram) override {
    std::map<std::string, double> ret_val{};

    auto current_gj = this->get_current_hca(telegram);
    auto last_month_gj = this->get_last_month_hca(telegram);

    // 1. Aktualne zużycie całkowite (GJ) -> np. 1.1175 GJ
    add_to_map(ret_val, "total_energy_consumption_gj", current_gj);

    // 2. Zużycie na koniec poprzedniego miesiąca (GJ) -> np. 0.7182 GJ
    // Używamy wbudowanego w komponent klucza "previous_heating_kwh" (jednostkę nadpisujemy w YAML na GJ)
    add_to_map(ret_val, "previous_heating_kwh", last_month_gj);

    // 3. Zużycie w bieżącym miesiącu (GJ) -> wyliczona różnica (np. 1.1175 - 0.7182 = 0.3993 GJ)
    // Używamy wbudowanego w komponent klucza "current_heating_kwh"
    if (current_gj.has_value() && last_month_gj.has_value() && current_gj.value() >= last_month_gj.value()) {
      add_to_map(ret_val, "current_heating_kwh", esphome::optional<double>(current_gj.value() - last_month_gj.value()));
    }

    if (ret_val.size() > 0) {
      return ret_val;
    }
    else {
      return {};
    }
  };

private:
  bool is_valid_bcd4(std::vector<unsigned char> &t, size_t idx) {
    if (idx + 3 >= t.size()) return false;
    for (size_t k = 0; k < 4; k++) {
      if (((t[idx + k] >> 4) > 9) || ((t[idx + k] & 0x0F) > 9)) return false;
    }
    return true;
  }

  // Odczyt aktualnego zużycia (GJ)
  esphome::optional<double> get_current_hca(std::vector<unsigned char> &telegram) {
    // 1. Ramka długa (89 bajtów): wartość leży po sekwencji C0 0D FF FF (4 bajty BCD)
    for (size_t i = 20; i + 8 < telegram.size(); i++) {
      if (telegram[i] == 0xC0 && telegram[i+1] == 0x0D && telegram[i+2] == 0xFF && telegram[i+3] == 0xFF) {
        if (is_valid_bcd4(telegram, i + 4)) {
          return (double)bcd_2_int(telegram, i + 4, 4) / 10000.0;
        }
      }
    }
    // 2. Ramka standardowa (66 bajtów): wartość leży po 0C 0D (4 bajty BCD)
    for (size_t i = 18; i + 6 < telegram.size(); i++) {
      if (telegram[i] == 0x0C && telegram[i+1] == 0x0D) {
        if (is_valid_bcd4(telegram, i + 2)) {
          return (double)bcd_2_int(telegram, i + 2, 4) / 10000.0;
        }
      }
    }
    return {};
  }

  // Odczyt zużycia na koniec poprzedniego miesiąca (GJ)
  esphome::optional<double> get_last_month_hca(std::vector<unsigned char> &telegram) {
    // 1. Ramka długa (89 bajtów): wartość z końca poprzedniego miesiąca leży 16 bajtów za początkiem C0 0D FF FF
    // Układ: [C0 0D FF FF] (4B) + [Current BCD] (4B) + [FF FF 00 00 00 00] (6B) + [Data CP16] (2B) + [Last Month BCD] (4B)
    for (size_t i = 20; i + 20 < telegram.size(); i++) {
      if (telegram[i] == 0xC0 && telegram[i+1] == 0x0D && telegram[i+2] == 0xFF && telegram[i+3] == 0xFF) {
        if (is_valid_bcd4(telegram, i + 16)) {
          return (double)bcd_2_int(telegram, i + 16, 4) / 10000.0;
        }
      }
    }
    // 2. Ramka standardowa (66 bajtów): wartość po sekwencji 4C 0D (Storage 1, 4 bajty BCD)
    for (size_t i = 18; i + 6 < telegram.size(); i++) {
      if (telegram[i] == 0x4C && telegram[i+1] == 0x0D) {
        if (is_valid_bcd4(telegram, i + 2)) {
          return (double)bcd_2_int(telegram, i + 2, 4) / 10000.0;
        }
      }
    }
    return {};
  }
};
