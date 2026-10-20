#include "energy_flow.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace esphome::energy_flow {

using tessera::Font;
using tessera::Tone;
namespace ui = tessera::ui;

static constexpr uint32_t ICON_SOLAR = 0xF0A72;  // solar-power
static constexpr uint32_t ICON_HOME = 0xF02DC;   // home
static constexpr uint32_t ICON_GRID = 0xF0D3E;   // transmission-tower
static constexpr uint32_t ICON_RIGHT = 0xF0054;  // arrow-right
static constexpr uint32_t ICON_LEFT = 0xF004D;   // arrow-left
static constexpr float FLOWING = 5.0f;           // watts below which nothing counts as flowing

// The colours of the three: the sun amber, the grid blue while it gives, green while it takes.
static constexpr theme::Role SOLAR = theme::MARK_AMBER;
static constexpr theme::Role IMPORT = theme::MARK_BLUE;
static constexpr theme::Role EXPORT = theme::MARK_GREEN;

// ---- Reading the sensors ------------------------------------------------------------------------------------------

static bool known(const std::string &s) { return !s.empty() && s != "unknown" && s != "unavailable" && s != "None"; }

float EnergyFlow::number(int index) const {
  const text_sensor::TextSensor *s = index >= 0 && index < FIELD_COUNT ? fields_[index] : nullptr;
  if (!s || !s->has_state() || !known(s->state)) return NAN;
  char *end = nullptr;
  const float v = strtof(s->state.c_str(), &end);
  return end == s->state.c_str() ? NAN : v;
}

const char *EnergyFlow::unit(int index) const {
  const text_sensor::TextSensor *s = index >= 0 && index < FIELD_COUNT ? fields_[index] : nullptr;
  return s && s->has_state() ? s->state.c_str() : "";
}

// Power in W whatever the sensor's unit (W, kW, MW).
static float watts(float v, const char *unit) {
  if (std::isnan(v)) return v;
  if (strcmp(unit, "kW") == 0) return v * 1000.0f;
  if (strcmp(unit, "MW") == 0) return v * 1000000.0f;
  return v;
}

// Energy in kWh whatever the sensor's unit (Wh, kWh, MWh).
static float kwh(float v, const char *unit) {
  if (std::isnan(v)) return v;
  if (strcmp(unit, "Wh") == 0) return v / 1000.0f;
  if (strcmp(unit, "MWh") == 0) return v * 1000.0f;
  return v;
}

Reading EnergyFlow::read(bool invert) const {
  Reading r;
  r.grid = watts(number(F_GRID), unit(F_GRID_UNIT));
  r.have_grid = !std::isnan(r.grid);
  if (r.have_grid && invert) r.grid = -r.grid;
  r.pv = watts(number(F_PV), unit(F_PV_UNIT));
  if (!std::isnan(r.pv)) r.pv = std::max(0.0f, r.pv);
  if (r.have_grid) r.home = std::max(0.0f, r.grid + (std::isnan(r.pv) ? 0.0f : r.pv));
  r.coverage = number(F_COVERAGE);
  if (std::isnan(r.coverage) && !std::isnan(r.pv) && !std::isnan(r.home))
    r.coverage = r.home > FLOWING ? 100.0f * std::min(r.pv, r.home) / r.home : (r.pv > FLOWING ? 100.0f : 0.0f);
  if (!std::isnan(r.coverage)) r.coverage = std::max(0.0f, std::min(100.0f, r.coverage));
  r.pv_today = kwh(number(F_PV_TODAY), unit(F_PV_TODAY_UNIT));
  r.forecast = kwh(number(F_FORECAST), unit(F_FORECAST_UNIT));
  r.grid_today = kwh(number(F_GRID_TODAY), unit(F_GRID_TODAY_UNIT));
  return r;
}

// "1.24" with the screen's decimal sign.
static std::string decimal(std::string text, const char *sign) {
  if (sign && *sign && *sign != '.')
    for (char &c : text)
      if (c == '.') c = *sign;
  return text;
}

std::string EnergyFlow::power_text(float w) const {
  if (std::isnan(w)) return "–";
  char buf[24];
  const float a = std::fabs(w);
  if (a < 1000.0f) snprintf(buf, sizeof buf, "%d W", static_cast<int>(lroundf(a)));
  else snprintf(buf, sizeof buf, a < 10000.0f ? "%.2f kW" : "%.1f kW", a / 1000.0f);
  return decimal(buf, text("decimal"));
}

