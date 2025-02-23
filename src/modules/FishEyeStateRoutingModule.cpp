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
  if(it != LSPDB.end()){    //Node already in LSPDB

    if(it->second.LSP.creation < Ninfo.creation){
      bool diff = false;
      for(int i = 0; i < min(Ninfo.neighbors_count,it->second.LSP.neighbors_count); i++){
        if((Ninfo.neighbors[i].node_id) != (it->second.LSP.neighbors[i].node_id)){
          diff = true;
          break;
        }
      }
      diff = diff || (it->second.LSP.traveledHops != 1) || (it->second.LSP.neighbors_count != Ninfo.neighbors_count);
      LSPDBEntry entry;
      NinfoToLSPDBEntry(&Ninfo,&entry);
      if(it->second.forwarded == false){
        entry.timeout = min(it->second.timeout,entry.timeout);
      }
      entry.nextHop = it->second.nextHop;
      it->second = entry;

      if(diff){
        calcNextHop();
        return 1;
      }
    }
  }else{                  //Node not in LSPDB
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
  fsr->timeout = (uint32_t) (getTime() + moduleConfig.neighbor_info.update_interval * std::pow(fsr->LSP.traveledHops, alpha));
  fsr->forwarded = false;
}

bool FishEyeStateRoutingModule::isequal(const meshtastic_FishEyeStateRouting &s1, const meshtastic_FishEyeStateRouting &s2){
  if((s1.neighbors_count == s2.neighbors_count) && (s1.node_id == s2.node_id)){
    bool diff = false;
    for(int i = 0; i < s1.neighbors_count; i++){
      if(s1.neighbors[i].node_id != s2.neighbors[i].node_id){
        diff = true;
        break;
      }
    }
    return !diff;
  }
  return false;
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
  auto it = LSPDB.find(lsp->node_id);
  if(it != LSPDB.end()){    //Node already in LSPDB
    if(it->second.LSP.creation < lsp->creation){
      bool diff = isequal(it->second.LSP,*lsp);
      it->second.LSP = *lsp;
      it->second.LSP.traveledHops += 1;
      if(it->second.forwarded){
        it->second.forwarded = false;
        it->second.timeout = (uint32_t) (getTime() + moduleConfig.neighbor_info.update_interval * pow(it->second.LSP.traveledHops,alpha));
      }else{
        it->second.timeout = min(it->second.timeout, (uint32_t) (getTime() + moduleConfig.neighbor_info.update_interval * pow(it->second.LSP.traveledHops,alpha)));
      }
      if(diff){calcNextHop();}
    }
  }else{                  //Node not in LSPDB
    LSPDBEntry entry;
    entry.forwarded = false;
    entry.LSP = *lsp;
    entry.LSP.traveledHops += 1;
    entry.nextHop = NODENUM_BROADCAST;
    entry.timeout = (uint32_t) (getTime() + moduleConfig.neighbor_info.update_interval * pow(entry.LSP.traveledHops,alpha));
    LSPDB.insert(std::make_pair(entry.LSP.node_id,entry));
    calcNextHop();
  }
  return true;
}

/*
 * broadcast all Packages, that weren't broadcastet and whose timeout is expired
 */
int32_t FishEyeStateRoutingModule::runOnce(){
  auto it = LSPDB.begin();
  uint32_t min = UINT32_MAX;
  while (it != LSPDB.end())
  {
    if((getTime() > it->second.timeout) && (!it->second.forwarded )){
      meshtastic_MeshPacket *p = allocDataProtobuf(it->second.LSP);
      p->to = NODENUM_BROADCAST;
      p->decoded.want_response = false;
      p->priority = meshtastic_MeshPacket_Priority_BACKGROUND;
      service->sendToMesh(p,RX_SRC_LOCAL,true);
      it->second.forwarded = true;
    }else if((getTime() < it->second.timeout) && (!it->second.forwarded) && (it->second.timeout < min)){
      min = it->second.timeout;
    }
    ++it;
  }
  return min;
}

/*
 * Calculates Distances and Next-Hops
 */
bool FishEyeStateRoutingModule::calcNextHop(){
  //TODO: runOnce-Intervall aufs neue Minimum setzen!!

}