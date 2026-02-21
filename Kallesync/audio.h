#ifndef AUDIO_H
#define AUDIO_H

#include <cstdint>
#include <atomic>

void audio(std::atomic<bool>* running, uint16_t port);

#endif