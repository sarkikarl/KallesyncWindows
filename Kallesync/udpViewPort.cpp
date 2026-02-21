#include "config.h"
#include "threadStructures.h"
#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#include <vector>
#include <thread>
#include <mutex>
#include <string>
#include<chrono>
#include <turbojpeg.h>
#include <cstdint>
#include "viewport.h"
#include <cstring>
#include <random>
#include <fstream>

#pragma comment(lib, "turbojpeg.lib")  // or "turbojpeg-static.lib" depending on your setup
static HANDLE g_iocp = NULL;
static SOCKET g_sock = INVALID_SOCKET;

// Global state and lock
void renderFrame(const std::vector<uint8_t>& rawFrame);

struct FrameState {
    std::vector<unsigned char> data;
    std::vector<bool> chunk_received;
    int width = 1920;
    int height = 1080;
    int pixel_format = TJPF_RGBA;
    uint32_t total_chunks = 0;
    uint32_t received_chunks = 0;
    bool metadata_received = false;
};

std::vector<FrameState> Frames(256);

std::chrono::high_resolution_clock::time_point g_last_call;
bool g_first_call = true;


std::vector<uint8_t> decompress_frame(const FrameState& compressed) {
    auto now = std::chrono::high_resolution_clock::now();

    if (!g_first_call) {
        long long us = std::chrono::duration_cast<std::chrono::microseconds>(now - g_last_call).count();
        std::cout << "frameTime: " << us << " MicroSeconds\n";
    }
    else {
        g_first_call = false;
    }
    g_last_call = now;


    std::vector<unsigned char> raw_data;



    if (compressed.data.size() >= 3) {
        std::cout << "JPEG header: "
            << std::hex
            << (int)(unsigned char)compressed.data[0] << " "
            << (int)(unsigned char)compressed.data[1] << " "
            << (int)(unsigned char)compressed.data[2]
            << std::dec
            << " size=" << compressed.data.size() << std::endl;
    }




    tjhandle decompressor = tjInitDecompress();
    if (!decompressor) {
        return raw_data;
    }

    // Calculate output size
    int bytes_per_pixel = tjPixelSize[compressed.pixel_format];
    int raw_size = compressed.width * compressed.height * bytes_per_pixel;
    raw_data.resize(raw_size);

    int ret = tjDecompress2(decompressor,
        compressed.data.data(),
        compressed.data.size(),
        raw_data.data(),
        compressed.width,
        0,  // pitch
        compressed.height,
        TJPF_BGRX,
        TJFLAG_FASTDCT);

    tjDestroy(decompressor);

    if (ret != 0) {
        raw_data.clear();  // Clear on error
    }


    return raw_data;
}


#pragma comment(lib, "ws2_32.lib")

#include "packet_pipeline.h"


using namespace PacketPipeline;

// Create global pipeline instance
RingBuffer<8192> packet_ring;
IOCPAdapter iocp_adapter(&packet_ring);

int framecount = 0;


void resetFrame(uint32_t current_iteration) {
    Frames[current_iteration].data.clear();
    Frames[current_iteration].chunk_received.clear();
    Frames[current_iteration].total_chunks = 0;
    Frames[current_iteration].received_chunks = 0;
    Frames[current_iteration].metadata_received = false;
}



