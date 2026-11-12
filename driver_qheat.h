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

    // 1. Aktualne zużycie energii (GJ)
    add_to_map(ret_val, "total_energy_consumption_gj", this->get_current_energy(telegram));
    
    // 2. Zużycie historyczne - poprzedni okres / miesiąc (GJ)
    add_to_map(ret_val, "previous_energy_consumption_gj", this->get_previous_energy(telegram));
    
    // 3. Całkowita objętość przepływu (m3)
    add_to_map(ret_val, "total_volume_m3", this->get_volume(telegram));

    // 4. Temperatury zasilania i powrotu (°C)
    add_to_map(ret_val, "flow_temperature_c", this->get_temperature(telegram, 0x5A));
    add_to_map(ret_val, "return_temperature_c", this->get_temperature(telegram, 0x5E));

    // 5. Moc chwilowa (kW)
    add_to_map(ret_val, "current_power_kw", this->get_power(telegram));

    if (ret_val.size() > 0) {
      return ret_val;
    }
    else {
      return {};
    }
  };

private:
  // Walidacja N bajtów BCD
  bool is_valid_bcd(std::vector<unsigned char> &t, size_t idx, size_t len) {
    if (idx + len - 1 >= t.size()) return false;
    for (size_t k = 0; k < len; k++) {
      if (((t[idx + k] >> 4) > 9) || ((t[idx + k] & 0x0F) > 9)) return false;
    }
    return true;
  }

  // Odczyt 16-bitowego inta (Little Endian) dla rejestrów binarnych (DIF 0x02)
  int16_t get_int16_le(std::vector<unsigned char> &t, size_t idx) {
    return (int16_t)((t[idx + 1] << 8) | t[idx]);
  }

  // --- Aktualna energia (GJ) ---
  esphome::optional<double> get_current_energy(std::vector<unsigned char> &telegram) {
    // 1. Ramka długa (89 bajtów): po sekwencji C0 0D FF FF
    for (size_t i = 20; i + 8 < telegram.size(); i++) {
      if (telegram[i] == 0xC0 && telegram[i+1] == 0x0D && telegram[i+2] == 0xFF && telegram[i+3] == 0xFF) {
        if (is_valid_bcd(telegram, i + 4, 4)) {
          return (double)bcd_2_int(telegram, i + 4, 4) / 10000.0;
        }
      }
    }
    // 2. Ramka standardowa (66 bajtów): po sekwencji 0C 0D
    for (size_t i = 18; i + 6 < telegram.size(); i++) {
      if (telegram[i] == 0x0C && telegram[i+1] == 0x0D) {
        if (is_valid_bcd(telegram, i + 2, 4)) {
          return (double)bcd_2_int(telegram, i + 2, 4) / 10000.0;
        }
      }
    }
    return {};
  }

  // --- Energia historyczna - Storage 1 (GJ) ---
  esphome::optional<double> get_previous_energy(std::vector<unsigned char> &telegram) {
    for (size_t i = 18; i + 6 < telegram.size(); i++) {
      // Standardowy zapis M-Bus: DIF=0x4C (BCD 8 cyfr, Storage 1), VIF=0x0D (GJ * 0.0001)
      if (telegram[i] == 0x4C && telegram[i+1] == 0x0D) {
        if (is_valid_bcd(telegram, i + 2, 4)) {
          return (double)bcd_2_int(telegram, i + 2, 4) / 10000.0;
        }
      }
      // Alternatywny zapis w niektórych rewizjach Qheat (DIF=0x8C, Storage 2, sub-unit 0x01)
      if (i + 7 < telegram.size() && telegram[i] == 0x8C && (telegram[i+1] == 0x01 || telegram[i+1] == 0x10) && telegram[i+2] == 0x0D) {
        if (is_valid_bcd(telegram, i + 3, 4)) {
          return (double)bcd_2_int(telegram, i + 3, 4) / 10000.0;
        }
      }
    }
    return {};
  }

  // --- Objętość całkowita (m3) ---
  esphome::optional<double> get_volume(std::vector<unsigned char> &telegram) {
    for (size_t i = 18; i + 6 < telegram.size(); i++) {
      if (telegram[i] == 0x0C) {
        // VIF 0x13 (0.001 m3), 0x14 (0.01 m3), 0x15 (0.1 m3)
        if (telegram[i+1] >= 0x13 && telegram[i+1] <= 0x15) {
          if (is_valid_bcd(telegram, i + 2, 4)) {
            double divider = (telegram[i+1] == 0x13) ? 1000.0 : ((telegram[i+1] == 0x14) ? 100.0 : 10.0);
            return (double)bcd_2_int(telegram, i + 2, 4) / divider;
          }
        }
      }
    }
    return {};
  }

  // --- Temperatura zasilania (vif_base = 0x5A) lub powrotu (vif_base = 0x5E) ---
  esphome::optional<double> get_temperature(std::vector<unsigned char> &telegram, unsigned char vif_base) {
    for (size_t i = 18; i + 4 < telegram.size(); i++) {
      // Wariant BCD (2 bajty = 4 cyfry BCD, DIF = 0x0A)
      if (telegram[i] == 0x0A && (telegram[i+1] == vif_base || telegram[i+1] == vif_base + 1)) {
        if (is_valid_bcd(telegram, i + 2, 2)) {
          double divider = (telegram[i+1] == vif_base) ? 10.0 : 1.0; // 0x5A = 0.1°C, 0x5B = 1°C
          return (double)bcd_2_int(telegram, i + 2, 2) / divider;
        }
      }
      // Wariant Int16 (2 bajty binarne, DIF = 0x02)
      if (telegram[i] == 0x02 && (telegram[i+1] == vif_base || telegram[i+1] == vif_base + 1)) {
        double divider = (telegram[i+1] == vif_base) ? 10.0 : 1.0;
        int16_t raw = get_int16_le(telegram, i + 2);
        if (raw > -200 && raw < 1800) { // Filtr sanity-check (-20.0°C do 180.0°C)
          return (double)raw / divider;
        }
      }
    }
    return {};
  }

  // --- Moc chwilowa (kW) ---
  esphome::optional<double> get_power(std::vector<unsigned char> &telegram) {
    for (size_t i = 18; i + 5 < telegram.size(); i++) {
      // DIF 0x0B (3 bajty BCD), VIF 0x2D (100 W = 0.1 kW) lub 0x2B (1 W = 0.001 kW)
      if (telegram[i] == 0x0B && (telegram[i+1] == 0x2D || telegram[i+1] == 0x2B)) {
        if (is_valid_bcd(telegram, i + 2, 3)) {
          double divider = (telegram[i+1] == 0x2D) ? 10.0 : 1000.0;
          return (double)bcd_2_int(telegram, i + 2, 3) / divider;
        }
      }
    }
    return {};
  }
};
