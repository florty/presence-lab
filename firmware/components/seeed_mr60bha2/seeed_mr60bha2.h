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

 protected:
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
