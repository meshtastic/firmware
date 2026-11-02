#pragma once

/**
 * Thin compatibility layer over the sdbus-c++ 1.x and 2.x APIs, so
 * LinuxBluetooth.cpp can be written once against a single registration/proxy
 * syntax.
 *
 * The 2.x major release (Debian trixie, Fedora, current Raspberry Pi OS)
 * replaced the 1.x fluent object-registration API
 * (registerMethod().onInterface().implementedAs() + finishRegistration()) with
 * vtable items (addVTable(items...).forInterface()), and made
 * bus/interface/member names strong types. 1.x (Debian bookworm ships 1.2,
 * Ubuntu 24.04 ships 1.4) predates both. MESHTASTIC_SDBUS_CPP_V2 is set from
 * pkg-config in variants/native/portduino.ini.
 *
 * Only what LinuxBluetooth.cpp actually uses is shimmed.
 */

#include <sdbus-c++/sdbus-c++.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace sdbuscompat
{

#ifdef MESHTASTIC_SDBUS_CPP_V2

// Error argument of an async method-reply callback (2.x passes an optional, 1.x
// a pointer).
using AsyncError = std::optional<sdbus::Error>;
inline bool asyncFailed(const AsyncError &error)
{
    return error.has_value();
}
inline std::string asyncErrorMessage(const AsyncError &error)
{
    return error ? error->getMessage() : std::string();
}

template <typename F> auto method(const char *name, F &&callback)
{
    return sdbus::registerMethod(std::string(name)).implementedAs(std::forward<F>(callback));
}

template <typename F> auto property(const char *name, F &&getter)
{
    return sdbus::registerProperty(std::string(name)).withGetter(std::forward<F>(getter));
}

template <typename... Items> void addVTable(sdbus::IObject &object, const char *interfaceName, Items &&...items)
{
    object.addVTable(std::forward<Items>(items)...).forInterface(sdbus::InterfaceName{interfaceName});
}

inline std::unique_ptr<sdbus::IProxy> makeProxy(sdbus::IConnection &connection, const char *destination, const std::string &path)
{
    return sdbus::createProxy(connection, sdbus::ServiceName{destination}, sdbus::ObjectPath{path});
}

inline sdbus::Error dbusError(const char *name, const char *message)
{
    return sdbus::Error(sdbus::Error::Name{name}, message);
}

inline void emitPropertiesChanged(sdbus::IObject &object, const char *interfaceName, const char *propertyName)
{
    object.emitPropertiesChangedSignal(interfaceName, {sdbus::PropertyName{propertyName}});
}

#else // sdbus-c++ 1.x

using AsyncError = const sdbus::Error *;
inline bool asyncFailed(AsyncError error)
{
    return error != nullptr;
}
inline std::string asyncErrorMessage(AsyncError error)
{
    return error ? error->getMessage() : std::string();
}

// 1.x has no vtable-item concept; emulate one with type-erased "apply this
// registration to that object" closures so call sites read the same as the 2.x
// path.
struct VTableItem {
    std::function<void(sdbus::IObject &, const std::string &)> apply;
};

template <typename F> VTableItem method(const char *name, F &&callback)
{
    return {[name = std::string(name), cb = std::forward<F>(callback)](sdbus::IObject &object, const std::string &iface) mutable {
        object.registerMethod(name).onInterface(iface).implementedAs(std::move(cb));
    }};
}

template <typename F> VTableItem property(const char *name, F &&getter)
{
    return {[name = std::string(name), g = std::forward<F>(getter)](sdbus::IObject &object, const std::string &iface) mutable {
        object.registerProperty(name).onInterface(iface).withGetter(std::move(g));
    }};
}

template <typename... Items> void addVTable(sdbus::IObject &object, const char *interfaceName, Items &&...items)
{
    (items.apply(object, interfaceName), ...);
    object.finishRegistration();
}

inline std::unique_ptr<sdbus::IProxy> makeProxy(sdbus::IConnection &connection, const char *destination, const std::string &path)
{
    return sdbus::createProxy(connection, destination, path);
}

inline sdbus::Error dbusError(const char *name, const char *message)
{
    return sdbus::Error(name, message);
}

inline void emitPropertiesChanged(sdbus::IObject &object, const char *interfaceName, const char *propertyName)
{
    object.emitPropertiesChangedSignal(interfaceName, {std::string(propertyName)});
}

#endif

inline std::unique_ptr<sdbus::IObject> makeObject(sdbus::IConnection &connection, const std::string &path)
{
    // sdbus::ObjectPath derives from std::string in both major versions, so this
    // one needs no #if: 1.x takes std::string, 2.x takes ObjectPath.
    return sdbus::createObject(connection, sdbus::ObjectPath{path});
}

inline void finishProxy(sdbus::IProxy &proxy)
{
#ifndef MESHTASTIC_SDBUS_CPP_V2
    // 1.x defers uponSignal() subscriptions until finishRegistration(); 2.x
    // subscribes immediately and has no such method.
    proxy.finishRegistration();
#else
    (void)proxy;
#endif
}

} // namespace sdbuscompat
