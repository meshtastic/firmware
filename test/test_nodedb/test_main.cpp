#include "FSCommon.h"
#include "NodeDB.h"
#include "SPILock.h"
#include "TestUtil.h"
#include <unity.h>

// NUM_RESERVED is defined internally in NodeDB.cpp; mirror it here for edge-case tests
#define NUM_RESERVED 4

static constexpr uint32_t OUR_NODE_NUM = 0xDEAD1234;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void createNode(uint32_t nodeId)
{
    meshtastic_Position emptyPos = meshtastic_Position_init_default;
    nodeDB->updatePosition(nodeId, emptyPos, RX_SRC_RADIO);
}

static void fillDatabase(uint32_t start, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        createNode(start + static_cast<uint32_t>(i));
    }
}

static meshtastic_MeshPacket makePacket(uint32_t from, uint32_t to = NODENUM_BROADCAST)
{
    meshtastic_MeshPacket p = meshtastic_MeshPacket_init_default;
    p.from = from;
    p.to = to;
    p.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    return p;
}

// ---------------------------------------------------------------------------
// setUp / tearDown
// ---------------------------------------------------------------------------

void setUp(void)
{
    myNodeInfo.my_node_num = OUR_NODE_NUM;
    meshtastic_Position emptyPos = meshtastic_Position_init_default;
    nodeDB->updatePosition(OUR_NODE_NUM, emptyPos, RX_SRC_RADIO);
    nodeDB->resetNodes(false);
}

void tearDown(void) {}

// ===========================================================================
// Group 1: Initialization
// ===========================================================================

static void test_init_own_node_exists(void)
{
    meshtastic_NodeInfoLite *own = nodeDB->getMeshNode(OUR_NODE_NUM);
    TEST_ASSERT_NOT_NULL(own);
}

static void test_init_count_is_one(void)
{
    TEST_ASSERT_EQUAL(1, nodeDB->getNumMeshNodes());
}

static void test_init_own_node_has_correct_num(void)
{
    meshtastic_NodeInfoLite *own = nodeDB->getMeshNode(OUR_NODE_NUM);
    TEST_ASSERT_NOT_NULL(own);
    TEST_ASSERT_EQUAL_UINT32(OUR_NODE_NUM, own->num);
}

// ===========================================================================
// Group 2: Basic Lookup
// ===========================================================================

static void test_getMeshNode_finds_created_node(void)
{
    createNode(0x100);
    meshtastic_NodeInfoLite *n = nodeDB->getMeshNode(0x100);
    TEST_ASSERT_NOT_NULL(n);
    TEST_ASSERT_EQUAL_UINT32(0x100, n->num);
}

static void test_getMeshNode_returns_null_for_unknown(void)
{
    meshtastic_NodeInfoLite *n = nodeDB->getMeshNode(0xBADBAD);
    TEST_ASSERT_NULL(n);
}

static void test_getNumMeshNodes_increments_on_create(void)
{
    TEST_ASSERT_EQUAL(1, nodeDB->getNumMeshNodes());
    createNode(0x200);
    TEST_ASSERT_EQUAL(2, nodeDB->getNumMeshNodes());
}

static void test_getNumMeshNodes_after_multiple_creates(void)
{
    fillDatabase(0x300, 5);
    TEST_ASSERT_EQUAL(6, nodeDB->getNumMeshNodes()); // own + 5
}

static void test_getMeshNodeChannel_returns_default(void)
{
    createNode(0x400);
    uint8_t ch = nodeDB->getMeshNodeChannel(0x400);
    TEST_ASSERT_EQUAL_UINT8(0, ch);
}

// ===========================================================================
// Group 3: Node Creation & Sorted Order
// ===========================================================================

static void test_create_via_updatePosition(void)
{
    meshtastic_Position pos = meshtastic_Position_init_default;
    pos.latitude_i = 123456;
    nodeDB->updatePosition(0x500, pos, RX_SRC_RADIO);
    meshtastic_NodeInfoLite *n = nodeDB->getMeshNode(0x500);
    TEST_ASSERT_NOT_NULL(n);
}

static void test_create_via_updateUser(void)
{
    meshtastic_User user = meshtastic_User_init_default;
    strncpy(user.long_name, "TestUser", sizeof(user.long_name));
    strncpy(user.short_name, "TU", sizeof(user.short_name));
    nodeDB->updateUser(0x600, user);
    meshtastic_NodeInfoLite *n = nodeDB->getMeshNode(0x600);
    TEST_ASSERT_NOT_NULL(n);
    TEST_ASSERT_TRUE(n->has_user);
}

