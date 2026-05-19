#pragma once

#include <type_traits>
#include <utility>

namespace stream_api
{
template <typename Client, typename = void> struct HasAvailableForWrite : std::false_type
{};

template <typename Client>
struct HasAvailableForWrite<Client, std::void_t<decltype(std::declval<Client &>().availableForWrite())>> : std::true_type
{};

template <typename Client> int availableForWriteOrUnknown(Client &client)
{
    if constexpr (HasAvailableForWrite<Client>::value)
        return client.availableForWrite();

    (void)client;
    return -1;
}

template <typename Client> bool clientReadyForWrite(Client &client)
{
    if (!client.connected())
        return false;

    int writable = availableForWriteOrUnknown(client);
    return writable == -1 || writable > 0;
}
} // namespace stream_api
