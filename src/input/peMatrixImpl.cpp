#include "peMatrixImpl.h"
#include "InputBroker.h"

PeMatrixImpl *peMatrixImpl;

PeMatrixImpl::PeMatrixImpl() : PeMatrixBase("matrixPE") {}

void PeMatrixImpl::init()
{
    if (kb_model != 0x12) {
        disable();
        return;
    }

    inputBroker->registerSource(this);
}