static void test_create_via_updateTelemetry(void)
{
    meshtastic_Telemetry t = meshtastic_Telemetry_init_default;
    t.which_variant = meshtastic_Telemetry_device_metrics_tag;
    t.variant.device_metrics.battery_level = 75;
    nodeDB->updateTelemetry(0x700, t, RX_SRC_RADIO);
    meshtastic_NodeInfoLite *n = nodeDB->getMeshNode(0x700);
    TEST_ASSERT_NOT_NULL(n);
}

static void test_create_duplicate_returns_existing(void)
{
    createNode(0x800);
    size_t before = nodeDB->getNumMeshNodes();
    createNode(0x800);
    TEST_ASSERT_EQUAL(before, nodeDB->getNumMeshNodes());
}

static void test_create_maintains_sorted_order(void)
{
    createNode(300);
    createNode(100);
    createNode(200);
    TEST_ASSERT_NOT_NULL(nodeDB->getMeshNode(100));
    TEST_ASSERT_NOT_NULL(nodeDB->getMeshNode(200));
    TEST_ASSERT_NOT_NULL(nodeDB->getMeshNode(300));

    // Verify ascending order via readNextMeshNode
    uint32_t idx = 0;
    uint32_t prev = 0;
    const meshtastic_NodeInfoLite *n;
    while ((n = nodeDB->readNextMeshNode(idx)) != nullptr) {
        TEST_ASSERT_TRUE_MESSAGE(n->num >= prev, "Nodes not in ascending order");
        prev = n->num;
    }
}

static void test_create_multiple_nodes_all_findable(void)
{
    for (uint32_t i = 10; i < 20; i++) {
        createNode(i);
    }
    for (uint32_t i = 10; i < 20; i++) {
        TEST_ASSERT_NOT_NULL(nodeDB->getMeshNode(i));
    }
}

static void test_create_interleaved_maintains_sort(void)
{
    createNode(500);
    createNode(100);
    createNode(300);
    createNode(200);
    createNode(400);

    uint32_t idx = 0;
    uint32_t prev = 0;
    const meshtastic_NodeInfoLite *n;
    while ((n = nodeDB->readNextMeshNode(idx)) != nullptr) {
        TEST_ASSERT_TRUE_MESSAGE(n->num >= prev, "Interleaved insert broke sort");
        prev = n->num;
    }
}

// ===========================================================================
// Group 4: Node Removal
// ===========================================================================

static void test_remove_existing_node(void)
{
    createNode(0x900);
    TEST_ASSERT_NOT_NULL(nodeDB->getMeshNode(0x900));
    nodeDB->removeNodeByNum(0x900);
    TEST_ASSERT_NULL(nodeDB->getMeshNode(0x900));
}

static void test_remove_decrements_count(void)
{
    createNode(0xA00);
    size_t before = nodeDB->getNumMeshNodes();
    nodeDB->removeNodeByNum(0xA00);
    TEST_ASSERT_EQUAL(before - 1, nodeDB->getNumMeshNodes());
}

static void test_remove_nonexistent_safe(void)
{
    size_t before = nodeDB->getNumMeshNodes();
    nodeDB->removeNodeByNum(0xFFFFFF);
    TEST_ASSERT_EQUAL(before, nodeDB->getNumMeshNodes());
}

static void test_remove_preserves_other_nodes(void)
{
    createNode(0xB00);
    createNode(0xB01);
    createNode(0xB02);
    nodeDB->removeNodeByNum(0xB01);
    TEST_ASSERT_NOT_NULL(nodeDB->getMeshNode(0xB00));
    TEST_ASSERT_NULL(nodeDB->getMeshNode(0xB01));
    TEST_ASSERT_NOT_NULL(nodeDB->getMeshNode(0xB02));
}

static void test_remove_maintains_sorted_order(void)
{
    createNode(0xC00);
    createNode(0xC01);
    createNode(0xC02);
    nodeDB->removeNodeByNum(0xC01);

    uint32_t idx = 0;
    uint32_t prev = 0;
    const meshtastic_NodeInfoLite *n;
    while ((n = nodeDB->readNextMeshNode(idx)) != nullptr) {
        TEST_ASSERT_TRUE_MESSAGE(n->num >= prev, "Remove broke sorted order");
        prev = n->num;
    }
}

// ===========================================================================
// Group 5: resetNodes
// ===========================================================================

static void test_resetNodes_clears_to_own_node(void)
{
    fillDatabase(0x1000, 10);
    TEST_ASSERT_EQUAL(11, nodeDB->getNumMeshNodes());
    nodeDB->resetNodes(false);
    TEST_ASSERT_EQUAL(1, nodeDB->getNumMeshNodes());
    TEST_ASSERT_NOT_NULL(nodeDB->getMeshNode(OUR_NODE_NUM));
}

