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

    // 1. Aktualne zużycie (GJ) -> w Twojej ramce: 1.1175 GJ
    add_to_map(ret_val, "total_energy_consumption_gj", current_gj);

    // 2. Zużycie na koniec poprzedniego miesiąca (GJ) -> w Twojej ramce (31.10.2026): 0.7182 GJ
    add_to_map(ret_val, "last_month_energy_gj", last_month_gj);

    // 3. Zużycie w bieżącym miesiącu (różnica: aktualne - koniec poprz. miesiąca) -> 0.3993 GJ
    if (current_gj.has_value() && last_month_gj.has_value() && current_gj.value() >= last_month_gj.value()) {
      add_to_map(ret_val, "current_month_energy_gj", esphome::optional<double>(current_gj.value() - last_month_gj.value()));
    }

    // 4. Flagi błędów (02 FD 17 XX XX) -> w Twojej ramce: 0
    add_to_map(ret_val, "error_flags", this->get_error_flags(telegram));

    // 5. Pola z ramki 66-bajtowej (jeśli się pojawią)
    add_to_map(ret_val, "flow_temperature_c", this->get_temperature(telegram, 0x5A));
    add_to_map(ret_val, "return_temperature_c", this->get_temperature(telegram, 0x5E));

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

  bool is_valid_bcd2(std::vector<unsigned char> &t, size_t idx) {
    if (idx + 1 >= t.size()) return false;
    for (size_t k = 0; k < 2; k++) {
      if (((t[idx + k] >> 4) > 9) || ((t[idx + k] & 0x0F) > 9)) return false;
    }
    return true;
  }

  // Aktualne zużycie (GJ)
  esphome::optional<double> get_current_hca(std::vector<unsigned char> &telegram) {
    // 1. Ramka długa (89 bajtów): po C0 0D FF FF (offset +4) -> 75 11 01 00 = 1.1175 GJ
    for (size_t i = 20; i + 8 < telegram.size(); i++) {
      if (telegram[i] == 0xC0 && telegram[i+1] == 0x0D && telegram[i+2] == 0xFF && telegram[i+3] == 0xFF) {
        if (is_valid_bcd4(telegram, i + 4)) {
          return (double)bcd_2_int(telegram, i + 4, 4) / 10000.0;
        }
      }
    }
    // 2. Ramka standardowa (66 bajtów): po 0C 0D
    for (size_t i = 18; i + 6 < telegram.size(); i++) {
      if (telegram[i] == 0x0C && telegram[i+1] == 0x0D) {
        if (is_valid_bcd4(telegram, i + 2)) {
          return (double)bcd_2_int(telegram, i + 2, 4) / 10000.0;
        }
      }
    }
    return {};
  }

  // Zużycie na koniec poprzedniego miesiąca (GJ)
  esphome::optional<double> get_last_month_hca(std::vector<unsigned char> &telegram) {
    // 1. W ramce 89-bajtowej wartość z końca poprzedniego miesiąca leży 16 bajtów za początkiem C0 0D FF FF
    // Struktura: [C0 0D FF FF] (4B) + [Current BCD] (4B) + [FF FF 00 00 00 00] (6B) + [Date CP16] (2B) + [Last Month BCD] (4B)
    for (size_t i = 20; i + 20 < telegram.size(); i++) {
      if (telegram[i] == 0xC0 && telegram[i+1] == 0x0D && telegram[i+2] == 0xFF && telegram[i+3] == 0xFF) {
        if (is_valid_bcd4(telegram, i + 16)) {
          return (double)bcd_2_int(telegram, i + 16, 4) / 10000.0;
        }
      }
    }
    // 2. W ramce 66-bajtowej (jeśli występuje jako standardowe 4C 0D)
    for (size_t i = 18; i + 6 < telegram.size(); i++) {
      if (telegram[i] == 0x4C && telegram[i+1] == 0x0D) {
        if (is_valid_bcd4(telegram, i + 2)) {
          return (double)bcd_2_int(telegram, i + 2, 4) / 10000.0;
        }
      }
    }
    return {};
  }

  // Flagi błędów (02 FD 17 XX XX)
  esphome::optional<double> get_error_flags(std::vector<unsigned char> &telegram) {
    for (size_t i = 20; i + 4 < telegram.size(); i++) {
      if (telegram[i] == 0x02 && telegram[i+1] == 0xFD && telegram[i+2] == 0x17) {
        uint16_t err = (telegram[i+4] << 8) | telegram[i+3];
        return (double)err;
      }
    }
    return {};
  }

  // Temperatury dla ramki 66-bajtowej (jeśli występują)
  esphome::optional<double> get_temperature(std::vector<unsigned char> &telegram, unsigned char vif_base) {
    for (size_t i = 18; i + 3 < telegram.size(); i++) {
      if (telegram[i] == 0x0A && (telegram[i+1] == vif_base || telegram[i+1] == vif_base + 1)) {
        if (is_valid_bcd2(telegram, i + 2)) {
          double divider = (telegram[i+1] == vif_base) ? 10.0 : 1.0;
          return (double)bcd_2_int(telegram, i + 2, 2) / divider;
        }
      }
    }
    return {};
  }
};
