#pragma once
#include <Arduino.h>
#include <SPI.h>
#include <PN5180ISO15693.h>
#include <string>
#include "esphome/core/log.h"

static const char *TAG_PN = "pn5180";

#ifndef ISO15693_EC_CMD_NOT_AVAILABLE
  #define ISO15693_EC_CMD_NOT_AVAILABLE ISO15693_EC_BLOCK_NOT_AVAILABLE
#endif

namespace pn5180glue {
  static PN5180ISO15693* nfc = nullptr;

  // Passwörter
  static uint8_t pwd_nxp[4]    = {0x0F, 0x0F, 0x0F, 0x0F};
  static uint8_t pwd_fake[4]   = {0x00, 0x00, 0x00, 0x00};

  static uint8_t last_uid_raw[8] = {0};
  static bool    have_uid = false;

  // Systeminfo
  static uint8_t g_block_size = 4;  // Bytes/Block
  static uint8_t g_num_blocks = 0;  // Blöcke

  // Letzter erfolgreicher Unlock-Mode (für HA-Sensor)
  static const char* g_last_unlock = "none";

  template<class T>
  inline auto unlock_slix2(T* t, uint8_t* pwd)
    -> decltype(t->unlockICODESLIX2(pwd), ISO15693_EC_OK) { return t->unlockICODESLIX2(pwd); }
  inline ISO15693ErrorCode unlock_slix2(...) { return ISO15693_EC_CMD_NOT_AVAILABLE; }

  template<class T>
  inline auto disable_privacy(T* t, uint8_t* pwd)
    -> decltype(t->disablePrivacyMode(pwd), ISO15693_EC_OK) { return t->disablePrivacyMode(pwd); }
  inline ISO15693ErrorCode disable_privacy(...) { return ISO15693_EC_CMD_NOT_AVAILABLE; }

  inline void begin(int nss, int busy, int rst) {
    ESP_LOGI(TAG_PN, "SPI.begin() + PN5180 init");
    SPI.begin();
    nfc = new PN5180ISO15693(nss, busy, rst);
    nfc->begin();
    nfc->reset();
    nfc->setupRF();
  }

  inline const char* last_unlock_method() { return g_last_unlock; }

  // kompletter Privacy-Run mit Logging + Setzen von g_last_unlock
  inline bool unlock_privacy_round() {
    if (!nfc) return false;
    struct PW { uint8_t* p; const char* name_slix2; const char* name_dis; } pw[] = {
      { pwd_nxp,    "slix2_nxp",    "disable_nxp"    },
      { pwd_fake,   "slix2_fake",   "disable_fake"   },
    };

    for (auto &e : pw) {
      nfc->reset(); nfc->setupRF(); delay(30);
      ISO15693ErrorCode rc = unlock_slix2(nfc, e.p);
      ESP_LOGI(TAG_PN, "unlockICODESLIX2 %s rc=%d", e.name_slix2, (int)rc);
      if (rc == ISO15693_EC_OK) { g_last_unlock = e.name_slix2; return true; }
    }
    for (auto &e : pw) {
      nfc->reset(); nfc->setupRF(); delay(30);
      ISO15693ErrorCode rc = disable_privacy(nfc, e.p);
      ESP_LOGI(TAG_PN, "disablePrivacyMode %s rc=%d", e.name_dis, (int)rc);
      if (rc == ISO15693_EC_OK) { g_last_unlock = e.name_dis; return true; }
    }
    ESP_LOGW(TAG_PN, "Privacy unlock FAILED");
    g_last_unlock = "failed";
    return false;
  }

  inline void refresh_sysinfo() {
    if (!nfc || !have_uid) return;
    uint8_t bs = g_block_size, nb = g_num_blocks;
    ISO15693ErrorCode rc = nfc->getSystemInfo(last_uid_raw, &bs, &nb);
    if (rc == ISO15693_EC_OK) {
      g_block_size = bs; g_num_blocks = nb;
      ESP_LOGI(TAG_PN, "SystemInfo: block_size=%u, num_blocks=%u", bs, nb);
    } else {
      ESP_LOGW(TAG_PN, "getSystemInfo failed rc=%d", (int)rc);
    }
  }

