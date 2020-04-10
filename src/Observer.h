#pragma once

#include <Arduino.h>

#include <list>

template <class T> class Observable;

/**
 * An observer which can be mixed in as a baseclass.  Implement onNotify as a method in your class.
 */
template <class T> class Observer
{
    Observable<T> *observed;

  public:
    Observer() : observed(NULL) {}

    virtual ~Observer();

    void observe(Observable<T> *o);

  private:
    friend class Observable<T>;

  protected:
    virtual void onNotify(T arg) = 0;
};

/**
 * An observer that calls an arbitrary method
 */
template <class Callback, class T> class CallbackObserver : public Observer<T>
{
    typedef void (Callback::*ObserverCallback)(T arg);

    Callback *objPtr;
    ObserverCallback method;

  public:
    CallbackObserver(Callback *_objPtr, ObserverCallback _method) : objPtr(_objPtr), method(_method) {}

  protected:
    virtual void onNotify(T arg) { (objPtr->*method)(arg); }
};

/**
 * An observable class that will notify observers anytime notifyObservers is called.  Argument type T can be any type, but for
 * performance reasons a pointer or word sized object is recommended.
 */
template <class T> class Observable
{
    std::list<Observer<T> *> observers;

  public:
    /**
     * Tell all observers about a change, observers can process arg as they wish
     */
    void notifyObservers(T arg)
    {
        for (typename std::list<Observer<T> *>::const_iterator iterator = observers.begin(); iterator != observers.end();
             ++iterator) {
            (*iterator)->onNotify(arg);
        }
    }

  private:
    friend class Observer<T>;

    // Not called directly, instead call observer.observe
    void addObserver(Observer<T> *o) { observers.push_back(o); }

    void removeObserver(Observer<T> *o) { observers.remove(o); }
};

template <class T> Observer<T>::~Observer()
{
    if (observed)
        observed->removeObserver(this);
    observed = NULL;
}

template <class T> void Observer<T>::observe(Observable<T> *o)
{
    o->addObserver(this);
}