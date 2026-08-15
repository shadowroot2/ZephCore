/*
 * SPDX-License-Identifier: MIT
 * RepeaterDataStore - Filesystem storage for repeater
 *
 * Uses /lfs/repeater/ prefix to keep data separate from companion.
 * This allows flashing back and forth between roles without corruption.
 */

#pragma once

#include <cstdint>
#include <stddef.h>
#include <mesh/Identity.h>
#include <helpers/NodePrefs.h>
#include <helpers/ClientACL.h>
#include <helpers/RegionMap.h>

struct RepeaterBridgePrefs {
    uint8_t peer_mac[6];
    uint8_t lmk[16];
    uint8_t key_is_custom;
    uint8_t transport;  /* 0 = BLE, 1 = ESP-NOW */
    uint8_t enabled;
    uint8_t peer_addr_type; /* BLE: 0 = public, 1 = random */
    uint8_t forward_priority; /* 0 = primary, higher values wait longer */
};

class RepeaterDataStore {
public:
    RepeaterDataStore();

    /* Initialize filesystem and repeater directory */
    bool begin();

    /* Identity management */
    bool loadIdentity(mesh::LocalIdentity& id);
    bool saveIdentity(const mesh::LocalIdentity& id);

    /* Prefs management */
    bool loadPrefs(NodePrefs& prefs);
    bool savePrefs(const NodePrefs& prefs);

    /* ESP-NOW bridge settings are deliberately separate from NodePrefs: the
     * latter has an Arduino-compatible positional on-disk layout. */
    bool loadBridgePrefs(RepeaterBridgePrefs& prefs);
    bool saveBridgePrefs(const RepeaterBridgePrefs& prefs);

    /* ACL management - paths passed to ClientACL */
    const char* getAclPath() const;

    /* Region management - paths passed to RegionMap */
    const char* getRegionsPath() const;

    /* Factory reset - erase all repeater data */
    bool formatFileSystem();

    /* Get base path for repeater storage */
    const char* getBasePath() const;

private:
    bool _initialized;
    static constexpr const char* BASE_PATH = "/lfs/repeater";
};
