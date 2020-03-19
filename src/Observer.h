#pragma once

#include <Arduino.h>

#include <list>

class Observable;

class Observer
{
    Observable *observed;

  public:
    Observer() : observed(NULL) {}

    virtual ~Observer();

    void observe(Observable *o);

  private:
    friend class Observable;

    virtual void onNotify(Observable *o) = 0;
};

class Observable
{
    std::list<Observer *> observers;

  public:
    void notifyObservers()
    {
        for (std::list<Observer *>::const_iterator iterator = observers.begin(); iterator != observers.end(); ++iterator) {
            (*iterator)->onNotify(this);
        }
    }

    void addObserver(Observer *o) { observers.push_back(o); }

    void removeObserver(Observer *o) { observers.remove(o); }
};
