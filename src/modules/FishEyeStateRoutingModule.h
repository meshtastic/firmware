#pragma once
#include "ProtobufModule.h"
#include <unordered_map>
#include <functional>

/*
 * FishEyeStateRouting Module, for Routing in the Mesh
 */
class FishEyeStateRoutingModule : public ProtobufModule<meshtastic_FishEyeStateRouting>, private concurrency::OSThread
{
  public:
    /*
     * Expose the constructor
     */
    FishEyeStateRoutingModule();

    /*
     * Get Information of direct Neighbors from the NeighborInfoModule and process it
     */
    bool addNeighborInfo(meshtastic_NeighborInfo Ninfo);

    /*
     * Get Next-Hop for Package to dest
     */
    uint32_t getNextHopForID(uint32_t dest);


  protected:
    /*
     * Called to handle an incomming LSP-Package, adds it to it's collection 
     * and calculates an offset for it
     */
    virtual bool handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_FishEyeStateRouting *lsp) override;

    /*
     * Check whether we have to forward an LSP-Package an does so if necessary
     */
    int32_t runOnce() override;

  private:

    float alpha = 2;

    struct LSPDBEntry{
      uint32_t timeout;
      bool forwarded;
      uint32_t nextHop;
      meshtastic_FishEyeStateRouting LSP;
    };

    /*
     * Database for the received LSP-Packages and their Next-Hop
     */
    std::unordered_map<uint32_t,LSPDBEntry> LSPDB;

    /*
     * Calculate nextHop and distance from this node all other nodes an updates Next-Hop tabel
     */
    bool calcNextHop();

    /*
     * converts NeighborInfo Struct into LSPDBEntry-Struct
     */
    void NinfoToLSPDBEntry(meshtastic_NeighborInfo *Ninfo, LSPDBEntry *fsr);

};
extern FishEyeStateRoutingModule *fishEyeStateRoutingModule;