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

bool FishEyeStateRoutingModule::addNeighborInfo(meshtastic_NeighborInfo Ninfo){

}

uint32_t FishEyeStateRoutingModule::getNextHopForID(uint32_t dest){

}

bool FishEyeStateRoutingModule::handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_FishEyeStateRouting *lsp)
{
    
}

int32_t FishEyeStateRoutingModule::runOnce(){

}

bool FishEyeStateRoutingModule::calcNextHop(){

}

/*
Collect a received neighbor info packet from another node
Pass it to an upper client; do not persist this data on the mesh
*/