static void test_resetNodes_preserves_own_node_data(void)
{
    // resetNodes calls clearLocalPosition() which zeroes the own node's position,
    // but user info on the own node should survive the reset.
    meshtastic_User user = meshtastic_User_init_default;
    strncpy(user.long_name, "OwnUser", sizeof(user.long_name));
    strncpy(user.short_name, "OU", sizeof(user.short_name));
    nodeDB->updateUser(OUR_NODE_NUM, user);

    meshtastic_NodeInfoLite *before = nodeDB->getMeshNode(OUR_NODE_NUM);
    TEST_ASSERT_NOT_NULL(before);
    TEST_ASSERT_TRUE(before->has_user);

    nodeDB->resetNodes(false);

    meshtastic_NodeInfoLite *after = nodeDB->getMeshNode(OUR_NODE_NUM);
    TEST_ASSERT_NOT_NULL(after);
    TEST_ASSERT_TRUE(after->has_user);
    TEST_ASSERT_EQUAL_STRING("OU", after->user.short_name);
}

static void test_resetNodes_keepFavorites_true(void)
{
    createNode(0x2000);
    nodeDB->set_favorite(true, 0x2000);
    createNode(0x2001); // not favorite
    nodeDB->resetNodes(true);

    TEST_ASSERT_NOT_NULL(nodeDB->getMeshNode(0x2000));
}

static void test_resetNodes_keepFavorites_removes_nonfavorites(void)
{
    createNode(0x3000);
    nodeDB->set_favorite(true, 0x3000);
    createNode(0x3001); // not favorite
    nodeDB->resetNodes(true);

    TEST_ASSERT_NULL(nodeDB->getMeshNode(0x3001));
}

static void test_resetNodes_keepFavorites_always_keeps_own(void)
{
    createNode(0x4000);
    nodeDB->set_favorite(true, 0x4000);
    // Own node is not explicitly favorited
    nodeDB->resetNodes(true);

    TEST_ASSERT_NOT_NULL(nodeDB->getMeshNode(OUR_NODE_NUM));
}

static void test_resetNodes_keepFavorites_sorted_after(void)
{
    createNode(0x5002);
    nodeDB->set_favorite(true, 0x5002);
    createNode(0x5000);
    nodeDB->set_favorite(true, 0x5000);
    createNode(0x5001); // not favorite, will be removed
    nodeDB->resetNodes(true);

    uint32_t idx = 0;
    uint32_t prev = 0;
    const meshtastic_NodeInfoLite *n;
    while ((n = nodeDB->readNextMeshNode(idx)) != nullptr) {
        TEST_ASSERT_TRUE_MESSAGE(n->num >= prev, "resetNodes(true) broke sort");
        prev = n->num;
    }
}

// ===========================================================================
// Group 6: Favorites
// ===========================================================================

static void test_set_favorite_marks_node(void)
{
    createNode(0x6000);
    nodeDB->set_favorite(true, 0x6000);
    TEST_ASSERT_TRUE(nodeDB->isFavorite(0x6000));
}

static void test_set_favorite_unmark(void)
{
    createNode(0x6100);
    nodeDB->set_favorite(true, 0x6100);
    nodeDB->set_favorite(false, 0x6100);
    TEST_ASSERT_FALSE(nodeDB->isFavorite(0x6100));
}

static void test_isFavorite_unknown_node_false(void)
{
    TEST_ASSERT_FALSE(nodeDB->isFavorite(0xBAD));
}

static void test_isFavorite_broadcast_false(void)
{
    TEST_ASSERT_FALSE(nodeDB->isFavorite(NODENUM_BROADCAST));
}

static void test_isFromOrToFavorited_from(void)
{
    createNode(0x6200);
    nodeDB->set_favorite(true, 0x6200);
    meshtastic_MeshPacket p = makePacket(0x6200);
    TEST_ASSERT_TRUE(nodeDB->isFromOrToFavoritedNode(p));
}

static void test_isFromOrToFavorited_neither(void)
{
    createNode(0x6300);
    createNode(0x6301);
    meshtastic_MeshPacket p = makePacket(0x6300, 0x6301);
    TEST_ASSERT_FALSE(nodeDB->isFromOrToFavoritedNode(p));
}

// ===========================================================================
// Group 7: Display Order
// ===========================================================================
// Sort order: own node -> favorites -> last_heard desc -> nodeNum asc

static void test_displayOrder_own_node_first(void)
{
    createNode(0x7000);
    meshtastic_NodeInfoLite *first = nodeDB->getMeshNodeByIndex(0);
    TEST_ASSERT_NOT_NULL(first);
    TEST_ASSERT_EQUAL_UINT32(OUR_NODE_NUM, first->num);
}

