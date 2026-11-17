#ifndef IDENTITY_CORE_H
#define IDENTITY_CORE_H

#include <stddef.h>
#include <stdint.h>

// Identification name, stored in <dir>/NAME.TXT. Changing it over BLE needs
// an HMAC-SHA256 tag made with the shared OTA secret.
//
// Wire format:
//   auth (read/notify, 18 B): [proto][result][nonce 16]
//   set  (write):             [fragment offset u8][body fragment]
// Set body (48 B): [len][name, zero-padded to 15 B][tag 32]
// It may be split over several writes so it fits any ATT MTU; it is processed
// once all 48 bytes have arrived in order.
// tag = HMAC-SHA256(secret, AUTH_LABEL || nonce || body[0..16]).
// The nonce changes on every connection and after every attempt.
class IdentityCore {
public:
  static constexpr uint8_t PROTOCOL_VERSION = 1;
  static constexpr size_t NAME_MAX_LEN = 15;
  static constexpr size_t NONCE_SIZE = 16;
  static constexpr size_t AUTH_SIZE = 2 + NONCE_SIZE;
  static constexpr size_t SET_HEADER_SIZE = 1;
  static constexpr size_t SET_SIGNED_SIZE = 1 + NAME_MAX_LEN;
  static constexpr size_t SET_BODY_SIZE = SET_SIGNED_SIZE + 32;
  static constexpr size_t SET_MAX_SIZE = SET_HEADER_SIZE + SET_BODY_SIZE;
  static constexpr const char *AUTH_LABEL = "CabControl-NAME-v1";
  static constexpr const char *DEFAULT_NAME = "CabControl";

  enum class Result : uint8_t {
    NONE = 0,
    OK = 1,
    AUTH = 2,
    INVALID = 3,
    STORAGE = 4,
    BAD_COMMAND = 5
  };

  using RandomFn = void (*)(uint8_t *out, size_t len);

  IdentityCore(const uint8_t *secret, size_t secret_len, const char *dir,
               RandomFn random);

  // Reads the stored name; falls back to DEFAULT_NAME.
  void load();
  // Drops a partial set request, clears the result and issues a new nonce.
  void reset();
  // Returns true when the stored name changed.
  bool handle_set(const uint8_t *data, size_t len);
  void get_auth(uint8_t out[AUTH_SIZE]) const;

  const char *name() const { return m_name; }
  Result result() const { return m_result; }

  // 1..NAME_MAX_LEN printable ASCII characters, no leading/trailing space.
  static bool is_valid_name(const char *name, size_t len);

private:
  bool apply(const uint8_t *body);
  bool read_name(const char *path, char out[NAME_MAX_LEN + 1]) const;
  bool save(const char *name);
  void new_nonce();

  const uint8_t *m_secret;
  size_t m_secret_len;
  RandomFn m_random;
  char m_txt_path[48];
  char m_tmp_path[48];

  char m_name[NAME_MAX_LEN + 1] = {};
  uint8_t m_nonce[NONCE_SIZE] = {};
  uint8_t m_body[SET_BODY_SIZE] = {};
  size_t m_body_len = 0;
  Result m_result = Result::NONE;
};

#endif
