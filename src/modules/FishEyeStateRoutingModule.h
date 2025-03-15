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
     * Get Next-Hop for Package to dest
     */
    uint32_t getNextHopForID(uint32_t dest);

    /*
     * To set own Neighborhood
     */
    bool setOwnNeighborhood(meshtastic_NeighborInfo Ninfo);
    bool setOwnNeighborhood(std::vector<meshtastic_Neighbor> n);

    

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

    float alpha = 1.4; // Factor that determines the strenght of blurring towards outer nodes 
    uint32_t nextLSPPckg = 0;
    std::vector<meshtastic_Neighbor> neighborhood; // own Neighborhood as Basis for NextHop Calculation
    std::unordered_map<uint32_t,uint32_t> NextHopTable; // saves for every reachable known Node the optimal (SSSP) NextHop

    struct LSPDBEntry{  // Structure which describes the Structure of an Entry of the LSP-Database
      uint32_t timeout;
      bool forwarded;
      meshtastic_FishEyeStateRouting LSP;
    };

    /*
     * Comparing two LSP-Structs
     */
    bool isequal(const meshtastic_FishEyeStateRouting &s1, const meshtastic_FishEyeStateRouting &s2);

    /*
     * Database for the received LSP-Packages and their Next-Hop
     */
    std::unordered_map<uint32_t,LSPDBEntry> LSPDB;

    /*
     * Calculate nextHop and distance from this node all other nodes an updates Next-Hop tabel
     */
    bool calcNextHop();

    /*
     * Send own NeighborInfo to 0-Hop Neighborhood
     */
    void sendInitialLSP();

    /*
     * prints the Contents of the LSP-Database
     */
    void printLSPDB();

};
extern FishEyeStateRoutingModule *fishEyeStateRoutingModule;