void process_packet(const Packet& p) {
    if (p.size < 1) return;
    uint8_t packet_type = static_cast<uint8_t>(p.data[0]);

    uint32_t current_iteration, chunk_index, total_chunks;
    memcpy(&current_iteration, p.data.data() + 1, 4);
    memcpy(&chunk_index, p.data.data() + 5, 4);
    memcpy(&total_chunks, p.data.data() + 9, 4);

    // Validate inputs
    if (current_iteration >= Frames.size()) {
        std::cerr << "Invalid current_iteration: " << current_iteration << std::endl;
        return;
    }

    FrameState& frame = Frames[current_iteration];



    //std::cout << "Packet type: " << (int)packet_type << std::endl;
    if (packet_type == 1 && p.size == 17) {
        uint32_t current_iteration;
        int width, height, pixel_format;
        memcpy(&current_iteration, p.data.data() + 1, 4);
        memcpy(&width, p.data.data() + 5, 4);
        memcpy(&height, p.data.data() + 9, 4);
        memcpy(&pixel_format, p.data.data() + 13, 4);
        std::cout << "Metadata: " << width << "x" << height
            << " pixel_format=" << pixel_format << std::endl;
        Frames[current_iteration].width = width;
        Frames[current_iteration].height = height;
        Frames[current_iteration].pixel_format = pixel_format;
        Frames[current_iteration].metadata_received = true;

        if (Frames[current_iteration].received_chunks == Frames[current_iteration].total_chunks
            && Frames[current_iteration].total_chunks > 0) {
            std::cout << "Rendering frame " << current_iteration << std::endl;
            //decrypt_packets(decryptor, Frames[current_iteration].data.data(), Frames[current_iteration].data.size(), 1);
            auto decompressed = decompress_frame(Frames[current_iteration]);
            std::cout << "Decompressed size: " << decompressed.size() << std::endl;
            if (!decompressed.empty()) {
                renderFrame(decompressed);
            }
            resetFrame(current_iteration);//reset frame since there is no more data for said frame and it will be reused
        }
    }
    else if (packet_type == 0 && p.size > 13) {
        // First chunk for this frame - initialize
        if (frame.total_chunks == 0) {
            frame.total_chunks = total_chunks;
            frame.data.resize(total_chunks * 1400);  // Max possible size
            frame.chunk_received.resize(total_chunks, false);
        }

        // Additional safety checks
        if (chunk_index >= frame.chunk_received.size()) {
            std::cerr << "Invalid chunk_index: " << chunk_index
                << " (max: " << frame.chunk_received.size() - 1 << ")" << std::endl;
            return;
        }
        uint32_t current_iteration, chunk_index, total_chunks;
        memcpy(&current_iteration, p.data.data() + 1, 4);
        memcpy(&chunk_index, p.data.data() + 5, 4);
        memcpy(&total_chunks, p.data.data() + 9, 4);

        FrameState& frame = Frames[current_iteration];

        // First chunk for this frame - initialize
        if (frame.total_chunks == 0) {
            frame.total_chunks = total_chunks;
            frame.data.resize(total_chunks * 1400);  // Max possible size
            frame.chunk_received.resize(total_chunks, false);
        }

        // Skip duplicate chunks
        if (frame.chunk_received[chunk_index]) {
            return;
        }

        // Copy chunk to correct position
        size_t chunk_size = p.size - 13;
        // ADD THIS
        if (chunk_index < 3 || chunk_index == total_chunks - 1) {
            std::cout << "Chunk " << chunk_index << "/" << total_chunks
                << " size=" << chunk_size << std::endl;
        }

        size_t offset = chunk_index * 1400;
        memcpy(frame.data.data() + offset, p.data.data() + 13, chunk_size);

        frame.chunk_received[chunk_index] = true;
        frame.received_chunks++;

        // Last chunk might be smaller - track actual size
        if (chunk_index == total_chunks - 1) {
            frame.data.resize(offset + chunk_size);
        }

        if (frame.received_chunks == total_chunks && frame.metadata_received) {
            std::cout << "Rendering frame " << current_iteration << std::endl;
            auto decompressed = decompress_frame(frame);
            std::cout << "Decompressed size: " << decompressed.size() << std::endl;
            if (!decompressed.empty()) {
                renderFrame(decompressed);

            }
            resetFrame(current_iteration);
        }
    }
}


void ConsumePackets(int max_packets = 64) {
    Packet p;
    int processed = 0;
    while (processed < max_packets && packet_ring.try_pop(p)) {

        process_packet(p);
        processed++;
    }
}


using namespace std;

using namespace chrono;
steady_clock::time_point last_small_packet_time;
steady_clock::time_point lastframeEnd;
mutex time_mutex;
bool first_small_packet = true;

/*/ Define CONTAINING_RECORD if not already defined (from Windows headers)
#define CONTAINING_RECORD(address, type, field) ((type *)( \
    (char*)(address) - \
    (ULONG_PTR)(&((type *)0)->field)))*/

    // Per-IO data structure
