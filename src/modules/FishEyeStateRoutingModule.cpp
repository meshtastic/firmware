#include "FishEyeStateRoutingModule.h"
#include "Default.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "RTC.h"
#include <Throttle.h>
#include <queue>
#include <set>

FishEyeStateRoutingModule *fishEyeStateRoutingModule;


/* 
 * check if Extension is enabled 
 */
FishEyeStateRoutingModule::FishEyeStateRoutingModule()
    : ProtobufModule("fishEyeStateRouting", meshtastic_PortNum_FISHEYESTATEROUTING_APP, &meshtastic_FishEyeStateRouting_msg),
      concurrency::OSThread("FishEyeStateRoutingModule")
{
  if(moduleConfig.fish_eye_state_routing.enabled && config.network.routingAlgorithm == meshtastic_Config_RoutingConfig_FishEyeState && moduleConfig.has_neighbor_info == true && moduleConfig.neighbor_info.enabled == true){
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

    if(it->second.LSP.creation < Ninfo.creation){ // Package newer that our Version?
      bool diff = false;  // check if it's any different
      for(int i = 0; i < min(Ninfo.neighbors_count,it->second.LSP.neighbors_count); i++){
        if((Ninfo.neighbors[i].node_id) != (it->second.LSP.neighbors[i].node_id)){
          diff = true;
          break;
        }
      }
      diff = diff || (it->second.LSP.traveledHops != 1) || (it->second.LSP.neighbors_count != Ninfo.neighbors_count);

      LSPDBEntry entry;   //Copy it into Database
      NinfoToLSPDBEntry(&Ninfo,&entry);
      if(it->second.forwarded == false){
        entry.timeout = min(it->second.timeout,entry.timeout);
      }
      it->second = entry;

      if(diff && moduleConfig.fish_eye_state_routing.enabled){
        calcNextHop();
        return 1;
      }
    }
    
  }else{                  //Node not in LSPDB
    LSPDBEntry entry; //new entry
    NinfoToLSPDBEntry(&Ninfo,&entry);
    LSPDB.insert(std::make_pair(entry.LSP.node_id,entry)); //insert into DB
    if(moduleConfig.fish_eye_state_routing.enabled){
    calcNextHop();
    }
    return 1;
  }
  return 0;
}

/*
 * convert a meshtastic_NeighborInfo Struct into an LSPDBEntry Struct
 */
void FishEyeStateRoutingModule::NinfoToLSPDBEntry(meshtastic_NeighborInfo *Ninfo, LSPDBEntry *fsr){
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

/*
 * Compare two meshtastic_FishEyeStateRouting Structs
 * Criteria: Neighbor cout, Node ID, neighbors list (only by ID)
 */
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
 * returns next-Hop for a Message to a given NodeID, if Node is unknwon BroadcastID is returned
 */
uint32_t FishEyeStateRoutingModule::getNextHopForID(uint32_t dest){
  if (dest == nodeDB->getNodeNum()){return dest;}
  auto it = NextHopTable.find(dest);
  if(it == NextHopTable.end()){
    return NODENUM_BROADCAST;
  }else{
    return it->second;
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
      if(it->second.forwarded){ //calculate Timeout
        it->second.forwarded = false;
        it->second.timeout = (uint32_t) (getTime() + moduleConfig.neighbor_info.update_interval * pow(it->second.LSP.traveledHops,alpha));
      }else{
        it->second.timeout = min(it->second.timeout, (uint32_t) (getTime() + moduleConfig.neighbor_info.update_interval * pow(it->second.LSP.traveledHops,alpha)));
      }
      if(!diff && moduleConfig.fish_eye_state_routing.enabled){calcNextHop();}
    }

  }else{                  //Node not in LSPDB

    LSPDBEntry entry;     //create a new LSPDB entry
    entry.forwarded = false;
    entry.LSP = *lsp;
    entry.LSP.traveledHops += 1;
    entry.timeout = (uint32_t) (getTime() + moduleConfig.neighbor_info.update_interval * pow(entry.LSP.traveledHops,alpha));
    LSPDB.insert(std::make_pair(entry.LSP.node_id,entry));
    if(moduleConfig.fish_eye_state_routing.enabled){calcNextHop();}
  }
  return true;
}

