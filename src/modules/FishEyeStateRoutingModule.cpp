#include "FishEyeStateRoutingModule.h"
#include "Default.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "RTC.h"
#include <Throttle.h>

FishEyeStateRoutingModule *fishEyeStateRoutingModule;


/* 
 * check if Extension is enabled 
 */
FishEyeStateRoutingModule::FishEyeStateRoutingModule()
    : ProtobufModule("fishEyeStateRouting", meshtastic_PortNum_FISHEYESTATEROUTING_APP, &meshtastic_FishEyeStateRouting_msg),
      concurrency::OSThread("FishEyeStateRoutingModule")
{
  if(moduleConfig.fish_eye_state_routing.enabled && config.network.routingAlgorithm == meshtastic_Config_RoutingConfig_FishEyeState){
    setIntervalFromNow(Default::getConfiguredOrDefaultMs(moduleConfig.neighbor_info.update_interval,
      default_telemetry_broadcast_interval_secs));
  }else{
    LOG_DEBUG("FishEyeStateRouting Module is disabled");
    disable();
  }
}

/*
 * gets called from the NeighborInfo-Module if a new NeighborInfo Package arrives
 */
bool FishEyeStateRoutingModule::addNeighborInfo(meshtastic_NeighborInfo Ninfo){
  auto it = LSPDB.find(Ninfo.node_id);
  if(it != LSPDB.end()){ //Node already in LSPDB
    if(it->second.LSP.creation < Ninfo.creation){
      bool diff = false;
      for(int i = 0; i < min(Ninfo.neighbors_count,it->second.LSP.neighbors_count); i++){
        if((Ninfo.neighbors[i].node_id) != (it->second.LSP.neighbors[i].node_id)){
          diff = true;
          break;
        }
      }
      if(diff || (it->second.LSP.traveledHops != 1) || (it->second.LSP.neighbors_count != Ninfo.neighbors_count)){
        LSPDBEntry entry;
        NinfoToLSPDBEntry(&Ninfo,&entry);
        if(it->second.forwarded == false){
          entry.timeout = min(it->second.timeout,entry.timeout);
        }
        entry.nextHop = it->second.nextHop;
        it->second = entry;
        calcNextHop();
        return 1;
      }else{
        it->second.forwarded = false;
        it->second.timeout = getTime() + moduleConfig.neighbor_info.update_interval * std::pow(it->second.LSP.traveledHops, alpha);
      }
    }
  }else{ //Node not in LSPDB
    LSPDBEntry entry; //new entry
    NinfoToLSPDBEntry(&Ninfo,&entry);
    LSPDB.insert(std::make_pair(entry.LSP.node_id,entry)); //insert into DB
    calcNextHop(); //calculate shortest Path
    return 1;
  }
  return 0;
}

void FishEyeStateRoutingModule::NinfoToLSPDBEntry(meshtastic_NeighborInfo *Ninfo, LSPDBEntry *fsr){
  fsr->nextHop = NODENUM_BROADCAST;
  fsr->LSP.node_id = Ninfo->node_id;
  fsr->LSP.traveledHops = 1;
  fsr->LSP.neighbors_count = Ninfo->neighbors_count;
  for(int i = 0; i < Ninfo->neighbors_count; i++){
    fsr->LSP.neighbors[i] = Ninfo->neighbors[i];
  }
  fsr->LSP.creation = Ninfo->creation;
  fsr->timeout = getTime() + moduleConfig.neighbor_info.update_interval * std::pow(fsr->LSP.traveledHops, alpha);
  fsr->forwarded = false;
}

/*
 * returns next-Hop for a Message to a given NodeID
 */
uint32_t FishEyeStateRoutingModule::getNextHopForID(uint32_t dest){
  auto it = LSPDB.find(dest);
  if(it == LSPDB.end()){
    return NODENUM_BROADCAST;
  }else{
    return it->second.nextHop;
  }
}

/*
 * handels incomming LSP-Packages
 */
bool FishEyeStateRoutingModule::handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_FishEyeStateRouting *lsp)
{

}

/*
 * 
 */
int32_t FishEyeStateRoutingModule::runOnce(){

}

/*
 * Calculates Distances and Next-Hops
 */
bool FishEyeStateRoutingModule::calcNextHop(){
  //sofortigen run once Check calen

}