struct PerIOData {
    OVERLAPPED overlapped;
    WSABUF wsabuf;
    char buffer[1413];
    sockaddr_in clientAddr;
    int addrLen;
};

// Globals
const int MAX_PENDING = 256; // Can adjust as needed
mutex cout_mutex; // For synchronizing cout in threads

bool winsockStart() {
    WSADATA data;
    WORD version = MAKEWORD(2, 2);
    int wsOK = WSAStartup(version, &data);
    if (wsOK != 0) {
        cout << "Can't start Winsock: " << wsOK << endl;
        return false;
    }
    std::cout << "winsock started in veiwport\n";
    return true;
}

int UdpVeiwPort() {
    if (!winsockStart()) {
        return 1;
    }
    cout << "test 1 \n";
    /*/std::thread consumer_thread([]() {
        // Create consumer with function pointer
        Consumer<void(*)(const Packet&)> consumer(&packet_ring, process_packet);
        consumer.consume_blocking();
        });*/
    cout << "test 2 \n";

        // Create IOCP
    HANDLE iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    if (iocp == NULL) {
        cout << "Can't create IOCP: " << GetLastError() << endl;
        WSACleanup();
        return 1;
    }
    cout << "test 3 \n";

    // Create UDP socket with overlapped flag
    SOCKET sock = WSASocket(AF_INET, SOCK_DGRAM, IPPROTO_UDP, NULL, 0, WSA_FLAG_OVERLAPPED);
    if (sock == INVALID_SOCKET) {
        cout << "Can't create socket: " << WSAGetLastError() << endl;
        CloseHandle(iocp);
        WSACleanup();
        return 1;
    }
    cout << "test 4 \n";

    // Bind socket to IP and port
    sockaddr_in serverHint;
    serverHint.sin_family = AF_INET;
    serverHint.sin_addr.S_un.S_addr = ADDR_ANY;
    serverHint.sin_port = htons(g_config->displayPort);
    if (bind(sock, (sockaddr*)&serverHint, sizeof(serverHint)) == SOCKET_ERROR) {
        cout << "Can't bind socket: " << WSAGetLastError() << endl;
        closesocket(sock);
        CloseHandle(iocp);
        WSACleanup();
        return 1;
    }
    cout << "test 5 \n";

    // Associate socket with IOCP (using completion key 0)
    if (CreateIoCompletionPort((HANDLE)sock, iocp, 0, 0) == NULL) {
        cout << "Can't associate socket with IOCP: " << GetLastError() << endl;
        closesocket(sock);
        CloseHandle(iocp);
        WSACleanup();
        return 1;
    }
    cout << "test 6 \n";

    // Allocate PerIOData structures
    PerIOData* ioDatas[MAX_PENDING];
    for (int i = 0; i < MAX_PENDING; ++i) {
        ioDatas[i] = new PerIOData();
        if (ioDatas[i] == nullptr) {
            cout << "Memory allocation failed" << endl;
            // Cleanup previous allocations
            for (int j = 0; j < i; ++j) {
                delete ioDatas[j];
            }
            closesocket(sock);
            CloseHandle(iocp);
            WSACleanup();
            return 1;
        }
    }
    cout << "test 7 \n";

    // Post initial receives
    for (int i = 0; i < MAX_PENDING; ++i) {
        PerIOData* data = ioDatas[i];
        ZeroMemory(&data->overlapped, sizeof(OVERLAPPED));
        data->wsabuf.buf = data->buffer;
        data->wsabuf.len = sizeof(data->buffer);
        data->addrLen = sizeof(data->clientAddr);

        DWORD flags = 0;
        int res = WSARecvFrom(sock, &data->wsabuf, 1, NULL, &flags,
            (sockaddr*)&data->clientAddr, &data->addrLen,
            &data->overlapped, NULL);
        if (res == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err != WSA_IO_PENDING) {
                cout << "WSARecvFrom failed: " << err << endl;
                // Cleanup
                for (int j = 0; j < MAX_PENDING; ++j) {
                    delete ioDatas[j];
                }
                closesocket(sock);
                CloseHandle(iocp);
                WSACleanup();
                return 1;
            }
        }
    }
    cout << "test 8 \n";

    // Create worker threads
    vector<thread> threads;
    int num_threads = thread::hardware_concurrency(); // Auto-detect cores; or hardcode 4
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([iocp, sock]() {
            while (true) {
                DWORD bytes = 0;
                ULONG_PTR key = 0;
                LPOVERLAPPED ov = NULL;

                BOOL result = GetQueuedCompletionStatus(iocp, &bytes, &key, &ov, INFINITE);
                if (!result) {
                    DWORD err = GetLastError();
                    if (ov == NULL) {
                        // Timeout or pure error (but INFINITE shouldn't timeout); handle shutdown if needed
                        if (err == ERROR_OPERATION_ABORTED) {
                            // Graceful shutdown signal; break if you add quit logic
                            break;
                        }
                        else {
                            // Log error and continue or break
                            {
                                lock_guard<mutex> lock(cout_mutex);
                                cout << "GetQueuedCompletionStatus failed: " << err << endl;
                            }
                            continue;
                        }
                    }
                    else {
                        // I/O completed but with error
                        {
                            lock_guard<mutex> lock(cout_mutex);
                            cout << "I/O error on completion: " << err << endl;
                        }
                        // Could cancel/repurpose the PerIOData, but for now continue
                    }
                }

                if (ov) {
                    PerIOData* data = CONTAINING_RECORD(ov, PerIOData, overlapped);

                    auto now = steady_clock::now();
                    iocp_adapter.on_packet_received(data->buffer, bytes, data->clientAddr);

                    lock_guard<mutex> lock(time_mutex);
                    if (!first_small_packet) {
                        auto delta = duration_cast<microseconds>(now - last_small_packet_time).count();

                        lock_guard<mutex> lock2(cout_mutex);
                        //cout << "Time since last packet: " << delta << " microseconds" << endl;
                    }
                    else {
                        first_small_packet = false;
                        lastframeEnd = now;
                    }
                    last_small_packet_time = now;


                    // Display message and client info (synced)
                    char clientIP[256];
                    ZeroMemory(clientIP, sizeof(clientIP));
                    inet_ntop(AF_INET, &data->clientAddr.sin_addr, clientIP, sizeof(clientIP));
                    {
                        lock_guard<mutex> lock(cout_mutex);
                        cout << "Message recv from " << clientIP << " : size of packet" << bytes << endl;
                        if (bytes == 16) {

                            auto deltaFrame = duration_cast<microseconds>(now - lastframeEnd).count();
                            cout << "frameTime: " << deltaFrame << "\n";
                            lastframeEnd = now;
                        }
                    }

                    // Repost the receive
                    ZeroMemory(&data->overlapped, sizeof(OVERLAPPED));
                    data->addrLen = sizeof(data->clientAddr);

                    DWORD flags = 0;
                    int res = WSARecvFrom(sock, &data->wsabuf, 1, NULL, &flags,
                        (sockaddr*)&data->clientAddr, &data->addrLen,
                        &data->overlapped, NULL);
                    if (res == SOCKET_ERROR) {
                        int err = WSAGetLastError();
                        if (err != WSA_IO_PENDING) {
                            {
                                lock_guard<mutex> lock(cout_mutex);
                                cout << "WSARecvFrom repost failed: " << err << endl;
                            }
                            // Could break, but continue for resilience
                        }
                    }
                }
            }
            });
    }




    for (auto& t : threads) {
        t.detach();
    }
    /*/ Join threads (blocks main forever since infinite loop; Ctrl+C to stop)
    for (auto& t : threads) {
        t.join();
    }*/

    /*/ Cleanup (won't reach here without shutdown logic, but for completeness)
    for (int i = 0; i < MAX_PENDING; ++i) {
        delete ioDatas[i];
    }
    closesocket(sock);
    CloseHandle(iocp);
    WSACleanup();*/
    return 0;
}