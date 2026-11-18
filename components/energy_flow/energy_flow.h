#pragma once
#include <cmath>
#include <cstdint>
#include <string>
#include "esphome/core/component.h"
#include "esphome/components/smart_display/plugin_api.h"

namespace esphome::energy_flow {

// The sensors a tile reads: its options of kind entity, which the app sends under `entities` with these ids.
enum Source : uint8_t { S_GRID, S_PV, S_HOME, S_PV_TODAY, S_COVERAGE, S_FORECAST, S_GRID_TODAY, SOURCES };

// What the app sent, each value in its own unit already turned into W (power), kWh (energy) or % (coverage); NAN for
// a sensor that is not chosen or has no number.
struct Values {
  bool grid_chosen = false;  // a grid sensor is chosen in the tile's settings
  bool grid_energy = false;  // ... but it counts energy (Wh, kWh), not power
  float value[SOURCES] = {NAN, NAN, NAN, NAN, NAN, NAN, NAN};
};

// What the tile and the card show, worked out from the values. Power in W, energy in kWh; NAN for not known.
struct Reading {
  bool have_grid = false;      // the grid sensor has a number
  const char *note = "choose";  // without it: the text key that says why
  float grid = NAN;            // + from the grid, - back to it
  float pv = NAN;              // what the panels make now
  float home = NAN;            // what the house uses now: its own sensor, else grid + pv
  float coverage = NAN;        // the share of the house's use the panels cover now, 0 to 100
  float pv_today = NAN, forecast = NAN, grid_today = NAN;
};

// The plugin: a tile ("flow") and a card ("details").
class EnergyFlow : public Component, public tessera::Plugin {
 public:
  void setup() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  // The values of a tile's data (a tile's or its card's on_state).
  static Values parse(JsonObjectConst data);
  // Everything at once; `invert` for a meter that counts what goes to the grid as positive.
  static Reading read(const Values &v, bool invert);

  // "352 W", "1,24 kW", "3,4 kWh" in the screen's decimal sign.
  std::string power_text(float watts) const;
  std::string energy_text(float kwh) const;

  // What the tile tapped last had, for the card it opens.
  mutable Values card_values;
  mutable bool card_invert = false;
};

}  // namespace esphome::energy_flow
