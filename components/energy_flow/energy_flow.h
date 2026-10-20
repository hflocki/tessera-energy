#pragma once
#include <cmath>
#include <cstdint>
#include <string>
#include "esphome/core/component.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/smart_display/plugin_api.h"

namespace esphome::energy_flow {

// The values plugin.yaml reads, in the order of FIELDS in __init__.py.
enum Field : uint8_t {
  F_GRID, F_GRID_UNIT, F_PV, F_PV_UNIT, F_PV_TODAY, F_PV_TODAY_UNIT, F_COVERAGE, F_FORECAST, F_FORECAST_UNIT,
  F_GRID_TODAY, F_GRID_TODAY_UNIT, FIELD_COUNT
};

// What the tile and the card show, worked out from the sensors. Power in W, energy in kWh; NAN for not known.
struct Reading {
  bool have_grid = false;  // the grid sensor has a number
  float grid = NAN;        // + from the grid, - back to it
  float pv = NAN;          // what the panels make now
  float home = NAN;        // what the house uses now: grid + pv
  float coverage = NAN;    // the share of the house's use the panels cover now, 0 to 100
  float pv_today = NAN, forecast = NAN, grid_today = NAN;
};

// The plugin: a tile ("flow") and a card ("details"). It holds the text sensors plugin.yaml reads.
class EnergyFlow : public Component, public tessera::Plugin {
 public:
  void setup() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_field(int index, text_sensor::TextSensor *s) {
    if (index >= 0 && index < FIELD_COUNT) fields_[index] = s;
  }
  // Everything at once; `invert` for a meter that counts what goes to the grid as positive.
  Reading read(bool invert) const;

  // "352 W", "1,24 kW", "3,4 kWh" in the screen's decimal sign.
  std::string power_text(float watts) const;
  std::string energy_text(float kwh) const;

  // The tile's "invert" for the card it opens.
  mutable bool card_invert = false;

 protected:
  float number(int index) const;
  const char *unit(int index) const;
  text_sensor::TextSensor *fields_[FIELD_COUNT]{};
};

}  // namespace esphome::energy_flow
