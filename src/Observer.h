#pragma once

#include <Arduino.h>
#include <algorithm>
#include <list>

template <class T> class Observable;

/**
 * An observer which can be mixed in as a baseclass.  Implement onNotify as a method in your class.
 */
template <class T> class Observer
{
    std::list<Observable<T> *> observables;

  public:
    virtual ~Observer();

    /// Stop watching the observable
    void unobserve(Observable<T> *o);

    /// Start watching a specified observable
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
    virtual int onNotify(T arg) override { return (objPtr->*method)(arg); }
};

/**
 * An observable class that will notify observers anytime notifyObservers is called.  Argument type T can be any type, but for
 * performance reasons a pointer or word sized object is recommended.
 */
template <class T> class Observable
{
    std::list<Observer<T> *> observers;

  public:
    ~Observable();

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

    // cppcheck-suppress constParameterPointer ; std::list::remove() needs a non-const pointer
    void removeObserver(Observer<T> *o) { observers.remove(o); }
};

template <class T> Observer<T>::~Observer()
{
    for (typename std::list<Observable<T> *>::const_iterator iterator = observables.begin(); iterator != observables.end();
         ++iterator) {
        (*iterator)->removeObserver(this);
    }
    observables.clear();
}

template <class T> void Observer<T>::unobserve(Observable<T> *o)
{
    // Membership before any read through `o`: ~Observable has already dropped a destroyed
    // observable from this list, so an unobserve() that runs after it is a no-op, not a use-after-free.
    if (std::find(observables.begin(), observables.end(), o) == observables.end())
        return;

    o->removeObserver(this);
    observables.remove(o);
}

template <class T> void Observer<T>::observe(Observable<T> *o)
{
    observables.push_back(o);
    o->addObserver(this);
}

// Symmetric to ~Observer: an observable destroyed while it still has watchers must drop itself from
// their lists, or their later unobserve() / ~Observer reads through the freed object.
template <class T> Observable<T>::~Observable()
{
    for (typename std::list<Observer<T> *>::const_iterator iterator = observers.begin(); iterator != observers.end();
         ++iterator) {
        (*iterator)->observables.remove(this);
    }
    observers.clear();
}