static void test_displayOrder_favorites_before_regular(void)
{
    createNode(0x7100);
    createNode(0x7101);
    nodeDB->set_favorite(true, 0x7101);

    // Favorite should appear before non-favorite (after own node)
    meshtastic_NodeInfoLite *atIndex1 = nodeDB->getMeshNodeByIndex(1);
    TEST_ASSERT_NOT_NULL(atIndex1);
    TEST_ASSERT_EQUAL_UINT32(0x7101, atIndex1->num);
}

static void test_displayOrder_last_heard_descending(void)
{
    // Create two nodes with different last_heard values
    createNode(0x7200);
    createNode(0x7201);
    meshtastic_NodeInfoLite *older = nodeDB->getMeshNode(0x7200);
    meshtastic_NodeInfoLite *newer = nodeDB->getMeshNode(0x7201);
    TEST_ASSERT_NOT_NULL(older);
    TEST_ASSERT_NOT_NULL(newer);
    older->last_heard = 1000;
    newer->last_heard = 2000;

    // Force display rebuild by marking dirty
    nodeDB->pause_sort(false);
    meshtastic_NodeInfoLite *atIndex1 = nodeDB->getMeshNodeByIndex(1);
    TEST_ASSERT_NOT_NULL(atIndex1);
    TEST_ASSERT_EQUAL_UINT32(0x7201, atIndex1->num);
}

static void test_displayOrder_nodenum_tiebreak(void)
{
    createNode(0x7302);
    createNode(0x7300);
    meshtastic_NodeInfoLite *a = nodeDB->getMeshNode(0x7300);
    meshtastic_NodeInfoLite *b = nodeDB->getMeshNode(0x7302);
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NOT_NULL(b);
    a->last_heard = 5000;
    b->last_heard = 5000;

    nodeDB->pause_sort(false);
    meshtastic_NodeInfoLite *atIndex1 = nodeDB->getMeshNodeByIndex(1);
    TEST_ASSERT_NOT_NULL(atIndex1);
    TEST_ASSERT_EQUAL_UINT32(0x7300, atIndex1->num); // lower num first
}

static void test_displayOrder_out_of_bounds_null(void)
{
    TEST_ASSERT_NULL(nodeDB->getMeshNodeByIndex(999));
}

static void test_displayOrder_count_matches(void)
{
    fillDatabase(0x7400, 5);
    size_t total = nodeDB->getNumMeshNodes();
    for (size_t i = 0; i < total; i++) {
        TEST_ASSERT_NOT_NULL(nodeDB->getMeshNodeByIndex(i));
    }
    TEST_ASSERT_NULL(nodeDB->getMeshNodeByIndex(total));
}

static void test_displayOrder_pause_sort(void)
{
    createNode(0x7500);
    createNode(0x7501);
    meshtastic_NodeInfoLite *a = nodeDB->getMeshNode(0x7500);
    TEST_ASSERT_NOT_NULL(a);
    a->last_heard = 9999;

    // Pause sorting and trigger rebuild — should not reorder unless dirty
    nodeDB->pause_sort(true);

    // getMeshNodeByIndex calls rebuildDisplayOrder which respects pause when not dirty
    // After creating nodes above, displayNodesDirty was already set, so the first call
    // will still rebuild. We need to consume that rebuild first.
    nodeDB->getMeshNodeByIndex(0);

    // Now change last_heard without dirtying the display list
    meshtastic_NodeInfoLite *b = nodeDB->getMeshNode(0x7501);
    TEST_ASSERT_NOT_NULL(b);
    b->last_heard = 99999; // newer, but display shouldn't resort since paused & not dirty

    // Since displayNodesDirty is false and sorting is paused, order should NOT change
    // (The old order is still valid from the previous rebuild)
    meshtastic_NodeInfoLite *first = nodeDB->getMeshNodeByIndex(0);
    TEST_ASSERT_NOT_NULL(first);
    TEST_ASSERT_EQUAL_UINT32(OUR_NODE_NUM, first->num); // own node always first
}

// ===========================================================================
// Group 8: Favorite Router Cache
// ===========================================================================

static void test_favoriteRouter_empty_by_default(void)
{
    const std::vector<uint8_t> &cache = nodeDB->getFavoriteRouterLastBytes();
    TEST_ASSERT_EQUAL(0, cache.size());
}

static void test_favoriteRouter_includes_favorite_router(void)
{
    createNode(0x8000);
    meshtastic_User user = meshtastic_User_init_default;
    user.role = meshtastic_Config_DeviceConfig_Role_ROUTER;
    strncpy(user.long_name, "Router", sizeof(user.long_name));
    strncpy(user.short_name, "RT", sizeof(user.short_name));
    nodeDB->updateUser(0x8000, user);
    nodeDB->set_favorite(true, 0x8000);

    const std::vector<uint8_t> &cache = nodeDB->getFavoriteRouterLastBytes();
    bool found = false;
    uint8_t expected = nodeDB->getLastByteOfNodeNum(0x8000);
    for (size_t i = 0; i < cache.size(); i++) {
        if (cache[i] == expected) {
            found = true;
            break;
        }
    }
    TEST_ASSERT_TRUE(found);
}

