#pragma once
#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#ifdef USE_BINARY_SENSOR
#include "esphome/components/binary_sensor/binary_sensor.h"
#endif
#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif
#include "esphome/components/uart/uart.h"
#include "esphome/core/automation.h"
#include "esphome/core/helpers.h"

#include <cstdio>
#include <cstring>
#include <map>
#include <string>

namespace esphome::seeed_mr60bha2 {
static const uint8_t FRAME_HEADER_BUFFER = 0x01;
static const uint16_t BREATH_RATE_TYPE_BUFFER = 0x0A14;
static const uint16_t PEOPLE_EXIST_TYPE_BUFFER = 0x0F09;
static const uint16_t HEART_RATE_TYPE_BUFFER = 0x0A15;
static const uint16_t DISTANCE_TYPE_BUFFER = 0x0A16;
static const uint16_t PRINT_CLOUD_BUFFER = 0x0A04;
// Lab addition: chest-movement phases (total, breathing, heart), three little-endian floats.
static const uint16_t HEART_BREATH_PHASE_BUFFER = 0x0A13;

class MR60BHA2Component final : public Component,
                                public uart::UARTDevice {  // The class name must be the name defined by text_sensor.py
#ifdef USE_BINARY_SENSOR
  SUB_BINARY_SENSOR(has_target);
#endif
#ifdef USE_SENSOR
  SUB_SENSOR(breath_rate);
  SUB_SENSOR(heart_rate);
  SUB_SENSOR(distance);
  SUB_SENSOR(num_targets);
#endif

 public:
  float get_setup_priority() const override { return esphome::setup_priority::LATE; }
  void dump_config() override;
  void loop() override;

  // Local addition (presence-office): point-cloud movement. A target with a non-zero Doppler index means
  // something in the field is moving (a seated person's micro-movement/breathing); static reflectors read 0.
  uint32_t get_last_motion_ms() const { return this->last_motion_ms_; }
  // Number of point-cloud frames with movement since the last call (resets the counter).
  uint32_t take_motion_frames() {
    uint32_t n = this->motion_frames_;
    this->motion_frames_ = 0;
    return n;
  }

  // Lab addition: latest point-cloud frame, as JSON [seq,[[x,y,doppler,cluster],...]]. Empty if no new frame.
  std::string take_points_json() {
    if (this->points_seq_ == this->points_sent_seq_)
      return "";
    this->points_sent_seq_ = this->points_seq_;
    std::string out = "[" + std::to_string(this->points_seq_) + ",[";
    char buf[64];
    for (uint8_t i = 0; i < this->point_count_; i++) {
      const CloudPoint &p = this->points_[i];
      snprintf(buf, sizeof(buf), "%s[%.3f,%.3f,%d,%d]", i ? "," : "", p.x, p.y, (int) p.doppler, (int) p.cluster);
      out += buf;
    }
    return out + "]]";
  }

  // Lab addition: the newest phase samples as JSON [[seq,ms,total,breath,heart],...], or "" if nothing new.
  // Each call repeats the last PHASE_REPEAT samples: the native API keeps only the newest state per entity
  // when it is busy, so a single dropped update would otherwise lose samples. seq counts every sample since
  // boot, so the reader drops duplicates and can spot real gaps; ms is millis() when the sample arrived.
  std::string take_phases_json() {
    if (this->phase_head_ == this->phase_sent_)
      return "";
    this->phase_sent_ = this->phase_head_;
    uint32_t from = this->phase_head_ > PHASE_REPEAT ? this->phase_head_ - PHASE_REPEAT : 0;
    std::string out = "[";
    char buf[96];
    for (uint32_t i = from; i < this->phase_head_; i++) {
      const PhaseSample &s = this->phases_[i % MAX_PHASES];
      snprintf(buf, sizeof(buf), "%s[%u,%u,%.4f,%.4f,%.4f]", i == from ? "" : ",", (unsigned) i, (unsigned) s.ms,
               s.total, s.breath, s.heart);
      out += buf;
    }
    return out + "]";
  }

 protected:
  struct PhaseSample {
    uint32_t ms;
    float total, breath, heart;
  };
  static const uint32_t MAX_PHASES = 16;
  static const uint32_t PHASE_REPEAT = 12;  // about 1.2 s at the radar's ~10 samples/s
  PhaseSample phases_[MAX_PHASES];
  uint32_t phase_head_{0};  // next sample to write
  uint32_t phase_sent_{0};  // phase_head_ at the last take_phases_json()

  struct CloudPoint {
    float x, y;
    int32_t doppler, cluster;
  };
  static const uint8_t MAX_POINTS = 8;
  CloudPoint points_[MAX_POINTS];
  uint8_t point_count_{0};
  uint32_t points_seq_{0};
  uint32_t points_sent_seq_{0};

  uint32_t last_motion_ms_{0};
  uint32_t motion_frames_{0};

  bool validate_message_();
  void process_frame_(uint16_t frame_id, uint16_t frame_type, const uint8_t *data, size_t length);

  std::vector<uint8_t> rx_message_;
};

}  // namespace esphome::seeed_mr60bha2
