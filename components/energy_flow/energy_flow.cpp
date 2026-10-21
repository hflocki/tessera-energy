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

// ---- Readable colours ---------------------------------------------------------------------------------------------

// A colour's luminance and the WCAG contrast of two: amber or yellow words on the light look's white card fall under
// what a person reads, so there they take the ink instead; on the dark look they keep their colour.
static float luminance(lv_color_t c) {
  auto channel = [](uint8_t v) {
    const float s = v / 255.0f;
    return s <= 0.03928f ? s / 12.92f : std::pow((s + 0.055f) / 1.055f, 2.4f);
  };
  return 0.2126f * channel(c.red) + 0.7152f * channel(c.green) + 0.0722f * channel(c.blue);
}
static bool reads_on_card(lv_color_t c) {
  const float a = luminance(c), b = luminance(ui::color(theme::CARD));
  return (std::max(a, b) + 0.05f) / (std::min(a, b) + 0.05f) >= 3.0f;
}
// A label (words or an icon) in a tone's or a role's colour where that reads on the card, else in the ink.
static void set_readable(lv_obj_t *label, tessera::Tone tone, theme::Role role) {
  if (reads_on_card(ui::tone_color(tone, role))) ui::set_tone(label, tone, role);
  else ui::set_color(label, theme::INK);
}

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

