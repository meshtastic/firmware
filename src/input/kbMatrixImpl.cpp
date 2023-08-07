#include "kbMatrixImpl.h"
#include "InputBroker.h"

KbMatrixImpl *kbMatrixImpl;

KbMatrixImpl::KbMatrixImpl() : KbMatrixBase("matrixKB") {}

void KbMatrixImpl::init()
{
    if (!INPUTBROKER_MATRIX_TYPE) {
        disable();
        return;
    }

    inputBroker->registerSource(this);
}