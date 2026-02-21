#pragma once

template <typename T>
uint32_t DataSize() {
	return sizeof(T) - sizeof(uint32_t);
}

struct kbm {
	int x, y;
};

struct audioData {
	uint32_t SAMPLE_RATE;
	uint16_t CHANNELS;
	uint16_t BITS_PER_SAMPLE;
	uint32_t FRAMES_PER_PACKET;
};

struct mainTData {
	POINT LinXY;
	uint32_t KbmTC;
	uint32_t KbmTS;
	uint32_t audioPort;
	uint32_t cbCport;
	uint32_t cbSport;
	uint32_t displayPort;
};

const struct ThreadData {
	kbm* kbmIn;
	audioData* audio;
	mainTData* main;
	uint32_t size = DataSize<ThreadData>();
};


struct ThreadSignals {
	ThreadData* Data;
	bool* Signals;
	uint32_t size = DataSize<ThreadSignals>();
};



bool InitThreads();