static void test_favoriteRouter_excludes_nonfavorite_router(void)
{
    createNode(0x8100);
    meshtastic_User user = meshtastic_User_init_default;
    user.role = meshtastic_Config_DeviceConfig_Role_ROUTER;
    strncpy(user.long_name, "Router2", sizeof(user.long_name));
    strncpy(user.short_name, "R2", sizeof(user.short_name));
    nodeDB->updateUser(0x8100, user);
    // NOT favorited

    const std::vector<uint8_t> &cache = nodeDB->getFavoriteRouterLastBytes();
    uint8_t lastByte = nodeDB->getLastByteOfNodeNum(0x8100);
    bool found = false;
    for (size_t i = 0; i < cache.size(); i++) {
        if (cache[i] == lastByte) {
            found = true;
            break;
        }
    }
    TEST_ASSERT_FALSE(found);
}

static void test_favoriteRouter_excludes_favorite_nonrouter(void)
{
    createNode(0x8200);
    meshtastic_User user = meshtastic_User_init_default;
    user.role = meshtastic_Config_DeviceConfig_Role_CLIENT;
    strncpy(user.long_name, "Client", sizeof(user.long_name));
    strncpy(user.short_name, "CL", sizeof(user.short_name));
    nodeDB->updateUser(0x8200, user);
    nodeDB->set_favorite(true, 0x8200);

    const std::vector<uint8_t> &cache = nodeDB->getFavoriteRouterLastBytes();
    uint8_t lastByte = nodeDB->getLastByteOfNodeNum(0x8200);
    bool found = false;
    for (size_t i = 0; i < cache.size(); i++) {
        if (cache[i] == lastByte) {
            found = true;
            break;
        }
    }
    TEST_ASSERT_FALSE(found);
}

// ===========================================================================
// Group 9: Eviction
// ===========================================================================

static void test_isFull_at_max_nodes(void)
{
    // Fill up to MAX_NUM_NODES (own node already occupies one slot)
    fillDatabase(0x10000, MAX_NUM_NODES - 1);
    TEST_ASSERT_EQUAL(MAX_NUM_NODES, nodeDB->getNumMeshNodes());
    TEST_ASSERT_TRUE(nodeDB->isFull());
}

static void test_eviction_occurs_when_full(void)
{
    fillDatabase(0x10000, MAX_NUM_NODES - 1);
    TEST_ASSERT_EQUAL(MAX_NUM_NODES, nodeDB->getNumMeshNodes());

    // Adding one more should trigger eviction and still succeed
    createNode(0xA0000);
    TEST_ASSERT_NOT_NULL(nodeDB->getMeshNode(0xA0000));
    // Count should stay at MAX_NUM_NODES (evicted one, added one)
    TEST_ASSERT_EQUAL(MAX_NUM_NODES, nodeDB->getNumMeshNodes());
}

static void test_eviction_prefers_boring_nodes(void)
{
    // Fill with nodes, some with public keys and some without
    fillDatabase(0x10000, MAX_NUM_NODES - 1);

    // Give one node a public key so it's "interesting"
    meshtastic_NodeInfoLite *keyed = nodeDB->getMeshNode(0x10001);
    TEST_ASSERT_NOT_NULL(keyed);
    keyed->user.public_key.size = 32;
    memset(keyed->user.public_key.bytes, 0xAA, 32);
    keyed->last_heard = 0; // make it the oldest

    // A "boring" node (no public key) with slightly newer last_heard
    meshtastic_NodeInfoLite *boring = nodeDB->getMeshNode(0x10002);
    TEST_ASSERT_NOT_NULL(boring);
    boring->last_heard = 1; // newer than keyed, but still boring

    // Trigger eviction by adding a new node
    createNode(0xB0000);

    // The keyed node should survive, boring node should be evicted
    TEST_ASSERT_NOT_NULL_MESSAGE(nodeDB->getMeshNode(0x10001), "Keyed node was evicted");
}

static void test_eviction_oldest_unfavorite_chosen(void)
{
    fillDatabase(0x10000, MAX_NUM_NODES - 1);

    // Set one node to have the oldest last_heard
    meshtastic_NodeInfoLite *oldest = nodeDB->getMeshNode(0x10005);
    TEST_ASSERT_NOT_NULL(oldest);
    oldest->last_heard = 0;

    // Give other nodes more recent last_heard
    for (uint32_t i = 0x10000; i < 0x10000 + (MAX_NUM_NODES - 1); i++) {
        if (i == 0x10005)
            continue;
        meshtastic_NodeInfoLite *n = nodeDB->getMeshNode(i);
        if (n)
            n->last_heard = 5000;
    }

    createNode(0xC0000);

    // The oldest unfavorite should have been evicted
    TEST_ASSERT_NULL_MESSAGE(nodeDB->getMeshNode(0x10005), "Oldest node survived eviction");
    TEST_ASSERT_NOT_NULL(nodeDB->getMeshNode(0xC0000));
}

