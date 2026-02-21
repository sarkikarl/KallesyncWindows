#include "chacha20_poly1305.hpp"
#include "key_manager.hpp"
#include <iostream>
#include "udpServer.h"
#include "udpClient.h"
#include "ChaChaHelpers.h"

// Stream 0 key is the only one saved - used for handshake/key exchange

const std::string KEYFILE = "client_keys.dat";

crypto::KeyStore keys;

// ===========================================================================
// NETWORK STUBS
// ===========================================================================

bool network_send(const std::vector<uint8_t>& data, udpClient& client) {
    udpSend(reinterpret_cast<const char*>(data.data()), data.size(), client);
    std::cout << "sent msg msg size(" << data.size() << ")\n";
    return true;
}

bool network_receive(std::vector<uint8_t>& out_data, udpServer& server) {
    while (!udpServerGet(server)) {
        std::cout << "waiting for server\n";
    }
    out_data.assign(server.buf, server.buf + server.bytesReceived);
    return true;
}

// ===========================================================================
// HANDSHAKE - generate and send stream keys to server
// ===========================================================================

bool send_handshake(const std::vector<uint32_t>& stream_ids, udpClient& client, udpServer& server) {
    // Generate random keys for each stream
    crypto::KeyStore export_keys;
    for (uint32_t id : stream_ids) {
        auto key = crypto::generate_key();
        keys.set_stream_key(id, key);
        export_keys.set_stream_key(id, key);
    }

    // Encrypt with stream 0 and send
    auto encrypted = export_keys.export_keys(stream_ids, keys.get_stream_key(0));
    auto msg = crypto::build_key_exchange_message(crypto::KeyExchangeMessageType::NEW_KEYS, encrypted);

    if (!network_send(msg, client)) {
        return false;
    }

    // Wait for confirmation
    std::vector<uint8_t> response;
    if (!network_receive(response, server)) {
        return false;
    }

    std::vector<uint8_t> payload;
    auto msg_type = crypto::parse_key_exchange_message(response, payload);
    return msg_type == crypto::KeyExchangeMessageType::KEYS_CONFIRMED;
}

// ===========================================================================
// STREAM SEND/RECEIVE
// ===========================================================================

bool stream_send(uint32_t stream_id, const std::vector<uint8_t>& data, udpClient& client) {
    auto encrypted = keys.encrypt_stream(stream_id, data);
    return network_send(encrypted, client);
}

bool stream_receive(uint32_t stream_id, std::vector<uint8_t>& out_data, udpServer& server) {
    std::vector<uint8_t> encrypted;
    if (!network_receive(encrypted, server)) {
        return false;
    }
    out_data = keys.decrypt_stream(stream_id, encrypted);
    return !out_data.empty();
}

// ===========================================================================
// KEY ROTATION - generate new keys and send to server
// ===========================================================================

bool initiate_key_rotation(const std::vector<uint32_t>& stream_ids, udpClient& client, udpServer& server) {
    std::map<uint32_t, std::vector<uint8_t>> new_keys;
    crypto::KeyStore export_keys;

    for (uint32_t id : stream_ids) {
        auto key = crypto::generate_key();
        new_keys[id] = key;
        export_keys.set_stream_key(id, key);
    }

    keys.set_pending(new_keys);

    auto encrypted = export_keys.export_keys(stream_ids, keys.get_stream_key(0));
    auto msg = crypto::build_key_exchange_message(crypto::KeyExchangeMessageType::NEW_KEYS, encrypted);

    if (!network_send(msg, client)) {
        return false;
    }

    std::vector<uint8_t> response;
    if (!network_receive(response, server)) {
        return false;
    }

    std::vector<uint8_t> payload;
    auto msg_type = crypto::parse_key_exchange_message(response, payload);

    if (msg_type != crypto::KeyExchangeMessageType::KEYS_CONFIRMED) {
        return false;
    }

    keys.commit_pending();
    return true;
}

// ===========================================================================
// USAGE EXAMPLE
// ===========================================================================

UdpPorts* chaStart(UdpPorts &main,std::string PASSWORD) {
    //crypto::KeyStore keys = main.keys;
    if (!keys.load(KEYFILE, PASSWORD)) {
        std::cerr << "Missing keyfile. Copy from server.\n";
        return nullptr;
    }

    // Handshake: generate and send keys for streams 1, 2, 3
    if (!send_handshake({ 1, 2, 3 }, main.main.Client, main.main.Server)) {
        return nullptr;
    }

    /*while (int i = 0 < 5) {
        std::vector<uint8_t> data = { 'H', 'e', 'l', 'l', 'o' };
        stream_send(main.clipboard.stream_id, data, main.clipboard.Client);
        stream_receive(main.clipboard.stream_id, data, main.clipboard.Server);
        std::cout << "Stream 1: " << std::string(data.begin(), data.end()) << "\n";
        stream_receive(main.clipboard.stream_id, data, main.KbmTele.Server);
        std::cout << "Stream 2: " << std::string(data.begin(), data.end()) << "\n";
        stream_send(main.clipboard.stream_id, data, main.KbmTele.Client);
        i++;
    }*/

    // Rotate individual streams as needed
    // initiate_key_rotation({1}, client1, server1);
    // initiate_key_rotation({2, 3}, client1, server1);
    return &main;
}