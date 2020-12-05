#pragma once
#include "MeshPlugin.h"

/**
 * Most plugins are only interested in sending/receving one particular portnum.  This baseclass simplifies that common
 * case.
 */
class SinglePortPlugin : public MeshPlugin
{
  protected:
    PortNum ourPortNum;

  public:
    /** Constructor
     * name is for debugging output
     */
    SinglePortPlugin(const char *_name, PortNum _ourPortNum) : MeshPlugin(_name), ourPortNum(_ourPortNum) {}

  protected:
    /**
     * @return true if you want to receive the specified portnum
     */
    virtual bool wantPortnum(PortNum p) { return p == ourPortNum; }
};