// A label inside a circle: a child of it, so the two are one part, centred in it.
static void inside(lv_obj_t *circle, lv_obj_t *label, int w, int h, int dy = 0) {
  lv_obj_set_style_pad_all(circle, 0, 0);
  lv_obj_remove_flag(circle, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(label, std::max(1, w), std::max(1, h));
  lv_obj_align(label, LV_ALIGN_CENTER, 0, dy);
}

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
// Shown or hidden, only when it differs. A hidden part is not drawn and takes no room.
static void show(lv_obj_t *obj, bool on) {
  if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN) == on) {
    if (on) lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
  }
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
      b.icon = ui::label(b.circle, big_icon_ ? Font::ICON : Font::ICON_SMALL, theme::MUTED);
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
    // Nothing shows until the first paint has placed it.
    for (Block &b : blocks_) hide_block(b);
    for (lv_obj_t *o : {arrows_[0], arrows_[1], share_track_, share_sun_, share_grid_, share_left_, share_right_, day_,
                        fc_track_, fc_fill_, fc_label_})
      show(o, false);
  }

  void paint(const Reading &r) {
    if (!r.have_grid) {
      for (Block &b : blocks_) hide_block(b);
      for (lv_obj_t *o : {arrows_[0], arrows_[1], share_track_, share_sun_, share_grid_, share_left_, share_right_, day_,
                          fc_track_, fc_fill_, fc_label_})
        show(o, false);
      show(note_, true);
      ui::set_text(note_, plugin_->text("choose"));
      ui::set_color(note_, theme::MUTED);
      shown_ = -1;
      return;
    }
    ui::set_text(note_, "");
    show(note_, false);

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
    show(day_, parts & 2);
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
      inside(b.circle, b.icon, dia_, ih);
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
    show(b.circle, false);  // its icon with it
    show(b.label, false);
    show(b.value, false);
  }
  void show_block(Block &b) {
    show(b.circle, true);
    show(b.label, true);
    show(b.value, true);
  }

  // A circle tinted in its colour with the icon in full colour while power flows there; grey while not.
  static void paint_circle(Block &b, uint32_t icon, theme::Role role, bool active) {
    show(b.circle, true);
    ui::set_fill(b.circle, Tone::NORMAL, active ? role : theme::TRACK);
    set_bg_opa(b.circle, active ? LV_OPA_20 : LV_OPA_60);
    ui::set_text(b.icon, ui::icon(icon));
    if (active) set_readable(b.icon, Tone::NORMAL, role);
    else ui::set_color(b.icon, theme::MUTED);
  }

  void paint_blocks(const Reading &r, bool has_pv) {
    const bool importing = r.grid > FLOWING, exporting = r.grid < -FLOWING;
    const theme::Role grid_role = exporting ? EXPORT : IMPORT;
    Block &g = blocks_[GRID];
    show_block(g);
    paint_circle(g, ICON_GRID, grid_role, importing || exporting);
    ui::set_text(g.label, plugin_->text(exporting ? "export" : "import"));
    ui::set_color(g.label, theme::MUTED);
    ui::set_text(g.value, plugin_->power_text(r.grid));
    ui::set_color(g.value, theme::INK);

    const bool sunny = has_pv && r.pv > FLOWING;
    Block &s = blocks_[SUN];
    if (wide_) {
      if (has_pv) {
        show_block(s);
        paint_circle(s, ICON_SOLAR, SOLAR, sunny);
        ui::set_text(s.label, plugin_->text("solar"));
        ui::set_color(s.label, theme::MUTED);
        ui::set_text(s.value, plugin_->power_text(r.pv));
        ui::set_color(s.value, theme::INK);
      } else {
        hide_block(s);
      }
      Block &h = blocks_[HOME];
      show_block(h);
      paint_circle(h, ICON_HOME, theme::INK, false);
      ui::set_color(h.icon, theme::INK);
      ui::set_text(h.label, plugin_->text("home"));
      ui::set_color(h.label, theme::MUTED);
      ui::set_text(h.value, plugin_->power_text(r.home));
      ui::set_color(h.value, theme::INK);
      // The sun flows to the house; the grid to the house (leftwards) or from it (rightwards).
      show(arrows_[0], sunny);
      show(arrows_[1], importing || exporting);
      ui::set_text(arrows_[0], sunny ? ui::icon(ICON_RIGHT) : "");
      set_readable(arrows_[0], Tone::NORMAL, SOLAR);
      ui::set_text(arrows_[1], importing ? ui::icon(ICON_LEFT) : exporting ? ui::icon(ICON_RIGHT) : "");
      set_readable(arrows_[1], Tone::NORMAL, grid_role);
    } else {
      // Narrow: "Solar 1,24 kW" in one line under the grid, amber while the sun gives.
      show(s.label, has_pv);
      ui::set_text(s.label, has_pv ? std::string(plugin_->text("solar")) + " " + plugin_->power_text(r.pv) : "");
      if (sunny) set_readable(s.label, Tone::NORMAL, SOLAR);
      else ui::set_color(s.label, theme::MUTED);
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
    show(share_left_, on);
    show(share_right_, on);
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
    show(fc_label_, on);
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

// ---- The tile ----------------------------------------------------------------------------------------

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

// ---- The card ------------------------------------------------------------------------------------------------------

static constexpr uint32_t ICON_PANEL = 0xF0D9B;  // solar-panel
static constexpr uint32_t ICON_LEAF = 0xF032A;   // leaf
static constexpr uint32_t ICON_SUNNY = 0xF0599;  // weather-sunny
static constexpr uint32_t ICON_METER = 0xF1A57;  // meter-electric

static void set_border(lv_obj_t *obj, theme::Role role) {
  const lv_color_t c = ui::color(role);
  if (!lv_color_eq(lv_obj_get_style_border_color(obj, LV_PART_MAIN), c)) lv_obj_set_style_border_color(obj, c, 0);
}

// A tap on the tile: at the left the flow as in Home Assistant's energy card (the sun above, the grid and the house
// below, joined by lines with a dot where power flows), at the right every sensor in a row of its own.
class DetailsCard : public tessera::Card {
 public:
  explicit DetailsCard(const EnergyFlow *plugin) : plugin_(plugin) {}

  void open(const tessera::CardContext &c) override {
    width_ = c.width;
    height_ = c.height;
    invert_ = plugin_->card_invert;
    const bool large = ui::large();
    line_w_ = std::max(2, ui::px(large ? 3 : 2));
    ring_w_ = std::max(2, ui::px(large ? 4 : 3));
    dot_ = ui::px(large ? 12 : 8);
    label_h_ = ui::line_height(Font::BODY);
    value_h_ = ui::line_height(Font::BODY_LARGE);

    // The diagram takes the left part, as wide as its height allows; the list the rest.
    const int gap = ui::px(large ? 32 : 16);
    diagram_w_ = std::min(width_ * 11 / 20, height_ * 3 / 2);
    list_x_ = diagram_w_ + gap;
    list_w_ = width_ - list_x_;
    dia_ = std::min({diagram_w_ * 10 / 32, (height_ - 2 * label_h_) * 10 / 22, ui::mm(30)});

    // Lines first, so the circles lie over their ends.
    for (lv_obj_t *&l : lines_) l = ui::block(c.parent, theme::TRACK);
    for (lv_obj_t *&d : dots_) {
      d = ui::block(c.parent, theme::TRACK);
      lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
    }
    for (Node &n : nodes_) {
      n.ring = ui::block(c.parent, theme::CARD);
      lv_obj_set_style_radius(n.ring, LV_RADIUS_CIRCLE, 0);
      lv_obj_set_style_border_width(n.ring, ring_w_, 0);
      lv_obj_set_style_border_side(n.ring, LV_BORDER_SIDE_FULL, 0);
      n.icon = ui::label(n.ring, Font::ICON, theme::INK);
      lv_obj_set_style_text_align(n.icon, LV_TEXT_ALIGN_CENTER, 0);
      n.value = ui::label(n.ring, Font::BODY_LARGE, theme::INK);
      lv_obj_set_style_text_align(n.value, LV_TEXT_ALIGN_CENTER, 0);
      n.extra = ui::label(n.ring, Font::BODY, theme::MUTED);
      lv_obj_set_style_text_align(n.extra, LV_TEXT_ALIGN_CENTER, 0);
      n.name = ui::label(c.parent, Font::BODY, theme::MUTED);
      lv_obj_set_style_text_align(n.name, LV_TEXT_ALIGN_CENTER, 0);
    }
    layout_diagram();

    for (Row &r : rows_) {
      r.icon = ui::label(c.parent, Font::ICON_SMALL, theme::MUTED);
      lv_obj_set_style_text_align(r.icon, LV_TEXT_ALIGN_CENTER, 0);
      r.name = ui::label(c.parent, Font::BODY_LARGE, theme::INK_SOFT);
      r.value = ui::label(c.parent, Font::TITLE, theme::INK);
      lv_obj_set_style_text_align(r.value, LV_TEXT_ALIGN_RIGHT, 0);
      r.rule = ui::block(c.parent, theme::TRACK);
    }
    note_ = ui::label(c.parent, Font::BODY_LARGE, theme::MUTED);
    place(note_, 0, (height_ - value_h_) / 2, width_, value_h_);
    on_tick(0);
  }

  void on_tick(uint32_t) override {
    const Reading r = plugin_->read(invert_);
    ui::set_text(note_, r.have_grid ? "" : plugin_->text("choose"));
    paint_diagram(r);
    paint_list(r);
  }

  void on_theme() override { on_tick(0); }

 private:
  struct Node {
    lv_obj_t *ring{}, *icon{}, *value{}, *extra{}, *name{};
    int cx = 0, cy = 0;
  };
  struct Row {
    lv_obj_t *icon{}, *name{}, *value{}, *rule{};
  };
  enum { SUN, GRID, HOME };
  // The lines: the sun down and to the grid, the sun down and to the house, the grid to the house.
  enum { SUN_DOWN_G, SUN_TO_GRID, SUN_DOWN_H, SUN_TO_HOME, GRID_HOME, LINES };
  enum { DOT_SUN_GRID, DOT_SUN_HOME, DOT_GRID_HOME, DOTS };
  static constexpr int MAX_ROWS = 7;

  void layout_diagram() {
    const int r = dia_ / 2;
    const int bottom_cy = height_ - label_h_ - ui::px(6) - r;
    const int top_cy = label_h_ + ui::px(6) + r;
    nodes_[SUN].cx = diagram_w_ / 2, nodes_[SUN].cy = top_cy;
    nodes_[GRID].cx = r, nodes_[GRID].cy = bottom_cy;
    nodes_[HOME].cx = diagram_w_ - r, nodes_[HOME].cy = bottom_cy;
    const int text_block = value_h_ + label_h_;
    const int ih = ui::line_height(Font::ICON);
    for (int i = 0; i < 3; ++i) {
      Node &n = nodes_[i];
      place(n.ring, n.cx - r, n.cy - r, dia_, dia_);
      // Icon, value and day inside the ring, as its children: one part, the three centred around its middle.
      const int start = -(ih + text_block) / 2;
      inside(n.ring, n.icon, dia_ - 2 * ring_w_, ih, start + ih / 2);
      inside(n.ring, n.value, dia_ - 2 * ring_w_, value_h_, start + ih + value_h_ / 2);
      inside(n.ring, n.extra, dia_ - 2 * ring_w_, label_h_, start + ih + value_h_ + label_h_ / 2);
      // The sun's name above it, the others' below.
      const int ny = i == SUN ? n.cy - r - label_h_ - ui::px(4) : n.cy + r + ui::px(4);
      // The name a little wider than its circle, but inside the card at the edges.
      const int name_w = dia_ + ui::px(40);
      place(n.name, std::max(0, std::min(n.cx - name_w / 2, width_ - name_w)), std::max(0, ny), name_w, label_h_);
    }
    // Lines as in Home Assistant's card: two from the bottom of the sun, one bending left to the grid, one right to
    // the house, a little above the middle of the lower circles; the grid's line to the house a little below it.
    const int off = r / 4, lw = line_w_;
    const Node &sun = nodes_[SUN], &grid = nodes_[GRID], &home = nodes_[HOME];
    upper_y_ = grid.cy - r / 4;
    lower_y_ = grid.cy + r / 4;
    const int sun_bottom = sun.cy + r - ring_w_;
    place(lines_[SUN_DOWN_G], sun.cx - off, sun_bottom, lw, upper_y_ - sun_bottom + lw);
    place(lines_[SUN_TO_GRID], grid.cx + r - ring_w_, upper_y_, sun.cx - off - (grid.cx + r - ring_w_) + lw, lw);
    place(lines_[SUN_DOWN_H], sun.cx + off, sun_bottom, lw, upper_y_ - sun_bottom + lw);
    place(lines_[SUN_TO_HOME], sun.cx + off, upper_y_, home.cx - r + ring_w_ - (sun.cx + off), lw);
    place(lines_[GRID_HOME], grid.cx + r - ring_w_, lower_y_, home.cx - grid.cx - 2 * (r - ring_w_), lw);
    const int d = dot_;
    place(dots_[DOT_SUN_GRID], (grid.cx + r + sun.cx - off) / 2 - d / 2, upper_y_ + lw / 2 - d / 2, d, d);
    place(dots_[DOT_SUN_HOME], (sun.cx + off + home.cx - r) / 2 - d / 2, upper_y_ + lw / 2 - d / 2, d, d);
    place(dots_[DOT_GRID_HOME], diagram_w_ / 2 - d / 2, lower_y_ + lw / 2 - d / 2, d, d);
  }

  void paint_node(Node &n, uint32_t icon, theme::Role role, bool active, const std::string &value,
                  const std::string &extra, const char *name) {
    set_border(n.ring, active ? role : theme::TRACK);
    ui::set_fill(n.ring, Tone::NORMAL, theme::CARD);
    ui::set_text(n.icon, ui::icon(icon));
    if (active) set_readable(n.icon, Tone::NORMAL, role);
    else ui::set_color(n.icon, theme::MUTED);
    ui::set_text(n.value, value);
    ui::set_color(n.value, theme::INK);
    ui::set_text(n.extra, extra);
    ui::set_color(n.extra, theme::MUTED);
    ui::set_text(n.name, name);
    ui::set_color(n.name, theme::MUTED);
  }

  void paint_line(int a, int b, int dot, theme::Role role, bool active) {
    for (int i : {a, b})
      if (i >= 0) ui::set_fill(lines_[i], Tone::NORMAL, active ? role : theme::TRACK);
    show(dots_[dot], active);
    ui::set_fill(dots_[dot], Tone::NORMAL, role);
  }

  void paint_diagram(const Reading &r) {
    const bool on = r.have_grid;
    for (Node &n : nodes_) {
      show(n.ring, on);
      if (!on) ui::set_text(n.icon, ""), ui::set_text(n.value, ""), ui::set_text(n.extra, ""), ui::set_text(n.name, "");
    }
    for (lv_obj_t *l : lines_) show(l, on);
    if (!on) {
      for (lv_obj_t *d : dots_) show(d, false);
      return;
    }
    const bool has_pv = !std::isnan(r.pv);
    const float pv = has_pv ? r.pv : 0.0f;
    const bool importing = r.grid > FLOWING, exporting = r.grid < -FLOWING;
    const float sun_home = std::min(pv, r.home);
    paint_node(nodes_[SUN], ICON_SOLAR, SOLAR, pv > FLOWING, has_pv ? plugin_->power_text(pv) : "–",
               std::isnan(r.pv_today) ? std::string() : plugin_->energy_text(r.pv_today), plugin_->text("solar"));
    // The screen's fonts have no arrows: the colour and the name say which way the power goes.
    paint_node(nodes_[GRID], ICON_GRID, exporting ? EXPORT : IMPORT, importing || exporting,
               plugin_->power_text(r.grid),
               std::isnan(r.grid_today) ? std::string() : plugin_->energy_text(r.grid_today),
               plugin_->text(exporting ? "export" : importing ? "import" : "grid"));
    paint_node(nodes_[HOME], ICON_HOME, theme::ACCENT, r.home > FLOWING, plugin_->power_text(r.home),
               std::isnan(r.coverage) ? std::string() : tessera::format(plugin_->text("percent"), percent(r.coverage)),
               plugin_->text("home"));
    paint_line(SUN_DOWN_G, SUN_TO_GRID, DOT_SUN_GRID, EXPORT, has_pv && exporting);
    paint_line(SUN_DOWN_H, SUN_TO_HOME, DOT_SUN_HOME, SOLAR, sun_home > FLOWING);
    paint_line(GRID_HOME, -1, DOT_GRID_HOME, IMPORT, importing);
    // Without a solar sensor, no lines from the sun.
    for (int i : {SUN_DOWN_G, SUN_TO_GRID, SUN_DOWN_H, SUN_TO_HOME}) show(lines_[i], has_pv);
    show(nodes_[SUN].ring, has_pv);
    if (!has_pv) ui::set_text(nodes_[SUN].icon, ""), ui::set_text(nodes_[SUN].value, ""),
        ui::set_text(nodes_[SUN].extra, ""), ui::set_text(nodes_[SUN].name, "");
  }

  // Every sensor in a row: its icon, its name and its value; only those chosen.
  void paint_list(const Reading &r) {
    struct Item {
      uint32_t icon;
      theme::Role role;
      std::string name, value;
    };
    Item items[MAX_ROWS];
    int n = 0;
    if (r.have_grid) {
      const bool exporting = r.grid < -FLOWING;
      items[n++] = {ICON_GRID, exporting ? EXPORT : IMPORT, plugin_->text(exporting ? "export" : "import"),
                    plugin_->power_text(r.grid)};
      if (!std::isnan(r.pv)) items[n++] = {ICON_SOLAR, SOLAR, plugin_->text("row_pv"), plugin_->power_text(r.pv)};
      items[n++] = {ICON_HOME, theme::ACCENT, plugin_->text("row_home"), plugin_->power_text(r.home)};
      if (!std::isnan(r.coverage))
        items[n++] = {ICON_LEAF, theme::MARK_GREEN, plugin_->text("row_coverage"),
                      tessera::format(plugin_->text("percent"), percent(r.coverage))};
      if (!std::isnan(r.pv_today))
        items[n++] = {ICON_PANEL, SOLAR, plugin_->text("row_pv_today"), plugin_->energy_text(r.pv_today)};
      if (!std::isnan(r.forecast)) {
        std::string v = plugin_->energy_text(r.forecast);
        if (!std::isnan(r.pv_today) && r.forecast > 0)
          v += " · " + tessera::format(plugin_->text("percent"), percent(100.0f * r.pv_today / r.forecast));
        items[n++] = {ICON_SUNNY, SOLAR, plugin_->text("row_forecast"), v};
      }
      if (!std::isnan(r.grid_today))
        items[n++] = {ICON_METER, IMPORT, plugin_->text("row_grid_today"), plugin_->energy_text(r.grid_today)};
    }
    // The rows share the height, at most a comfortable row each, in the middle.
    const int ih = ui::line_height(Font::ICON_SMALL), th = ui::line_height(Font::TITLE);
    const int row_h = n ? std::min(height_ / n, std::max(ih, th) + ui::px(ui::large() ? 28 : 14)) : 0;
    const int top = (height_ - n * row_h) / 2;
    if (n != rows_shown_) {
      rows_shown_ = n;
      for (int i = 0; i < MAX_ROWS; ++i) {
        Row &row = rows_[i];
        const int y = top + i * row_h;
        const int value_w = list_w_ * 2 / 5;
        place(row.icon, list_x_, y + (row_h - ih) / 2, ih, ih);
        place(row.name, list_x_ + ih + ui::px(12), y + (row_h - value_h_) / 2, list_w_ - ih - ui::px(12) - value_w,
              value_h_);
        place(row.value, list_x_ + list_w_ - value_w, y + (row_h - th) / 2, value_w, th);
        place(row.rule, list_x_, y + row_h - 1, list_w_, 1);
      }
    }
    for (int i = 0; i < MAX_ROWS; ++i) {
      Row &row = rows_[i];
      const bool on = i < n;
      ui::set_text(row.icon, on ? ui::icon(items[i].icon) : "");
      ui::set_text(row.name, on ? items[i].name : "");
      ui::set_text(row.value, on ? items[i].value : "");
      if (on) {
        set_readable(row.icon, Tone::NORMAL, items[i].role);
        ui::set_color(row.name, theme::INK_SOFT);
        ui::set_color(row.value, theme::INK);
      }
      show(row.rule, on && i < n - 1);
      ui::set_fill(row.rule, Tone::NORMAL, theme::TRACK);
    }
  }

  const EnergyFlow *plugin_;
  int width_ = 0, height_ = 0, diagram_w_ = 0, list_x_ = 0, list_w_ = 0, dia_ = 0, line_w_ = 0, ring_w_ = 0;
  int dot_ = 0, label_h_ = 0, value_h_ = 0, upper_y_ = 0, lower_y_ = 0, rows_shown_ = -1;
  bool invert_ = false;
  Node nodes_[3];
  lv_obj_t *lines_[LINES]{}, *dots_[DOTS]{};
  Row rows_[MAX_ROWS];
  lv_obj_t *note_{};
};

void EnergyFlow::setup() {
  add_tile("flow", [this]() { return new FlowTile(this); });
  add_card("details", [this]() { return new DetailsCard(this); }, true);
}

}  // namespace esphome::energy_flow
