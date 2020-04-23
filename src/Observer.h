#pragma once

#include <Arduino.h>
#include <assert.h>
#include <list>

template <class T> class Observable;

/**
 * An observer which can be mixed in as a baseclass.  Implement onNotify as a method in your class.
 */
template <class T> class Observer
{
    Observable<T> *observed = NULL;

  public:
    virtual ~Observer();

    void observe(Observable<T> *o);

  private:
    friend class Observable<T>;

  protected:
    /**
     * returns 0 if other observers should continue to be called
     * returns !0 if the observe calls should be aborted and this result code returned for notifyObservers
     **/
    virtual int onNotify(T arg) = 0;
};

/**
 * An observer that calls an arbitrary method
 */
template <class Callback, class T> class CallbackObserver : public Observer<T>
{
    typedef int (Callback::*ObserverCallback)(T arg);

    Callback *objPtr;
    ObserverCallback method;

  public:
    CallbackObserver(Callback *_objPtr, ObserverCallback _method) : objPtr(_objPtr), method(_method) {}

  protected:
    virtual int onNotify(T arg) { return (objPtr->*method)(arg); }
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
     *
     * returns !0 if an observer chose to abort processing by returning this code
     */
    int notifyObservers(T arg)
    {
        for (typename std::list<Observer<T> *>::const_iterator iterator = observers.begin(); iterator != observers.end();
             ++iterator) {
            int result = (*iterator)->onNotify(arg);
            if (result != 0)
                return result;
        }

        return 0;
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
    // We can only watch one thing at a time
    assert(!observed);

    observed = o;
    o->addObserver(this);
}