std::string EnergyFlow::energy_text(float v) const {
  if (std::isnan(v)) return "–";
  char buf[24];
  snprintf(buf, sizeof buf, v < 100.0f ? "%.1f kWh" : "%.0f kWh", v);
  return decimal(buf, text("decimal"));
}

// ---- Drawing helpers ----------------------------------------------------------------------------------------------

static void place(lv_obj_t *obj, int x, int y, int w, int h) {
  lv_obj_set_size(obj, std::max(1, w), std::max(1, h));
  lv_obj_set_pos(obj, x, y);
}

// Opacities written only when they differ: a write redraws the object, so not on every tick.
static void set_bg_opa(lv_obj_t *obj, lv_opa_t opa) {
  if (lv_obj_get_style_bg_opa(obj, LV_PART_MAIN) != opa) lv_obj_set_style_bg_opa(obj, opa, 0);
}
static void show(lv_obj_t *obj, bool on) {
  const lv_opa_t opa = on ? LV_OPA_COVER : LV_OPA_TRANSP;
  if (lv_obj_get_style_opa(obj, LV_PART_MAIN) != opa) lv_obj_set_style_opa(obj, opa, 0);
}

static int percent(float v) { return static_cast<int>(lroundf(v)); }

// ---- The view: the tile's and the card's drawing ------------------------------------------------------------------

// The three (solar, house, grid) side by side with arrows where the power flows, a bar of how much of the house's use
// the sun covers, and the day so far: what the panels made against the forecast, what came from the grid. Narrow, it
// shows the grid and the sun under each other. Each part only while there is room for it and a value to show.
class FlowView {
 public:
  explicit FlowView(const EnergyFlow *plugin) : plugin_(plugin) {}

  void create(lv_obj_t *parent, int width, int height, bool today, bool large) {
    width_ = width;
    height_ = height;
    today_ = today;
    label_h_ = ui::line_height(Font::BODY);
    value_font_ = large && ui::large() ? Font::VALUE : Font::TITLE;
    value_h_ = ui::line_height(value_font_);
    body_h_ = label_h_;
    gap_ = ui::px(ui::large() ? (large ? 14 : 8) : 5);
    bar_h_ = ui::px(large ? 10 : 6);
    arrow_w_ = ui::line_height(Font::ICON_SMALL);

    const int text_h = label_h_ + value_h_;
    dia_ = std::min(ui::mm(large ? 16 : 12), std::max(ui::line_height(Font::ICON_SMALL) + ui::px(8), text_h));
    dia_ = std::min(dia_, height_);
    big_icon_ = dia_ >= ui::line_height(Font::ICON) + ui::px(10);
    main_h_ = std::max(dia_, text_h);

    // Side by side when three circles and their values fit, with the arrows between them.
    const int need = dia_ + ui::px(8) + ui::text_width("8,88 kW", value_font_);
    wide_ = 3 * need + 2 * (arrow_w_ + ui::px(8)) <= width_;

    for (Block &b : blocks_) {
      b.circle = ui::block(parent, theme::TRACK);
      lv_obj_set_style_radius(b.circle, LV_RADIUS_CIRCLE, 0);
      b.icon = ui::label(parent, big_icon_ ? Font::ICON : Font::ICON_SMALL, theme::MUTED);
      lv_obj_set_style_text_align(b.icon, LV_TEXT_ALIGN_CENTER, 0);
      b.label = ui::label(parent, Font::BODY, theme::MUTED);
      b.value = ui::label(parent, value_font_, theme::INK);
    }
    for (lv_obj_t *&a : arrows_) {
      a = ui::label(parent, Font::ICON_SMALL, theme::MUTED);
      lv_obj_set_style_text_align(a, LV_TEXT_ALIGN_CENTER, 0);
    }
    share_track_ = ui::block(parent, theme::TRACK);
    share_sun_ = ui::block(parent, SOLAR);
    share_grid_ = ui::block(parent, IMPORT);
    share_left_ = ui::label(parent, Font::BODY, theme::MUTED);
    share_right_ = ui::label(parent, Font::BODY, theme::MUTED);
    lv_obj_set_style_text_align(share_right_, LV_TEXT_ALIGN_RIGHT, 0);
    day_ = ui::label(parent, Font::BODY, theme::INK_SOFT);
    fc_track_ = ui::block(parent, theme::TRACK);
    fc_fill_ = ui::block(parent, SOLAR);
    fc_label_ = ui::label(parent, Font::BODY, theme::MUTED);
    note_ = ui::label(parent, Font::BODY_LARGE, theme::MUTED);
    place(note_, 0, (height_ - ui::line_height(Font::BODY_LARGE)) / 2, width_, ui::line_height(Font::BODY_LARGE));
  }

