#pragma once

#include "Observer.h"

class KbInterruptObservable : public Observable<bool>
{
};

class KbInterruptObserver : public Observer<bool>
{
};