static void test_eviction_skips_own_node(void)
{
    fillDatabase(0x10000, MAX_NUM_NODES - 1);

    // Make own node the oldest
    meshtastic_NodeInfoLite *own = nodeDB->getMeshNode(OUR_NODE_NUM);
    TEST_ASSERT_NOT_NULL(own);
    own->last_heard = 0;

    // Give all other nodes more recent last_heard
    for (uint32_t i = 0x10000; i < 0x10000 + (MAX_NUM_NODES - 1); i++) {
        meshtastic_NodeInfoLite *n = nodeDB->getMeshNode(i);
        if (n)
            n->last_heard = 5000;
    }

    createNode(0xD0000);

    // Own node must survive
    TEST_ASSERT_NOT_NULL(nodeDB->getMeshNode(OUR_NODE_NUM));
}

static void test_eviction_skips_favorites(void)
{
    fillDatabase(0x10000, MAX_NUM_NODES - 1);

    // Mark a node as favorite with oldest last_heard
    meshtastic_NodeInfoLite *fav = nodeDB->getMeshNode(0x10010);
    TEST_ASSERT_NOT_NULL(fav);
    fav->last_heard = 0;
    nodeDB->set_favorite(true, 0x10010);

    // Give all others newer timestamps
    for (uint32_t i = 0x10000; i < 0x10000 + (MAX_NUM_NODES - 1); i++) {
        if (i == 0x10010)
            continue;
        meshtastic_NodeInfoLite *n = nodeDB->getMeshNode(i);
        if (n)
            n->last_heard = 5000;
    }

    createNode(0xE0000);

    // Favorite must survive
    TEST_ASSERT_NOT_NULL_MESSAGE(nodeDB->getMeshNode(0x10010), "Favorite was evicted");
}

static void test_eviction_returns_null_when_unevictable(void)
{
    fillDatabase(0x10000, MAX_NUM_NODES - 1);

    // Mark all nodes as favorite (unevictable)
    for (uint32_t i = 0x10000; i < 0x10000 + (MAX_NUM_NODES - 1); i++) {
        nodeDB->set_favorite(true, i);
    }

    size_t countBefore = nodeDB->getNumMeshNodes();

    // updatePosition calls getOrCreateMeshNode which returns nullptr when all are protected
    meshtastic_Position emptyPos = meshtastic_Position_init_default;
    nodeDB->updatePosition(0xF0000, emptyPos, RX_SRC_RADIO);

    // The new node should NOT have been created
    TEST_ASSERT_NULL(nodeDB->getMeshNode(0xF0000));
    TEST_ASSERT_EQUAL(countBefore, nodeDB->getNumMeshNodes());
}

// ===========================================================================
// Group 10: updateFrom
// ===========================================================================

static void test_updateFrom_creates_node(void)
{
    meshtastic_MeshPacket p = makePacket(0x9000);
    p.rx_time = 12345;
    nodeDB->updateFrom(p);
    TEST_ASSERT_NOT_NULL(nodeDB->getMeshNode(0x9000));
}

static void test_updateFrom_sets_last_heard(void)
{
    meshtastic_MeshPacket p = makePacket(0x9100);
    p.rx_time = 99999;
    nodeDB->updateFrom(p);
    meshtastic_NodeInfoLite *n = nodeDB->getMeshNode(0x9100);
    TEST_ASSERT_NOT_NULL(n);
    TEST_ASSERT_EQUAL_UINT32(99999, n->last_heard);
}

static void test_updateFrom_sets_snr(void)
{
    meshtastic_MeshPacket p = makePacket(0x9200);
    p.rx_time = 100;
    p.rx_snr = 7.5f;
    nodeDB->updateFrom(p);
    meshtastic_NodeInfoLite *n = nodeDB->getMeshNode(0x9200);
    TEST_ASSERT_NOT_NULL(n);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 7.5f, n->snr);
}

static void test_updateFrom_ignores_own_node(void)
{
    size_t before = nodeDB->getNumMeshNodes();
    meshtastic_MeshPacket p = makePacket(OUR_NODE_NUM);
    p.rx_time = 100;
    nodeDB->updateFrom(p);
    // Should not have created a duplicate
    TEST_ASSERT_EQUAL(before, nodeDB->getNumMeshNodes());
}