/*
 * broadcast all Packages, that weren't broadcastet and whose timeout is expired
 */
int32_t FishEyeStateRoutingModule::runOnce(){
  auto it = LSPDB.begin();
  uint32_t min = UINT32_MAX;
  while (it != LSPDB.end()) //iterate over every Entry
  {

    if((getTime() > it->second.timeout) && (!it->second.forwarded )){ //Timeout expired?
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

bool FishEyeStateRoutingModule::setOwnNeighborhood(meshtastic_NeighborInfo Ninfo){
  bool diff = false;
  for(int i = 0; i<min((uint32_t) Ninfo.neighbors_count,(uint32_t) neighborhood.size()); i++){
    if(neighborhood[i].node_id != Ninfo.neighbors[i].node_id){diff = true;}
  }
  if(Ninfo.neighbors_count != neighborhood.size()){diff = true;}
  neighborhood.clear();
  for(int i = 0; i< Ninfo.neighbors_count; i++){
    neighborhood.push_back(Ninfo.neighbors[i]);
  }
  if(diff && moduleConfig.fish_eye_state_routing.enabled){
    calcNextHop();
  }
  return diff;
}

bool FishEyeStateRoutingModule::setOwnNeighborhood(std::vector<meshtastic_Neighbor> n){
  bool diff = false;
  for(int i = 0; i<min((uint32_t) n.size(), (uint32_t) neighborhood.size()); i++){
    if(neighborhood[i].node_id != n[i].node_id){diff = true;}
  }
  if(n.size() != neighborhood.size()){diff = true;}
  neighborhood.clear();
  for(int i = 0; i< n.size(); i++){
    neighborhood.push_back(n[i]);
  }
  if(diff && moduleConfig.fish_eye_state_routing.enabled){
    calcNextHop();
  }
  return diff;
}


/*
 * Calculates and Next-Hops
 */
bool FishEyeStateRoutingModule::calcNextHop(){
  struct nodeIDwithPrev{
    uint32_t nodeID;
    uint32_t prev;
  };

  uint32_t ownID = (uint32_t) nodeDB->getNodeNum();
  std::queue<nodeIDwithPrev> waitingqueue;  //unprocessed Nodes
  std::set<uint32_t> alreadyProcessed;
  for(auto nbr : neighborhood){   //add our neighborhood
    nodeIDwithPrev entry;
    entry.nodeID = nbr.node_id;
    entry.prev = ownID;
    waitingqueue.push(entry);
  }
  alreadyProcessed.insert(ownID);

  while(waitingqueue.size() != 0){

    nodeIDwithPrev n = waitingqueue.front();
    uint32_t nextHopForN;       //calculate NextHop
    if(n.prev == ownID){
      nextHopForN = n.nodeID;
    }else{
      auto it = NextHopTable.find(n.prev);
      if(it == NextHopTable.end()){
        nextHopForN = NODENUM_BROADCAST;
      }else{
        nextHopForN = it->second;
      }
    }
    alreadyProcessed.insert(n.nodeID); //Node we just Handeled ist now Porcessed

    if(NextHopTable.find(n.nodeID) == NextHopTable.end()){      //insert NextHop in Storage
      NextHopTable.insert(std::make_pair(n.nodeID,nextHopForN));
    }else{
      NextHopTable.find(n.nodeID)->second = nextHopForN;
    }

    auto it = LSPDB.find(n.nodeID);    //discover new Nodes and push them in waitingqueue, if we haven't processed them yet (e.g. they aren't in the alreadyProcessed-Set)
    if(it != LSPDB.end()){
      for(int i = 0; i < (it->second.LSP.neighbors_count); i++){
        if(alreadyProcessed.find(it->second.LSP.neighbors[i].node_id) == alreadyProcessed.end()){
          nodeIDwithPrev entry;
          entry.nodeID = it->second.LSP.neighbors[i].node_id;
          entry.prev = n.nodeID;
          waitingqueue.push(entry);
        }
      }
    }
    waitingqueue.pop();

  }
  return true;
}