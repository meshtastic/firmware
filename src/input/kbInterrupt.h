#pragma once

#include "Observer.h"

class KbInterruptObservable : public Observable<KbInterruptObservable *>
{
};

class KbInterruptObserver : public Observer<KbInterruptObservable *>
{
};