static void test_updateFrom_sets_hops_away(void)
{
    meshtastic_MeshPacket p = makePacket(0x9300);
    p.rx_time = 100;
    p.hop_start = 5;
    p.hop_limit = 2;
    p.decoded.has_bitfield = true;
    nodeDB->updateFrom(p);
    meshtastic_NodeInfoLite *n = nodeDB->getMeshNode(0x9300);
    TEST_ASSERT_NOT_NULL(n);
    TEST_ASSERT_TRUE(n->has_hops_away);
    TEST_ASSERT_EQUAL(3, n->hops_away); // hop_start - hop_limit = 5 - 2 = 3
}

// ===========================================================================
// Group 11: Edge Cases
// ===========================================================================

static void test_reserved_nodenum_0_rejected(void)
{
    size_t before = nodeDB->getNumMeshNodes();
    createNode(0);
    TEST_ASSERT_EQUAL(before, nodeDB->getNumMeshNodes());
}

static void test_reserved_nodenum_3_rejected(void)
{
    size_t before = nodeDB->getNumMeshNodes();
    createNode(3);
    TEST_ASSERT_EQUAL(before, nodeDB->getNumMeshNodes());
}

static void test_nodenum_4_accepted(void)
{
    createNode(4);
    TEST_ASSERT_NOT_NULL(nodeDB->getMeshNode(4));
}

static void test_broadcast_nodenum_rejected(void)
{
    size_t before = nodeDB->getNumMeshNodes();
    createNode(NODENUM_BROADCAST);
    TEST_ASSERT_EQUAL(before, nodeDB->getNumMeshNodes());
}

static void test_getNodeNum_returns_own(void)
{
    TEST_ASSERT_EQUAL_UINT32(OUR_NODE_NUM, nodeDB->getNodeNum());
}

static void test_readNextMeshNode_null_past_end(void)
{
    uint32_t idx = nodeDB->getNumMeshNodes(); // past the last valid index
    const meshtastic_NodeInfoLite *n = nodeDB->readNextMeshNode(idx);
    TEST_ASSERT_NULL(n);
}

// ===========================================================================
// Group 12: Stress Tests
// ===========================================================================

static void test_fill_to_max_all_findable(void)
{
    fillDatabase(0x20000, MAX_NUM_NODES - 1);
    TEST_ASSERT_EQUAL(MAX_NUM_NODES, nodeDB->getNumMeshNodes());

    for (uint32_t i = 0x20000; i < 0x20000 + (MAX_NUM_NODES - 1); i++) {
        TEST_ASSERT_NOT_NULL_MESSAGE(nodeDB->getMeshNode(i), "Node not found in full DB");
    }
}

static void test_add_remove_cycle(void)
{
    fillDatabase(0x30000, 20);
    TEST_ASSERT_EQUAL(21, nodeDB->getNumMeshNodes()); // own + 20

    // Remove 10
    for (uint32_t i = 0x30000; i < 0x30000 + 10; i++) {
        nodeDB->removeNodeByNum(i);
    }
    TEST_ASSERT_EQUAL(11, nodeDB->getNumMeshNodes()); // own + 10 remaining

    // Add 20 more
    fillDatabase(0x40000, 20);
    TEST_ASSERT_EQUAL(31, nodeDB->getNumMeshNodes()); // own + 10 + 20
}

static void test_readNextMeshNode_covers_all(void)
{
    fillDatabase(0x50000, 10);
    uint32_t idx = 0;
    size_t count = 0;
    while (nodeDB->readNextMeshNode(idx) != nullptr) {
        count++;
    }
    TEST_ASSERT_EQUAL(nodeDB->getNumMeshNodes(), count);
}

