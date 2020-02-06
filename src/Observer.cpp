#include "Observer.h"

Observer::~Observer()
{
    if (observed)
        observed->removeObserver(this);
    observed = NULL;
}

void Observer::observe(Observable *o)
{
    o->addObserver(this);
}