  void paint(const Reading &r) {
    if (!r.have_grid) {
      for (Block &b : blocks_) hide_block(b);
      for (lv_obj_t *a : arrows_) ui::set_text(a, "");
      show(share_track_, false), show(share_sun_, false), show(share_grid_, false), show(fc_track_, false),
          show(fc_fill_, false);
      ui::set_text(share_left_, ""), ui::set_text(share_right_, ""), ui::set_text(day_, ""), ui::set_text(fc_label_, "");
      ui::set_text(note_, plugin_->text("choose"));
      ui::set_color(note_, theme::MUTED);
      shown_ = -1;
      return;
    }
    ui::set_text(note_, "");

    // Which parts there is something for, and of those which fit, in this order: the share bar, the day, the
    // forecast. A change of them places everything again.
    const bool has_pv = !std::isnan(r.pv);
    const std::string day = day_text(r);
    const bool want_share = !std::isnan(r.coverage) && r.home > FLOWING;
    const bool want_day = today_ && !day.empty();
    const bool want_fc = today_ && !std::isnan(r.forecast) && r.forecast > 0 && !std::isnan(r.pv_today);
    int parts = 0, used = wide_ ? main_h_ : compact_main_h(has_pv);
    auto take = [&](bool want, int h, int bit) {
      if (want && used + gap_ + h <= height_) {
        used += gap_ + h;
        parts |= bit;
      }
    };
    take(want_share, share_h(), 1);
    take(want_day, body_h_, 2);
    take(want_fc, bar_h_ + ui::px(4) + body_h_, 4);
    const int layout = parts | (has_pv ? 8 : 0);
    if (layout != shown_) {
      shown_ = layout;
      arrange(parts, has_pv, used);
    }

    paint_blocks(r, has_pv);
    paint_share(r, parts & 1);
    ui::set_text(day_, parts & 2 ? day : "");
    ui::set_color(day_, theme::INK_SOFT);
    paint_forecast(r, parts & 4);
  }

  void on_theme() {
    for (Block &b : blocks_) ui::set_color(b.label, theme::MUTED);
    ui::set_color(share_left_, theme::MUTED);
    ui::set_color(share_right_, theme::MUTED);
    ui::set_color(fc_label_, theme::MUTED);
    ui::set_fill(share_track_, Tone::NORMAL, theme::TRACK);
    ui::set_fill(fc_track_, Tone::NORMAL, theme::TRACK);
  }

 private:
  struct Block {
    lv_obj_t *circle{}, *icon{}, *label{}, *value{};
  };
  enum { SUN, HOME, GRID };

  int share_h() const { return bar_h_ + ui::px(4) + body_h_; }
  // Narrow: the grid's circle and value, the sun's line under it.
  int compact_main_h(bool has_pv) const { return main_h_ + (has_pv ? body_h_ : 0); }

  // Places every part for the parts shown, the whole block in the middle of the height.
  void arrange(int parts, bool has_pv, int used) {
    int y = std::max(0, (height_ - used) / 2);
    const int text_h = label_h_ + value_h_;
    const int ih = ui::line_height(big_icon_ ? Font::ICON : Font::ICON_SMALL);
    auto put_block = [&](Block &b, int x, int w) {
      place(b.circle, x, y + (main_h_ - dia_) / 2, dia_, dia_);
      place(b.icon, x, y + (main_h_ - ih) / 2, dia_, ih);
      const int tx = x + dia_ + ui::px(8), tw = std::max(1, w - dia_ - ui::px(8));
      place(b.label, tx, y + (main_h_ - text_h) / 2, tw, label_h_);
      place(b.value, tx, y + (main_h_ - text_h) / 2 + label_h_, tw, value_h_);
    };
    if (wide_) {
      // Three columns: the sun at the left edge, the house in the middle, the grid at the right edge; the arrows in
      // the gaps between them.
      const int col = (width_ - 2 * (arrow_w_ + ui::px(8))) / 3;
      const int step = col + arrow_w_ + ui::px(8);
      for (int i = 0; i < 3; ++i) put_block(blocks_[i], i * step, col);
      const int ah = ui::line_height(Font::ICON_SMALL);
      for (int i = 0; i < 2; ++i)
        place(arrows_[i], (i + 1) * step - arrow_w_ - ui::px(4), y + (main_h_ - ah) / 2, arrow_w_, ah);
      y += main_h_;
    } else {
      // The grid's block over the whole width, the sun's power in one line under it; no house, no arrows.
      put_block(blocks_[GRID], 0, width_);
      hide_block(blocks_[SUN]);
      hide_block(blocks_[HOME]);
      y += main_h_;
      if (has_pv) {
        place(blocks_[SUN].label, 0, y, width_, body_h_);
        y += body_h_;
      }
    }
    if (parts & 1) {
      y += gap_;
      share_y_ = y;
      place(share_track_, 0, y, width_, bar_h_);
      const int lw = width_ * 2 / 3;
      place(share_left_, 0, y + bar_h_ + ui::px(4), lw, body_h_);
      place(share_right_, width_ - (width_ - lw), y + bar_h_ + ui::px(4), width_ - lw, body_h_);
      y += share_h();
    }
    if (parts & 2) {
      y += gap_;
      place(day_, 0, y, width_, body_h_);
      y += body_h_;
    }
    if (parts & 4) {
      y += gap_;
      fc_y_ = y;
      place(fc_track_, 0, y, width_, bar_h_);
      place(fc_label_, 0, y + bar_h_ + ui::px(4), width_, body_h_);
    }
  }

