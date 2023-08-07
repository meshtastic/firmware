#include "kbMatrixImpl.h"
#include "InputBroker.h"

#ifdef INPUTBROKER_MATRIX_TYPE

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

#endif // INPUTBROKER_MATRIX_TYPE