static void test_churn_eviction_correctness(void)
{
    // Add 2x MAX_NUM_NODES nodes — the first batch will be evicted by the second
    size_t totalToAdd = 2 * MAX_NUM_NODES;
    for (size_t i = 0; i < totalToAdd; i++) {
        createNode(0x60000 + static_cast<uint32_t>(i));
    }

    // DB should not exceed MAX_NUM_NODES
    TEST_ASSERT_TRUE(nodeDB->getNumMeshNodes() <= (size_t)MAX_NUM_NODES);

    // The most recently added nodes should be present
    size_t foundRecent = 0;
    for (size_t i = totalToAdd - 10; i < totalToAdd; i++) {
        if (nodeDB->getMeshNode(0x60000 + static_cast<uint32_t>(i)) != nullptr) {
            foundRecent++;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(foundRecent > 0, "No recent nodes survived churn");
}

// ===========================================================================
// Test Runner
// ===========================================================================

void setup()
{
    delay(10);
    delay(2000);

    initializeTestEnvironment();
    initSPI();
    fsInit();
    myNodeInfo.my_node_num = OUR_NODE_NUM;
    nodeDB = new NodeDB();

    UNITY_BEGIN();

    // Group 1: Initialization
    RUN_TEST(test_init_own_node_exists);
    RUN_TEST(test_init_count_is_one);
    RUN_TEST(test_init_own_node_has_correct_num);

    // Group 2: Basic Lookup
    RUN_TEST(test_getMeshNode_finds_created_node);
    RUN_TEST(test_getMeshNode_returns_null_for_unknown);
    RUN_TEST(test_getNumMeshNodes_increments_on_create);
    RUN_TEST(test_getNumMeshNodes_after_multiple_creates);
    RUN_TEST(test_getMeshNodeChannel_returns_default);

    // Group 3: Node Creation & Sorted Order
    RUN_TEST(test_create_via_updatePosition);
    RUN_TEST(test_create_via_updateUser);
    RUN_TEST(test_create_via_updateTelemetry);
    RUN_TEST(test_create_duplicate_returns_existing);
    RUN_TEST(test_create_maintains_sorted_order);
    RUN_TEST(test_create_multiple_nodes_all_findable);
    RUN_TEST(test_create_interleaved_maintains_sort);

    // Group 4: Node Removal
    RUN_TEST(test_remove_existing_node);
    RUN_TEST(test_remove_decrements_count);
    RUN_TEST(test_remove_nonexistent_safe);
    RUN_TEST(test_remove_preserves_other_nodes);
    RUN_TEST(test_remove_maintains_sorted_order);

    // Group 5: resetNodes
    RUN_TEST(test_resetNodes_clears_to_own_node);
    RUN_TEST(test_resetNodes_preserves_own_node_data);
    RUN_TEST(test_resetNodes_keepFavorites_true);
    RUN_TEST(test_resetNodes_keepFavorites_removes_nonfavorites);
    RUN_TEST(test_resetNodes_keepFavorites_always_keeps_own);
    RUN_TEST(test_resetNodes_keepFavorites_sorted_after);

    // Group 6: Favorites
    RUN_TEST(test_set_favorite_marks_node);
    RUN_TEST(test_set_favorite_unmark);
    RUN_TEST(test_isFavorite_unknown_node_false);
    RUN_TEST(test_isFavorite_broadcast_false);
    RUN_TEST(test_isFromOrToFavorited_from);
    RUN_TEST(test_isFromOrToFavorited_neither);

    // Group 7: Display Order
    RUN_TEST(test_displayOrder_own_node_first);
    RUN_TEST(test_displayOrder_favorites_before_regular);
    RUN_TEST(test_displayOrder_last_heard_descending);
    RUN_TEST(test_displayOrder_nodenum_tiebreak);
    RUN_TEST(test_displayOrder_out_of_bounds_null);
    RUN_TEST(test_displayOrder_count_matches);
    RUN_TEST(test_displayOrder_pause_sort);

    // Group 8: Favorite Router Cache
    RUN_TEST(test_favoriteRouter_empty_by_default);
    RUN_TEST(test_favoriteRouter_includes_favorite_router);
    RUN_TEST(test_favoriteRouter_excludes_nonfavorite_router);
    RUN_TEST(test_favoriteRouter_excludes_favorite_nonrouter);

    // Group 9: Eviction
    RUN_TEST(test_isFull_at_max_nodes);
    RUN_TEST(test_eviction_occurs_when_full);
    RUN_TEST(test_eviction_prefers_boring_nodes);
    RUN_TEST(test_eviction_oldest_unfavorite_chosen);
    RUN_TEST(test_eviction_skips_own_node);
    RUN_TEST(test_eviction_skips_favorites);
    RUN_TEST(test_eviction_returns_null_when_unevictable);

    // Group 10: updateFrom
    RUN_TEST(test_updateFrom_creates_node);
    RUN_TEST(test_updateFrom_sets_last_heard);
    RUN_TEST(test_updateFrom_sets_snr);
    RUN_TEST(test_updateFrom_ignores_own_node);
    RUN_TEST(test_updateFrom_sets_hops_away);

    // Group 11: Edge Cases
    RUN_TEST(test_reserved_nodenum_0_rejected);
    RUN_TEST(test_reserved_nodenum_3_rejected);
    RUN_TEST(test_nodenum_4_accepted);
    RUN_TEST(test_broadcast_nodenum_rejected);
    RUN_TEST(test_getNodeNum_returns_own);
    RUN_TEST(test_readNextMeshNode_null_past_end);

    // Group 12: Stress Tests
    RUN_TEST(test_fill_to_max_all_findable);
    RUN_TEST(test_add_remove_cycle);
    RUN_TEST(test_readNextMeshNode_covers_all);
    RUN_TEST(test_churn_eviction_correctness);

    delete nodeDB;
    nodeDB = nullptr;
    exit(UNITY_END());
}

void loop() {}
