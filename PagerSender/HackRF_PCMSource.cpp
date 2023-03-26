/*
*  Subject: HackRF_PCMSource
*  Purpose: Converts PCM audio into float format for HackRFTransmitter class.
*  Author: Goshante (http://github.com/goshante)
*  Year: 2023
*  Original project: https://github.com/goshante/pocsag-hackrf-tx
*
*  Comment: Free to use if you credit me in your project.
*/

#include "HackRF_PCMSource.h"
#include <stdexcept>
#include <sstream>

#define WAVE_FORMAT_PCM		1

static inline int16_t pcm8to16bit(int8_t sample)
{
    constexpr const int16_t mult = 32767 / 255;
    return sample * mult;
}

static inline int16_t pcm32to16bit(int32_t sample)
{
    constexpr const int32_t mult = 2147483647 / 32767;
    return sample / mult;
}

static inline int16_t pcm24to16bit(int32_t sample)
{
    constexpr const int32_t mult = 8388607 / 32767;
    return sample / mult;
}

static void wavRead32FromMemory(const unsigned char* in, size_t inSize, uint16_t channels, std::vector<float>& out,
    size_t start, size_t sampleCount, uint16_t byterate)
{
    unsigned char bufi[8];
    size_t off = start;
    auto readNext = [&in, &bufi, &off, &inSize](size_t itemSize, size_t count)
    {
        size_t i = 0;
        for (i = 0; i < count * itemSize && i + off < inSize; i++)
            bufi[i] = in[i + off];
        off += i;

        return i / itemSize;
    };

    size_t step = channels;
    int8_t i8;
    int16_t i16;
    int32_t i32;

    auto makeSample = [&]()
    {
        switch (byterate)
        {
        case 1:
            i8 = ((int8_t*)bufi)[0];
            i16 = pcm8to16bit(i8);
            break;

        case 2:
            i16 = ((int16_t*)bufi)[0];
            break;

        case 3:
            i32 = bufi[0] | (bufi[1] << 8) | (bufi[2] << 16);
            if (i32 & 0x800000)
                i32 |= 0x80000000;
            i16 = pcm24to16bit(i32);
            break;

        case 4:
            i32 = ((int32_t*)bufi)[0];
            i16 = pcm32to16bit(i32);
            break;
        }
    };
    
    size_t i = 0;
    for  (i = 0; i < sampleCount; i += step)
    {
        if (readNext(byterate, 1) != 1)
            break;

        makeSample();

        //Make mono
        if (channels > 1)
        {
            if (readNext(byterate, 1) != 1)
                break;

            float fs1 = (float)i16 / 65530;
            makeSample();
            float fs2 = (float)i16 / 65530;
            out[i / channels] = ((fs1 + fs2) / 2.0f);
        }
        else
            out[i] = ((float)i16 / 65530);
    }
}

HackRF_PCMSource::HackRF_PCMSource(const std::string& fileName)
{
    FILE* file;
    if (!(file = fopen(fileName.c_str(), "rb")))
        throw std::runtime_error("Cannot open wav file");

	std::vector<uint8_t> buf;
    fseek(file, 0L, SEEK_END);
    uint32_t sz = ftell(file);
    buf.resize(sz);

    rewind(file);
    if (fread(&buf[0], sz, 1, file) != 1)
        throw std::runtime_error("Cannot read wav file");
	
    fclose(file);
	_makeBuffer(buf);
}

HackRF_PCMSource::HackRF_PCMSource(const void* sampleBufferRaw, size_t bufSize, uint32_t sampleRate, uint32_t bitrate, uint16_t channels)
{
    if (channels > 2)
        throw std::runtime_error("Unsupported channel number (supported only mono and stereo)");

    if (bitrate > 32 || bitrate % 8 != 0)
        throw std::runtime_error("Unsupported bitrate");

    uint16_t byterate = bitrate / 8;
    if (bufSize % byterate != 0)
        throw std::runtime_error("Buffer size not matching it's bitrate");

    m_samplingRate = sampleRate;
    size_t sampleCount = bufSize / byterate;
    m_buf.resize(sampleCount / channels);
    wavRead32FromMemory((unsigned char*)sampleBufferRaw, bufSize, channels, m_buf, 44, sampleCount, byterate);
}

HackRF_PCMSource::~HackRF_PCMSource()
{
}

HackRF_PCMSource::HackRF_PCMSource(const std::vector<uint8_t>& buf)
{
	_makeBuffer(buf);
}

void HackRF_PCMSource::_makeBuffer(const std::vector<uint8_t>& buf)
{
	if (buf[0] != 'R' || buf[1] != 'I' || buf[2] != 'F' || buf[3] != 'F' || buf.size() < 44)
		throw std::runtime_error("This is not a WAVE file or buffer.");

	uint16_t pcm = *reinterpret_cast<const uint16_t*>(&buf[20]);
	
	if (pcm != WAVE_FORMAT_PCM)
		throw std::runtime_error("This is not PCM wave format. Other formats is unsupported.");

	uint16_t channels = *reinterpret_cast<const uint16_t*>(&buf[22]);
    m_samplingRate = *reinterpret_cast<const uint32_t*>(&buf[24]);
	//m_pcmHeader.sampleBytePerSec = *reinterpret_cast<const uint32_t*>(&buf[28]);
	uint16_t bitrate = *reinterpret_cast<const uint16_t*>(&buf[34]);
    uint16_t byterate = bitrate / 8;

    if (channels > 2)
        throw std::runtime_error("Unsupported channel number (supported only mono and stereo)");

    if (bitrate > 32 || bitrate % 8 != 0)
        throw std::runtime_error("Unsupported bitrate");

	size_t sampleCount = (buf.size() - 44) / byterate;
    m_buf.clear();
    m_buf.resize(sampleCount / channels);
    wavRead32FromMemory(&buf[0], buf.size(), channels, m_buf, 44, sampleCount, byterate);
}

