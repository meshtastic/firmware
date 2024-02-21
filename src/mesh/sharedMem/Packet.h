#pragma once

#include <memory>

/**
 * Polymorphic packets that can be moved into and out of packet queues.
 */
class Packet
{
  public:
    using PacketPtr = std::unique_ptr<Packet>;

    Packet(int packetId) : id(packetId) {}

    // virtual move constructor
    virtual PacketPtr move() { return PacketPtr(new Packet(std::move(*this))); }

    // Disable copying
    Packet(const Packet &) = delete;
    Packet &operator=(const Packet &) = delete;

    virtual ~Packet() {}

    int getPacketId() const { return id; }

  protected:
    // Enable moving
    Packet(Packet &&) = default;
    Packet &operator=(Packet &&) = default;

  private:
    int id;
};

/**
 * generic packet type class
 */
template <typename PacketType> class DataPacket : public Packet
{
  public:
    template <typename... Args> DataPacket(int id, Args &&...args) : Packet(id), data(new PacketType(std::forward<Args>(args)...))
    {
    }

    PacketPtr move() override { return PacketPtr(new DataPacket(std::move(*this))); }

    // Disable copying
    DataPacket(const DataPacket &) = delete;
    DataPacket &operator=(const DataPacket &) = delete;

    virtual ~DataPacket() {}

    const PacketType &getData() const { return *data; }

  protected:
    // Enable moving
    DataPacket(DataPacket &&) = default;
    DataPacket &operator=(DataPacket &&) = default;

  private:
    std::unique_ptr<PacketType> data;
};
