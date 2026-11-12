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
    auto last_month_state_gj = this->get_last_month_state(telegram);

    // 1. Aktualne zużycie całkowite (GJ) -> 1.1176 GJ (z obu ramek)
    add_to_map(ret_val, "total_energy_consumption_gj", current_gj);

    // 2. Stan licznika na koniec poprzedniego miesiąca (GJ) -> 0.7182 GJ (z obu ramek)
    add_to_map(ret_val, "previous_heating_kwh", last_month_state_gj);

    // 3. Zużycie w bieżącym miesiącu (GJ) -> 0.3994 GJ (z obu ramek)
    if (current_gj.has_value() && last_month_state_gj.has_value() && current_gj.value() >= last_month_state_gj.value()) {
      add_to_map(ret_val, "current_heating_kwh", esphome::optional<double>(current_gj.value() - last_month_state_gj.value()));
    }

    // 4. Zużycie netto w poprzednim miesiącu (Październik: 0.6412 GJ) -> z tabeli 12 m-cy w ramce 89B
    add_to_map(ret_val, "last_month_total_water_m3", this->get_history_month_delta(telegram, 12));

    // 5. Zużycie netto 2 miesiące temu (Wrzesień: 0.0770 GJ) -> z tabeli 12 m-cy w ramce 89B
    add_to_map(ret_val, "current_month_total_water_l", this->get_history_month_delta(telegram, 11));

    // 6. Kod błędu urządzenia (02 FD 17 XX XX) -> 0 (z obu ramek)
    add_to_map(ret_val, "total_water_m3", this->get_error_flags(telegram));

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

  // 1. Aktualne zużycie całkowite (GJ)
  esphome::optional<double> get_current_hca(std::vector<unsigned char> &telegram) {
    // Ramka 89B: po C0 0D FF FF
    for (size_t i = 20; i + 8 < telegram.size(); i++) {
      if (telegram[i] == 0xC0 && telegram[i+1] == 0x0D && telegram[i+2] == 0xFF && telegram[i+3] == 0xFF) {
        if (is_valid_bcd4(telegram, i + 4)) {
          return (double)bcd_2_int(telegram, i + 4, 4) / 10000.0;
        }
      }
    }
    // Ramka 66B: po 0C 0D
    for (size_t i = 18; i + 6 < telegram.size(); i++) {
      if (telegram[i] == 0x0C && telegram[i+1] == 0x0D) {
        if (is_valid_bcd4(telegram, i + 2)) {
          return (double)bcd_2_int(telegram, i + 2, 4) / 10000.0;
        }
      }
    }
    return {};
  }

  // 2. Stan licznika na koniec poprzedniego miesiąca (GJ)
  esphome::optional<double> get_last_month_state(std::vector<unsigned char> &telegram) {
    // Ramka 89B: 16 bajtów za C0 0D FF FF (82 71 00 00 -> 0.7182 GJ)
    for (size_t i = 20; i + 20 < telegram.size(); i++) {
      if (telegram[i] == 0xC0 && telegram[i+1] == 0x0D && telegram[i+2] == 0xFF && telegram[i+3] == 0xFF) {
        if (is_valid_bcd4(telegram, i + 16)) {
          return (double)bcd_2_int(telegram, i + 16, 4) / 10000.0;
        }
      }
    }
    // Ramka 66B: po CC 08 0D (82 71 00 00 -> 0.7182 GJ)
    for (size_t i = 18; i + 7 < telegram.size(); i++) {
      if (telegram[i] == 0xCC && telegram[i+1] == 0x08 && telegram[i+2] == 0x0D) {
        if (is_valid_bcd4(telegram, i + 3)) {
          return (double)bcd_2_int(telegram, i + 3, 4) / 10000.0;
        }
      }
    }
    return {};
  }

  // 4 & 5. Zużycie miesięczne z 12-miesięcznej tabeli historii w ramce 89B (slot_1_to_12: 12 = poprz. miesiąc, 11 = 2 m-ce temu)
  esphome::optional<double> get_history_month_delta(std::vector<unsigned char> &telegram, size_t slot_1_to_12) {
    if (slot_1_to_12 < 1 || slot_1_to_12 > 12) return {};
    for (size_t i = 20; i + 44 < telegram.size(); i++) {
      if (telegram[i] == 0xC0 && telegram[i+1] == 0x0D && telegram[i+2] == 0xFF && telegram[i+3] == 0xFF) {
        // Tabela 12 slotów po 2 bajty (uint16_t Little Endian) zaczyna się 20 bajtów za początkiem C0 0D FF FF
        size_t pos = i + 20 + (slot_1_to_12 - 1) * 2;
        if (pos + 1 < telegram.size()) {
          uint16_t val = (telegram[pos + 1] << 8) | telegram[pos];
          if (val != 0x8000) { // 0x8000 oznacza pusty slot (brak odczytu w danym miesiącu)
            return (double)val / 10000.0;
          }
        }
      }
    }
    return {};
  }

  // 6. Kod błędu (02 FD 17 XX XX)
  esphome::optional<double> get_error_flags(std::vector<unsigned char> &telegram) {
    for (size_t i = 20; i + 4 < telegram.size(); i++) {
      if (telegram[i] == 0x02 && telegram[i+1] == 0xFD && telegram[i+2] == 0x17) {
        uint16_t err = (telegram[i + 4] << 8) | telegram[i + 3];
        return (double)err;
      }
    }
    return {};
  }
};
