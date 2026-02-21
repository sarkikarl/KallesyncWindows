#include "chacha20_poly1305.hpp"
#include "key_manager.hpp"
#include <iostream>
#include "udpServer.h"
#include "udpClient.h"
inline std::atomic<bool> MainLocked{ false };//

struct UdpBiD {
    udpClient Client;
    udpServer Server;
    uint32_t stream_id;
};
struct UdpSS {
    udpServer Server;
    uint32_t stream_id;
};

struct UdpSC {
    udpClient Client;
    uint32_t stream_id;
};

struct UdpPorts {
    UdpBiD main;
    // Stream setup (flipped from server)
    UdpBiD clipboard; // = udpClientStart("192.168.1.69", 54055); //= udpServerStart("any", 54056, 1024);
    UdpBiD KbmTele; //= udpClientStart("192.168.1.69", 54057);//= udpServerStart("any", 54058, 1024);
    UdpSS Display;//one way
    UdpSS Audio;//oneway
};
extern crypto::KeyStore keys;

// ===========================================================================
// NETWORK STUBS
// ===========================================================================

bool network_receive(std::vector<uint8_t>& out_data, udpServer& server);

// ===========================================================================
// HANDSHAKE - generate and send stream keys to server
// ===========================================================================

bool send_handshake(const std::vector<uint32_t>& stream_ids, udpClient& client, udpServer& server);

// ===========================================================================
// STREAM SEND/RECEIVE
// ===========================================================================

bool stream_receive(uint32_t stream_id, std::vector<uint8_t>& out_data, udpServer& server);

// ===========================================================================
// KEY ROTATION - generate new keys and send to server
// ===========================================================================

bool initiate_key_rotation(const std::vector<uint32_t>& stream_ids, udpClient& client, udpServer& server);

// ===========================================================================
// USAGE EXAMPLE
// ===========================================================================

UdpPorts* chaStart(UdpPorts &main, std::string password);