  void hide_block(Block &b) {
    ui::set_text(b.icon, "");
    ui::set_text(b.label, "");
    ui::set_text(b.value, "");
    show(b.circle, false);
  }

  // A circle tinted in its colour with the icon in full colour while power flows there; grey while not.
  static void paint_circle(Block &b, uint32_t icon, theme::Role role, bool active) {
    show(b.circle, true);
    ui::set_fill(b.circle, Tone::NORMAL, active ? role : theme::TRACK);
    set_bg_opa(b.circle, active ? LV_OPA_20 : LV_OPA_COVER);
    ui::set_text(b.icon, ui::icon(icon));
    ui::set_color(b.icon, active ? role : theme::MUTED);
  }

  void paint_blocks(const Reading &r, bool has_pv) {
    const bool importing = r.grid > FLOWING, exporting = r.grid < -FLOWING;
    const theme::Role grid_role = exporting ? EXPORT : IMPORT;
    Block &g = blocks_[GRID];
    paint_circle(g, ICON_GRID, grid_role, importing || exporting);
    ui::set_text(g.label, plugin_->text(exporting ? "export" : "import"));
    ui::set_color(g.label, theme::MUTED);
    ui::set_text(g.value, plugin_->power_text(r.grid));
    ui::set_color(g.value, theme::INK);

    const bool sunny = has_pv && r.pv > FLOWING;
    Block &s = blocks_[SUN];
    if (wide_) {
      if (has_pv) {
        paint_circle(s, ICON_SOLAR, SOLAR, sunny);
        ui::set_text(s.label, plugin_->text("solar"));
        ui::set_color(s.label, theme::MUTED);
        ui::set_text(s.value, plugin_->power_text(r.pv));
        ui::set_color(s.value, theme::INK);
      } else {
        hide_block(s);
      }
      Block &h = blocks_[HOME];
      paint_circle(h, ICON_HOME, theme::INK, false);
      ui::set_color(h.icon, theme::INK);
      ui::set_text(h.label, plugin_->text("home"));
      ui::set_color(h.label, theme::MUTED);
      ui::set_text(h.value, plugin_->power_text(r.home));
      ui::set_color(h.value, theme::INK);
      // The sun flows to the house; the grid to the house (leftwards) or from it (rightwards).
      ui::set_text(arrows_[0], sunny ? ui::icon(ICON_RIGHT) : "");
      ui::set_color(arrows_[0], SOLAR);
      ui::set_text(arrows_[1], importing ? ui::icon(ICON_LEFT) : exporting ? ui::icon(ICON_RIGHT) : "");
      ui::set_color(arrows_[1], grid_role);
    } else {
      // Narrow: "Solar 1,24 kW" in one line under the grid, amber while the sun gives.
      ui::set_text(s.label, has_pv ? std::string(plugin_->text("solar")) + " " + plugin_->power_text(r.pv) : "");
      ui::set_color(s.label, sunny ? SOLAR : theme::MUTED);
    }
  }

  // The house's use split in what the sun covers (amber) and what comes from the grid (blue).
  void paint_share(const Reading &r, bool on) {
    show(share_track_, on);
    const int sun_w = on ? static_cast<int>(width_ * r.coverage / 100.0f + 0.5f) : 0;
    const int grid_w = on ? width_ - sun_w : 0;
    show(share_sun_, on && sun_w > 0);
    show(share_grid_, on && grid_w > 0 && r.grid > FLOWING);
    if (on) {
      place(share_sun_, 0, share_y_, std::max(1, sun_w), bar_h_);
      place(share_grid_, sun_w, share_y_, std::max(1, grid_w), bar_h_);
      ui::set_fill(share_sun_, Tone::NORMAL, SOLAR);
      ui::set_fill(share_grid_, Tone::NORMAL, IMPORT);
    }
    ui::set_text(share_left_, on ? tessera::format(plugin_->text("coverage"), percent(r.coverage)) : "");
    ui::set_text(share_right_,
                 on && r.grid > FLOWING ? tessera::format(plugin_->text("grid_share"), 100 - percent(r.coverage)) : "");
    ui::set_color(share_left_, theme::MUTED);
    ui::set_color(share_right_, theme::MUTED);
  }