  // Inventory mit Unlock-Fallback; setzt g_last_unlock auf "none" wenn direkt ok
  inline bool inventory_hex_upper(char* out16) {
    if (!nfc) return false;

    g_last_unlock = "none";  // Standard: kein Unlock nötig
    nfc->setupRF(); delay(5);

    uint8_t uid[8];
    ISO15693ErrorCode rc = nfc->getInventory(uid);
    if (rc != ISO15693_EC_OK) {
      ESP_LOGW(TAG_PN, "inventory failed rc=%d -> try privacy", (int)rc);
      if (!unlock_privacy_round()) { have_uid = false; return false; }
      nfc->setupRF(); delay(30);
      rc = nfc->getInventory(uid);
      if (rc != ISO15693_EC_OK) { ESP_LOGW(TAG_PN, "inventory still failing rc=%d", (int)rc); have_uid = false; return false; }
    }

    memcpy(last_uid_raw, uid, 8);
    have_uid = true;

    char uid_str[17];
    for (int i = 0; i < 8; i++) sprintf(uid_str + i*2, "%02X", uid[7 - i]);
    uid_str[16] = '\0';
    ESP_LOGI(TAG_PN, "UID: %s (unlock=%s)", uid_str, g_last_unlock);

    refresh_sysinfo();
    memcpy(out16, uid_str, 17);
    return true;
  }

  inline ISO15693ErrorCode read_block(uint8_t block, uint8_t* out, uint8_t block_size) {
    return nfc->readSingleBlock(last_uid_raw, block, out, block_size);
  }

  inline bool read_blocks_hex_upper(uint8_t start, uint8_t count, char* out, size_t outlen, uint8_t retries = 3) {
    if (!nfc || !have_uid) return false;
    const uint8_t bs = (g_block_size == 0) ? 4 : g_block_size;
    const size_t need = (size_t)count * bs * 2 + 1;
    if (outlen < need) { ESP_LOGW(TAG_PN, "buffer too small: need=%u have=%u", (unsigned)need, (unsigned)outlen); return false; }

    while (retries--) {
      bool ok = true;
      for (uint8_t i = 0; i < count; i++) {
        uint8_t blk[16] = {0};
        ISO15693ErrorCode rc = read_block(start + i, blk, bs);
        if (rc != ISO15693_EC_OK) { ESP_LOGW(TAG_PN, "read block %u failed rc=%d (retries left=%u)", start + i, (int)rc, (unsigned)retries); ok = false; break; }
        for (uint8_t b = 0; b < bs; b++) sprintf(out + (i*bs*2 + b*2), "%02X", blk[b]);
      }
      if (ok) { out[count*bs*2] = '\0'; ESP_LOGI(TAG_PN, "read %u blocks @%u OK", (unsigned)count, (unsigned)start); return true; }
      delay(5); nfc->setupRF();
    }
    return false;
  }

  inline bool read_full_memory_hex(std::string& out, uint8_t retries = 3) {
    if (!nfc || !have_uid) return false;
    refresh_sysinfo();
    const uint8_t bs = (g_block_size == 0) ? 4 : g_block_size;
    const uint8_t nb = g_num_blocks;
    if (nb == 0) { ESP_LOGW(TAG_PN, "full memory read aborted: num_blocks=0"); return false; }

    ESP_LOGI(TAG_PN, "full memory read: %u blocks x %u bytes", (unsigned)nb, (unsigned)bs);
    out.clear(); out.reserve((size_t)nb * bs * 2);

    while (retries--) {
      bool ok = true;
      for (uint8_t i = 0; i < nb; i++) {
        uint8_t blk[16] = {0};
        ISO15693ErrorCode rc = read_block(i, blk, bs);
        if (rc != ISO15693_EC_OK) { ESP_LOGW(TAG_PN, "read block %u failed rc=%d (retries left=%u)", (unsigned)i, (int)rc, (unsigned)retries); ok = false; break; }
        for (uint8_t b = 0; b < bs; b++) {
          char tmp[3]; sprintf(tmp, "%02X", blk[b]); out.append(tmp);
        }
      }
      if (ok) { ESP_LOGI(TAG_PN, "full memory read OK (hex_len=%u)", (unsigned)out.size()); return true; }
      delay(5); nfc->setupRF();
    }
    ESP_LOGW(TAG_PN, "full memory read FAILED after retries");
    return false;
  }
}
