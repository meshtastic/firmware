#include "MeshMapModule.h"

#if ARCH_PORTDUINO && HAS_SQLITE3

MeshMapModule *meshMapModule;

// Each entry upgrades the schema by one version; never edit one that has shipped, append a new one.
// Times are epoch seconds, except rx_frame.rx_time_ms (milliseconds, to order copies of one flood).
static const char *const MIGRATIONS[] = {
    // v1
    "CREATE TABLE rx_frame("
    "  rx_time_ms INTEGER NOT NULL,"
    "  sender INTEGER NOT NULL,"
    "  destination INTEGER NOT NULL,"
    "  packet_id INTEGER NOT NULL,"
    "  channel_hash INTEGER,"
    "  hop_start INTEGER,"
    "  hop_limit INTEGER,"
    "  relay_byte INTEGER,"
    "  next_hop_byte INTEGER,"
    "  want_ack INTEGER NOT NULL DEFAULT 0,"
    "  via_mqtt INTEGER NOT NULL DEFAULT 0,"
    "  transport INTEGER NOT NULL DEFAULT 0,"
    "  snr REAL,"
    "  rssi INTEGER,"
    "  size INTEGER NOT NULL,"
    "  airtime_ms INTEGER,"
    "  portnum INTEGER,"
    "  our_action INTEGER NOT NULL DEFAULT 0"
    ");"
    "CREATE INDEX rx_frame_packet ON rx_frame(sender, packet_id);"
    "CREATE INDEX rx_frame_time ON rx_frame(rx_time_ms);"

    "CREATE TABLE node("
    "  nodenum INTEGER PRIMARY KEY,"
    "  first_heard INTEGER NOT NULL,"
    "  last_heard INTEGER NOT NULL,"
    "  role INTEGER,"
    "  hw_model INTEGER,"
    "  mobility INTEGER NOT NULL DEFAULT 0,"
    "  mobility_conf REAL"
    ");"

    "CREATE TABLE link("
    "  src INTEGER NOT NULL,"
    "  dst INTEGER NOT NULL,"
    "  evidence INTEGER NOT NULL,"
    "  first_seen INTEGER NOT NULL,"
    "  last_seen INTEGER NOT NULL,"
    "  samples INTEGER NOT NULL DEFAULT 0,"
    "  snr_avg REAL,"
    "  snr_var REAL,"
    "  successes INTEGER NOT NULL DEFAULT 0,"
    "  attempts INTEGER NOT NULL DEFAULT 0,"
    "  PRIMARY KEY (src, dst, evidence)"
    ") WITHOUT ROWID;"
    "CREATE INDEX link_dst ON link(dst);"

    "CREATE TABLE remote_report("
    "  nodenum INTEGER NOT NULL,"
    "  rx_time INTEGER NOT NULL,"
    "  portnum INTEGER NOT NULL,"
    "  payload BLOB NOT NULL"
    ");"
    "CREATE INDEX remote_report_node ON remote_report(nodenum, rx_time);"

    "CREATE TABLE position("
    "  nodenum INTEGER NOT NULL,"
    "  rx_time INTEGER NOT NULL,"
    "  latitude_i INTEGER NOT NULL,"
    "  longitude_i INTEGER NOT NULL,"
    "  altitude INTEGER,"
    "  precision_bits INTEGER"
    ");"
    "CREATE INDEX position_node ON position(nodenum, rx_time);",
};

int MeshMapModule::schemaVersion()
{
    return sizeof(MIGRATIONS) / sizeof(MIGRATIONS[0]);
}

MeshMapModule::MeshMapModule(const std::string &dbPath)
{
    if (!db.open(dbPath) || !db.migrate(MIGRATIONS, schemaVersion())) {
        db.close();
        LOG_ERROR("MeshMap: disabled, database unavailable");
        return;
    }
    LOG_INFO("MeshMap: ready, schema v%d", schemaVersion());
}

#endif
