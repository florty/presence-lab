#include "seeed_mr60bha2.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#include <cinttypes>
#include <utility>

namespace esphome::seeed_mr60bha2 {

static const char *const TAG = "seeed_mr60bha2";

// Maximum bytes to log in verbose hex output
static constexpr size_t MR60BHA2_MAX_LOG_BYTES = 64;

// Prints the component's configuration data. dump_config() prints all of the component's configuration
// items in an easy-to-read format, including the configuration key-value pairs.
void MR60BHA2Component::dump_config() {
  ESP_LOGCONFIG(TAG, "MR60BHA2:");
#ifdef USE_BINARY_SENSOR
  LOG_BINARY_SENSOR(" ", "People Exist Binary Sensor", this->has_target_binary_sensor_);
#endif
#ifdef USE_SENSOR
  LOG_SENSOR(" ", "Breath Rate Sensor", this->breath_rate_sensor_);
  LOG_SENSOR(" ", "Heart Rate Sensor", this->heart_rate_sensor_);
  LOG_SENSOR(" ", "Distance Sensor", this->distance_sensor_);
  LOG_SENSOR(" ", "Target Number Sensor", this->num_targets_sensor_);
#endif
}

// main loop
void MR60BHA2Component::loop() {
  // Read all available bytes in batches to reduce UART call overhead.
  size_t avail = this->available();
  uint8_t buf[64];
  while (avail > 0) {
    size_t to_read = std::min(avail, sizeof(buf));
    if (!this->read_array(buf, to_read)) {
      break;
    }
    avail -= to_read;

    for (size_t i = 0; i < to_read; i++) {
      this->rx_message_.push_back(buf[i]);
      if (!this->validate_message_()) {
        this->rx_message_.clear();
      }
    }
  }
}

/**
 * @brief Calculate the checksum for a byte array.
 *
 * This function calculates the checksum for the provided byte array using an
 * XOR-based checksum algorithm.
 *
 * @param data The byte array to calculate the checksum for.
 * @param len The length of the byte array.
 * @return The calculated checksum.
 */
static uint8_t calculate_checksum(const uint8_t *data, size_t len) {
  uint8_t checksum = 0;
  for (size_t i = 0; i < len; i++) {
    checksum ^= data[i];
  }
  checksum = ~checksum;
  return checksum;
}

/**
 * @brief Validate the checksum of a byte array.
 *
 * This function validates the checksum of the provided byte array by comparing
 * it to the expected checksum.
 *
 * @param data The byte array to validate.
 * @param len The length of the byte array.
 * @param expected_checksum The expected checksum.
 * @return True if the checksum is valid, false otherwise.
 */
static bool validate_checksum(const uint8_t *data, size_t len, uint8_t expected_checksum) {
  return calculate_checksum(data, len) == expected_checksum;
}

bool MR60BHA2Component::validate_message_() {
  size_t at = this->rx_message_.size() - 1;
  auto *data = &this->rx_message_[0];
  uint8_t new_byte = data[at];

  if (at == 0) {
    return new_byte == FRAME_HEADER_BUFFER;
  }

  if (at <= 2) {
    return true;
  }
  uint16_t frame_id = encode_uint16(data[1], data[2]);

  if (at <= 4) {
    return true;
  }

  uint16_t length = encode_uint16(data[3], data[4]);

  if (at <= 6) {
    return true;
  }

  uint16_t frame_type = encode_uint16(data[5], data[6]);

  if (frame_type != BREATH_RATE_TYPE_BUFFER && frame_type != HEART_RATE_TYPE_BUFFER &&
      frame_type != DISTANCE_TYPE_BUFFER && frame_type != PEOPLE_EXIST_TYPE_BUFFER &&
      frame_type != PRINT_CLOUD_BUFFER && frame_type != HEART_BREATH_PHASE_BUFFER) {
    return false;
  }

  uint8_t header_checksum = new_byte;

  if (at == 7) {
    if (!validate_checksum(data, 7, header_checksum)) {
      ESP_LOGE(TAG, "HEAD_CKSUM_FRAME ERROR: 0x%02x", header_checksum);
#if ESPHOME_LOG_LEVEL >= ESPHOME_LOG_LEVEL_VERBOSE
      char hex_buf[format_hex_pretty_size(MR60BHA2_MAX_LOG_BYTES)];
#endif
      ESP_LOGV(TAG, "GET FRAME: %s", format_hex_pretty_to(hex_buf, sizeof(hex_buf), data, 8));
      return false;
    }
    return true;
  }

  // Wait until all data is read
  if (at - 8 < length) {
    return true;
  }

  uint8_t data_checksum = new_byte;
  if (at == 8 + length) {
    if (!validate_checksum(data + 8, length, data_checksum)) {
      ESP_LOGE(TAG, "DATA_CKSUM_FRAME ERROR: 0x%02x", data_checksum);
#if ESPHOME_LOG_LEVEL >= ESPHOME_LOG_LEVEL_VERBOSE
      char hex_buf[format_hex_pretty_size(MR60BHA2_MAX_LOG_BYTES)];
#endif
      ESP_LOGV(TAG, "GET FRAME: %s", format_hex_pretty_to(hex_buf, sizeof(hex_buf), data, 8 + length));
      return false;
    }
  }

  const uint8_t *frame_data = data + 8;
#if ESPHOME_LOG_LEVEL >= ESPHOME_LOG_LEVEL_VERBOSE
  char hex_buf1[format_hex_pretty_size(MR60BHA2_MAX_LOG_BYTES)];
  char hex_buf2[format_hex_pretty_size(MR60BHA2_MAX_LOG_BYTES)];
#endif
  ESP_LOGV(TAG, "Received Frame: ID: 0x%04x, Type: 0x%04x, Data: [%s] Raw Data: [%s]", frame_id, frame_type,
           format_hex_pretty_to(hex_buf1, sizeof(hex_buf1), frame_data, length),
           format_hex_pretty_to(hex_buf2, sizeof(hex_buf2), this->rx_message_.data(), this->rx_message_.size()));
  this->process_frame_(frame_id, frame_type, data + 8, length);

  // Return false to reset rx buffer
  return false;
}

void MR60BHA2Component::process_frame_(uint16_t frame_id, uint16_t frame_type, const uint8_t *data, size_t length) {
  switch (frame_type) {
    case BREATH_RATE_TYPE_BUFFER:
      if (this->breath_rate_sensor_ != nullptr && length >= 4) {
        uint32_t current_breath_rate_int = encode_uint32(data[3], data[2], data[1], data[0]);
        if (current_breath_rate_int != 0) {
          float breath_rate_float;
          memcpy(&breath_rate_float, &current_breath_rate_int, sizeof(float));
          this->breath_rate_sensor_->publish_state(breath_rate_float);
        }
      }
      break;
    case PEOPLE_EXIST_TYPE_BUFFER:
      if (this->has_target_binary_sensor_ != nullptr && length >= 2) {
        uint16_t has_target_int = encode_uint16(data[1], data[0]);
        this->has_target_binary_sensor_->publish_state(has_target_int);
        if (has_target_int == 0) {
          if (this->breath_rate_sensor_ != nullptr)
            this->breath_rate_sensor_->publish_state(0.0);
          if (this->heart_rate_sensor_ != nullptr)
            this->heart_rate_sensor_->publish_state(0.0);
          if (this->distance_sensor_ != nullptr)
            this->distance_sensor_->publish_state(0.0);
          if (this->num_targets_sensor_ != nullptr)
            this->num_targets_sensor_->publish_state(0);
        }
      }
      break;
    case HEART_RATE_TYPE_BUFFER:
      if (this->heart_rate_sensor_ != nullptr && length >= 4) {
        uint32_t current_heart_rate_int = encode_uint32(data[3], data[2], data[1], data[0]);
        if (current_heart_rate_int != 0) {
          float heart_rate_float;
          memcpy(&heart_rate_float, &current_heart_rate_int, sizeof(float));
          this->heart_rate_sensor_->publish_state(heart_rate_float);
        }
      }
      break;
    case DISTANCE_TYPE_BUFFER:
      if (length >= 1 && data[0] != 0) {
        if (this->distance_sensor_ != nullptr && length >= 8) {
          uint32_t current_distance_int = encode_uint32(data[7], data[6], data[5], data[4]);
          float distance_float;
          memcpy(&distance_float, &current_distance_int, sizeof(float));
          this->distance_sensor_->publish_state(distance_float);
        }
      }
      break;
    case PRINT_CLOUD_BUFFER:
      if (this->num_targets_sensor_ != nullptr && length >= 4) {
        uint32_t current_num_targets_int = encode_uint32(data[3], data[2], data[1], data[0]);
        this->num_targets_sensor_->publish_state(current_num_targets_int);
      }
      // Local addition: each target = x (float), y (float), Doppler index (int32), cluster (int32).
      if (length >= 4) {
        uint32_t targets = encode_uint32(data[3], data[2], data[1], data[0]);
        bool moving = false;
        // Lab addition: keep each target's position for the point-cloud stream.
        this->point_count_ = 0;
        for (uint32_t i = 0; i < targets && 4 + 16 * (i + 1) <= length; i++) {
          const uint8_t *t = data + 4 + 16 * i;
          int32_t dop = (int32_t) encode_uint32(t[11], t[10], t[9], t[8]);
          if (dop != 0)
            moving = true;
          if (this->point_count_ < MAX_POINTS) {
            CloudPoint &p = this->points_[this->point_count_++];
            memcpy(&p.x, t, sizeof(float));
            memcpy(&p.y, t + 4, sizeof(float));
            p.doppler = dop;
            p.cluster = (int32_t) encode_uint32(t[15], t[14], t[13], t[12]);
          }
        }
        this->points_seq_++;
        if (moving) {
          this->last_motion_ms_ = millis();
          this->motion_frames_++;
        }
      }
      break;
    case HEART_BREATH_PHASE_BUFFER:
      // Lab addition: keep the newest waveform samples for take_phases_json().
      if (length >= 12) {
        PhaseSample &s = this->phases_[this->phase_head_ % MAX_PHASES];
        s.ms = millis();
        memcpy(&s.total, data, sizeof(float));
        memcpy(&s.breath, data + 4, sizeof(float));
        memcpy(&s.heart, data + 8, sizeof(float));
        this->phase_head_++;
      }
      break;
    default:
      break;
  }
}

}  // namespace esphome::seeed_mr60bha2
