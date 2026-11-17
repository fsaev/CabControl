#ifndef OTA_CORE_H
#define OTA_CORE_H

#include <stddef.h>
#include <stdio.h>
#include <stdint.h>

// Transport-independent OTA update session. The image is streamed to
// <dir>/UPDATE.TMP, verified, and renamed to <dir>/UPDATE.BIN so the Arduino
// SFU second-stage bootloader installs it on the next boot.
//
// Wire format (all integers little-endian):
//   info    (read, 20 B): [proto][major][minor][patch][nonce 16]
//   control (write):      BEGIN  [0x01][fragment offset u8][body fragment]
//                         COMMIT [0x02]
//                         ABORT  [0x03]
//   data    (write):      [offset u32][payload]
//   status  (read, 10 B): [state][error][received u32][total u32]
// BEGIN body (71 B): [size u32][major][minor][patch][sha256(image) 32][tag 32]
// It may be split over several writes so it fits any ATT MTU; it is processed
// once all 71 bytes have arrived in order.
// tag = HMAC-SHA256(secret, AUTH_LABEL || nonce || body[0..38]).
// The nonce changes on every connection and after every BEGIN attempt.
class OtaCore {
public:
  static constexpr uint8_t PROTOCOL_VERSION = 1;
  static constexpr size_t NONCE_SIZE = 16;
  static constexpr size_t INFO_SIZE = 4 + NONCE_SIZE;
  static constexpr size_t STATUS_SIZE = 10;
  static constexpr size_t BEGIN_HEADER_SIZE = 2;
  static constexpr size_t BEGIN_BODY_SIZE = 71;
  static constexpr size_t BEGIN_SIGNED_SIZE = 39;
  static constexpr size_t CONTROL_MAX_SIZE = BEGIN_HEADER_SIZE + BEGIN_BODY_SIZE;
  static constexpr size_t DATA_HEADER_SIZE = 4;
  static constexpr size_t MARKER_MAGIC_SIZE = 8;
  // The SFU bootloader occupies the first 64 KB of the image.
  static constexpr uint32_t SFU_SIZE = 0x10000;
  static constexpr uint32_t MIN_IMAGE_SIZE = SFU_SIZE + 0x200;
  // The SFU FAT partition is 1 MB; leave room for the filesystem metadata.
  static constexpr uint32_t MAX_IMAGE_SIZE = 0xF0000;
  static constexpr const char *AUTH_LABEL = "CabControl-OTA-v1";

  enum class Command : uint8_t { BEGIN = 0x01, COMMIT = 0x02, ABORT = 0x03 };
  enum class State : uint8_t {
    IDLE = 0,
    RECEIVING = 1,
    VERIFYING = 2,
    READY = 3,
    ERROR = 4
  };
  enum class Error : uint8_t {
    NONE = 0,
    BAD_COMMAND = 1,
    AUTH = 2,
    VERSION = 3,
    SIZE = 4,
    STORAGE = 5,
    SEQUENCE = 6,
    HASH = 7,
    NO_SFU = 8,
    MARKER = 9,
    STATE = 10,
    TIMEOUT = 11,
    ABORTED = 12
  };

  struct Version {
    uint8_t major;
    uint8_t minor;
    uint8_t patch;
  };

  using RandomFn = void (*)(uint8_t *out, size_t len);

  // marker_magic points at MARKER_MAGIC_SIZE bytes that must be followed by
  // [major][minor][patch] somewhere after SFU_SIZE in a valid image.
  OtaCore(const uint8_t *secret, size_t secret_len, Version current,
          const uint8_t *marker_magic, const char *dir, RandomFn random);

  // Drops any session in progress and issues a new nonce.
  void reset();
  void handle_control(const uint8_t *data, size_t len);
  void handle_data(const uint8_t *data, size_t len);
  void fail(Error error);

  void get_info(uint8_t out[INFO_SIZE]) const;
  void get_status(uint8_t out[STATUS_SIZE]) const;

  State state() const { return m_state; }
  Error error() const { return m_error; }
  uint32_t received() const { return m_received; }
  uint32_t total() const { return m_total; }
  uint8_t progress_percent() const;

  static const char *error_name(Error error);

private:
  void begin_fragment(const uint8_t *data, size_t len);
  void begin(const uint8_t *body);
  void commit();
  void close_file();
  void remove_files();
  void new_nonce();
  bool verify_image(Error &error);

  const uint8_t *m_secret;
  size_t m_secret_len;
  Version m_current;
  const uint8_t *m_marker_magic;
  RandomFn m_random;
  char m_tmp_path[48];
  char m_bin_path[48];

  uint8_t m_nonce[NONCE_SIZE] = {};
  uint8_t m_begin_body[BEGIN_BODY_SIZE] = {};
  size_t m_begin_len = 0;
  State m_state = State::IDLE;
  Error m_error = Error::NONE;
  uint32_t m_total = 0;
  uint32_t m_received = 0;
  Version m_image_version = {0, 0, 0};
  uint8_t m_image_hash[32] = {};
  FILE *m_file = nullptr;
};

#endif
