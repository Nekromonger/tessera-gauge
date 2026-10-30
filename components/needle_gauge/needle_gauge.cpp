#include "needle_gauge.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace esphome::needle_gauge {

using tessera::Font;
using tessera::Tone;
namespace ui = tessera::ui;

static constexpr float PI = 3.14159265f;
// The arc's resolution: the value arc moves in thousandths of the range.
static constexpr int STEPS = 1000;

// An option typed as text ("-2000", "21,5"), or a number from an older version of the manifest.
static float read_number(JsonVariantConst value, float fallback) {
  if (value.is<float>()) return value.as<float>();
  std::string text = value | "";
  for (char &ch : text)
    if (ch == ',') ch = '.';
  char *end = nullptr;
  const float v = std::strtof(text.c_str(), &end);
  return end != text.c_str() && std::isfinite(v) ? v : fallback;
}

// The tile: what it measures on top, a half-round dial with a needle in the middle, the value under it.
// create() lays everything out once; on_state() keeps the value; on_tick() paints it, and the ui::set_* calls change a
// part only when it differs.
class GaugeTile : public tessera::Tile {
 public:
  explicit GaugeTile(const tessera::Plugin *plugin) : plugin_(plugin) {}

  void create(const tessera::TileContext &c) override {
    width_ = c.width;
    height_ = c.height;
    min_ = read_number(c.options["min"], 0.0f);
    max_ = read_number(c.options["max"], 100.0f);
    if (max_ <= min_) max_ = min_ + 1;
    const char *decimals = c.options["decimals"] | "auto";
    decimals_ = std::strcmp(decimals, "auto") == 0 ? -1 : std::atoi(decimals);
    const char *colors = c.options["colors"] | "accent";
    colors_ = std::strcmp(colors, "high_bad") == 0 ? 1 : std::strcmp(colors, "low_bad") == 0 ? 2 : 0;
    title_text_ = c.options["title"] | "";
    if (title_text_.empty()) title_text_ = c.name;

    // Heights from the top: the title, the dial, the value. The fonts step down until the three fit.
    title_font_ = ui::large() ? Font::TITLE : Font::BODY;
    const int title_h = ui::line_height(title_font_), gap = ui::px(4);
    value_font_ = Font::VALUE;
    for (Font f : {Font::VALUE, Font::HEADLINE, Font::TITLE}) {
      value_font_ = f;
      if (ui::line_height(f) * 3 <= height_) break;
    }
    const int value_h = ui::line_height(value_font_);
    const int room = height_ - title_h - value_h - 2 * gap;

    // The dial: a half circle as wide as the tile allows, its centre on its lowest line.
    arc_w_ = std::max(ui::px(4), std::min(width_, room * 2) / 14);
    hub_ = std::max(ui::px(6), arc_w_ + ui::px(2));
    radius_ = std::min(width_ / 2 - ui::px(2), room - hub_ / 2);
    has_dial_ = radius_ >= ui::px(18);
    const int dial_h = has_dial_ ? radius_ + hub_ / 2 : 0;
    const int top = std::max(0, (height_ - title_h - dial_h - value_h - (has_dial_ ? 2 * gap : gap)) / 2);

    title_ = ui::label(c.parent, title_font_, theme::INK_SOFT);
    lv_obj_set_width(title_, width_);
    lv_obj_set_style_text_align(title_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(title_, 0, top);

    cx_ = width_ / 2;
    cy_ = top + title_h + gap + radius_;
    if (has_dial_) {
#if LV_USE_ARC
      arc_ = lv_arc_create(c.parent);
      lv_obj_remove_style_all(arc_);
      lv_obj_remove_flag(arc_, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_set_size(arc_, radius_ * 2, radius_ * 2);
      lv_obj_set_pos(arc_, cx_ - radius_, cy_ - radius_);
      lv_arc_set_rotation(arc_, 180);
      lv_arc_set_bg_angles(arc_, 0, 180);
      lv_arc_set_range(arc_, 0, STEPS);
      lv_arc_set_value(arc_, 0);
      lv_obj_set_style_arc_width(arc_, arc_w_, LV_PART_MAIN);
      lv_obj_set_style_arc_width(arc_, arc_w_, LV_PART_INDICATOR);
      lv_obj_set_style_arc_rounded(arc_, true, LV_PART_MAIN);
      lv_obj_set_style_arc_rounded(arc_, true, LV_PART_INDICATOR);
#endif
      needle_ = lv_line_create(c.parent);
      lv_obj_remove_flag(needle_, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_set_pos(needle_, 0, 0);
      lv_obj_set_style_line_width(needle_, std::max(2, arc_w_ / 2), 0);
      lv_obj_set_style_line_rounded(needle_, true, 0);
      points_[0].x = points_[1].x = cx_;
      points_[0].y = points_[1].y = cy_;
      lv_line_set_points(needle_, points_, 2);
      hub_dot_ = ui::block(c.parent, theme::INK);
      lv_obj_set_size(hub_dot_, hub_, hub_);
      lv_obj_set_style_radius(hub_dot_, LV_RADIUS_CIRCLE, 0);
      lv_obj_set_pos(hub_dot_, cx_ - hub_ / 2, cy_ - hub_ / 2);
    }

    value_top_ = (has_dial_ ? cy_ + hub_ / 2 + gap : top + title_h + gap);
    value_ = ui::label(c.parent, value_font_, theme::INK);
    unit_ = ui::label(c.parent, Font::BODY, theme::MUTED);
    on_theme();
  }

  void on_state(JsonObjectConst data) override {
    const char *state = data["state"] | "";
    name_ = data["name"] | "";
    unit_text_ = data["attributes"]["unit_of_measurement"] | "";
    char *end = nullptr;
    value_ok_ = *state && std::strcmp(state, "unknown") != 0 && std::strcmp(state, "unavailable") != 0;
    if (value_ok_) {
      value_num_ = std::strtof(state, &end);
      value_ok_ = end != state && std::isfinite(value_num_);
    }
    chosen_ = data["state"].is<const char *>();
  }

  void on_tick(uint32_t) override {
    ui::set_text(title_, roomy::plain(title_text_.empty() ? name_ : title_text_));
    std::string number, unit;
    if (!chosen_) {
      number = plugin_->text("choose");
    } else if (!value_ok_) {
      number = "-";
    } else {
      number = format(value_num_);
      unit = roomy::plain(unit_text_);
    }
    // The value as large as it fits beside its unit.
    const int unit_w = unit.empty() ? 0 : ui::text_width(unit, Font::BODY) + ui::px(3);
    Font face = value_font_;
    for (Font f : {value_font_, Font::HEADLINE, Font::TITLE, Font::BODY}) {
      if (ui::line_height(f) > ui::line_height(value_font_)) continue;
      face = f;
      if (ui::text_width(number, f) + unit_w <= width_) break;
    }
    ui::set_font(value_, face);
    ui::set_text(value_, number);
    ui::set_text(unit_, unit);
    const int nw = std::min(width_ - unit_w, ui::text_width(number, face)), vh = ui::line_height(face);
    const int left = (width_ - nw - unit_w) / 2;
    lv_obj_set_width(value_, std::max(1, nw));
    lv_obj_set_pos(value_, left, value_top_);
    // The unit on the value's baseline, as a sensor's tile writes it.
    lv_obj_set_width(unit_, std::max(1, unit_w));
    lv_obj_set_pos(unit_, left + nw + ui::px(3), value_top_ + vh - ui::line_height(Font::BODY) - ui::px(2));

    if (!has_dial_) return;
    const float share = value_ok_ ? std::clamp((value_num_ - min_) / (max_ - min_), 0.0f, 1.0f) : 0.0f;
    const int step = (int) std::lround(share * STEPS);
    if (step != painted_step_ || tone_dirty_) {
      painted_step_ = step;
      tone_dirty_ = false;
#if LV_USE_ARC
      lv_arc_set_value(arc_, value_ok_ ? step : 0);
      lv_obj_set_style_arc_color(arc_, ui::tone_color(tone(share), theme::ACCENT), LV_PART_INDICATOR);
#endif
      // The needle from the hub to just inside the arc: 180 degrees is the minimum, 360 the maximum.
      const float angle = (180.0f + share * 180.0f) * PI / 180.0f;
      const int length = radius_ - arc_w_ - ui::px(3);
      points_[0].x = cx_;
      points_[0].y = cy_;
      points_[1].x = cx_ + (int) std::lround(length * std::cos(angle));
      points_[1].y = cy_ + (int) std::lround(length * std::sin(angle));
      lv_line_set_points(needle_, points_, 2);
      if (value_ok_) lv_obj_remove_flag(needle_, LV_OBJ_FLAG_HIDDEN);
      else lv_obj_add_flag(needle_, LV_OBJ_FLAG_HIDDEN);
    }
  }

  // Light and dark: the theme roles have other values now.
  void on_theme() override {
    ui::set_color(title_, theme::INK_SOFT);
    ui::set_color(value_, theme::INK);
    ui::set_color(unit_, theme::MUTED);
    if (!has_dial_) return;
#if LV_USE_ARC
    lv_obj_set_style_arc_color(arc_, ui::color(theme::TRACK), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc_, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc_, LV_OPA_COVER, LV_PART_INDICATOR);
#endif
    lv_obj_set_style_line_color(needle_, ui::color(theme::INK), 0);
    ui::set_fill(hub_dot_, Tone::NORMAL, theme::INK);
    tone_dirty_ = true;
  }

 private:
  // The colour of the filled arc: the accent, or green, amber and red by where the value stands.
  Tone tone(float share) const {
    if (colors_ == 1) return share >= 0.85f ? Tone::ALERT : share >= 0.7f ? Tone::BUSY : Tone::GOOD;
    if (colors_ == 2) return share <= 0.15f ? Tone::ALERT : share <= 0.3f ? Tone::BUSY : Tone::GOOD;
    return Tone::ACCENT;
  }

  // The number with the decimals chosen ("auto": none from 100 up, one below), in the screen's decimal sign.
  std::string format(float v) const {
    int places = decimals_;
    if (places < 0) places = std::fabs(v) >= 100 ? 0 : (std::fabs(v - std::round(v)) < 0.05f ? 0 : 1);
    char buffer[24];
    snprintf(buffer, sizeof(buffer), "%.*f", places, v);
    std::string out = buffer;
    const std::string sign = plugin_->text("decimal_sign");
    const size_t dot = out.find('.');
    if (dot != std::string::npos && !sign.empty()) out.replace(dot, 1, sign);
    return out;
  }

  const tessera::Plugin *plugin_;
  int width_ = 0, height_ = 0, cx_ = 0, cy_ = 0, radius_ = 0, arc_w_ = 0, hub_ = 0, value_top_ = 0;
  int decimals_ = -1, colors_ = 0, painted_step_ = -1;
  bool has_dial_ = false, value_ok_ = false, chosen_ = false, tone_dirty_ = true;
  float min_ = 0, max_ = 100, value_num_ = 0;
  Font title_font_ = Font::TITLE, value_font_ = Font::VALUE;
  tessera::Text title_text_, name_, unit_text_;
  lv_point_precise_t points_[2]{};
  lv_obj_t *title_{}, *value_{}, *unit_{}, *arc_{}, *needle_{}, *hub_dot_{};
};

void NeedleGauge::setup() {
  add_tile("gauge", [this]() { return new GaugeTile(this); });
}

}  // namespace esphome::needle_gauge
