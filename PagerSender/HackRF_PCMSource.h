#pragma once

/*
*  Subject: HackRF_PCMSource
*  Purpose: Converts PCM audio into float format for HackRFTransmitter class.
*  Author: Goshante (http://github.com/goshante)
*  Year: 2023
*  Original project: https://github.com/goshante/pocsag-hackrf-tx
*
*  Comment: Free to use if you credit me in your project.
*/

#include <vector>
#include <string>

//Push object of this class into transmitter queue to transmit your sound or FSK data.
//Supported 3 sources of audio: File, Buffered file and Raw samples.
class HackRF_PCMSource
{
private:
	std::vector<float> m_buf;
	uint32_t m_samplingRate;

	void _makeBuffer(const std::vector<uint8_t>& buf);

	HackRF_PCMSource(const HackRF_PCMSource&) = delete;
	HackRF_PCMSource& operator=(const HackRF_PCMSource&) = delete;

	friend class HackRFTransmitter;

public:
	//Build buffer from file
	HackRF_PCMSource(const std::string& fileName);

	//Build buffer from memory (Another buffer with PCM header included)
	HackRF_PCMSource(const std::vector<uint8_t>& memoryBuffer); 

	//Build buffer from raw PCM samples and some data about it
	HackRF_PCMSource(const void* sampleBufferRaw, size_t bufSize, uint32_t sampleRate, uint32_t bitrate, uint16_t channels);
	~HackRF_PCMSource();
};