  // What the panels made today against the forecast.
  void paint_forecast(const Reading &r, bool on) {
    show(fc_track_, on);
    show(fc_fill_, on && r.pv_today > 0);
    if (!on) {
      ui::set_text(fc_label_, "");
      return;
    }
    const float share = std::min(1.0f, r.pv_today / r.forecast);
    place(fc_fill_, 0, fc_y_, std::max(1, static_cast<int>(width_ * share + 0.5f)), bar_h_);
    ui::set_fill(fc_fill_, Tone::NORMAL, SOLAR);
    std::string label = tessera::fill(plugin_->text("forecast"), "made", plugin_->energy_text(r.pv_today));
    label = tessera::fill(label.c_str(), "of", plugin_->energy_text(r.forecast));
    ui::set_text(fc_label_, label + " · " + std::to_string(percent(100.0f * r.pv_today / r.forecast)) + " %");
    ui::set_color(fc_label_, theme::MUTED);
  }

  // "Today: solar 3,4 kWh · from the grid 5,2 kWh"; only what is known. Without a forecast bar the sun's day is here.
  std::string day_text(const Reading &r) const {
    std::string out;
    auto add = [&](const char *key, float v) {
      if (std::isnan(v)) return;
      out += out.empty() ? std::string(plugin_->text("today")) + ": " : std::string(" · ");
      out += tessera::fill(plugin_->text(key), "v", plugin_->energy_text(v));
    };
    add("day_solar", r.pv_today);
    add("day_grid", r.grid_today);
    return out;
  }

  const EnergyFlow *plugin_;
  int width_ = 0, height_ = 0, label_h_ = 0, value_h_ = 0, body_h_ = 0, gap_ = 0, bar_h_ = 0, arrow_w_ = 0;
  int dia_ = 0, main_h_ = 0, share_y_ = 0, fc_y_ = 0, shown_ = -1;
  Font value_font_ = Font::TITLE;
  bool today_ = true, wide_ = true, big_icon_ = false;
  Block blocks_[3];
  lv_obj_t *arrows_[2]{};
  lv_obj_t *share_track_{}, *share_sun_{}, *share_grid_{}, *share_left_{}, *share_right_{};
  lv_obj_t *day_{}, *fc_track_{}, *fc_fill_{}, *fc_label_{}, *note_{};
};

// ---- The tile and the card ----------------------------------------------------------------------------------------

class FlowTile : public tessera::Tile {
 public:
  explicit FlowTile(const EnergyFlow *plugin) : plugin_(plugin), view_(plugin) {}

  void create(const tessera::TileContext &c) override {
    tile_ = c.tile;
    invert_ = c.options["invert"] | false;
    view_.create(c.parent, c.width, c.height, c.options["today"] | true, false);
  }
  void on_tick(uint32_t) override { view_.paint(plugin_->read(invert_)); }
  void on_theme() override {
    view_.on_theme();
    on_tick(0);
  }
  void on_tap() override {
    plugin_->card_invert = invert_;
    tessera::open_card(plugin_->plugin_id(), "details", "", tile_);
  }

 private:
  const EnergyFlow *plugin_;
  FlowView view_;
  int tile_ = -1;
  bool invert_ = false;
};

class DetailsCard : public tessera::Card {
 public:
  explicit DetailsCard(const EnergyFlow *plugin) : plugin_(plugin), view_(plugin) {}

  void open(const tessera::CardContext &c) override {
    view_.create(c.parent, c.width, c.height, true, true);
    on_tick(0);
  }
  void on_tick(uint32_t) override { view_.paint(plugin_->read(plugin_->card_invert)); }
  void on_theme() override {
    view_.on_theme();
    on_tick(0);
  }

 private:
  const EnergyFlow *plugin_;
  FlowView view_;
};

void EnergyFlow::setup() {
  add_tile("flow", [this]() { return new FlowTile(this); });
  add_card("details", [this]() { return new DetailsCard(this); }, true);
}

}  // namespace esphome::energy_flow
