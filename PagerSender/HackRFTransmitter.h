#pragma once

/*
*  Subject: HackRFTransmitter
*  Purpose: Transmitting PCM buffer through HackRF device.
*  Author: Goshante (http://github.com/goshante)
*  Year: 2023
*  Original project: https://github.com/goshante/pocsag-hackrf-tx
*
*  Comment: Free to use if you credit me in your project.
*/

#include <mutex>
#include "IHackRFData.h"
#include "HackRFDevice.h"
#include "HackRF_PCMSource.h"
#include <atomic>
#include <thread>
#include <queue>
#include <future>

class HackRFTransmitter : public IHackRFData
{
private:
	using WorkerBuf_t = std::vector<std::vector<int8_t>>;
	using PCMChunk_t = std::vector<float>;
	using PCMQueue_t = std::queue<PCMChunk_t>;

	HackRFDevice m_device;
	std::mutex m_deviceMutex;
	std::mutex m_queueMutex;
	int m_leftToSend;
	WorkerBuf_t m_workerBuf;
	int m_tail;
	int m_head;
	float m_localGain;
	std::vector<float> m_interpolatedBuf;
	std::vector<float> m_IQ_buf;
	uint32_t m_sample_rate;
	size_t m_sample_count;
	uint32_t m_hackrf_sample;
	float m_last_in_samples[4];
	uint32_t m_subchunkSizeSamples;
	PCMQueue_t m_waveQueue;
	PCMChunk_t m_currentChunk;
	uint32_t m_pcmSampleRate;
	size_t m_subchunkOffset;
	double m_FMdeviationKHz;
	bool m_AM;
	bool m_noIdleTx;
	std::atomic<bool> m_ready;
	double m_FM_phase;
	std::thread* m_queueThread;
	std::promise<bool> m_stopped;
	std::promise<bool> m_started;
	std::atomic<bool> m_stop;
	std::atomic<bool> m_emptyQueue;
	std::atomic<bool> m_TX_On;

	void _interpolation();
	void _modulation();
	void _work(size_t offset);
	bool _prepareNext();
	void _workerThread();
	void _nextSubChunk();

protected:
	int onData(int8_t* buffer, uint32_t length);

public:
	HackRFTransmitter(float localGain = 90.0f);
	~HackRFTransmitter();

	//Safe to call while TX is active
	void PushSamples(const HackRF_PCMSource& samples);

	bool WaitForEnd(const std::chrono::milliseconds timeout) const;
	bool WaitForIdle(const std::chrono::milliseconds timeout) const;
	uint32_t GetDeviceSampleRate() const;
	uint32_t GetChunkSizeSamples() const;
	bool IsIdle() const;
	bool IsRunning() const;
	
	//Excepts on call attempt while TX is active
	void SetFrequency(uint64_t mhz, uint64_t khz, uint64_t hz = 0);
	void SetFrequency(uint64_t hz);
	void SetSubChunkSizeSamples(size_t count);
	void SetGainRF(float gain);
	void SetLocalGain(float gain);
	void SetAMP(bool enableamp);
	void SetPCMSamplingRate(size_t sampleRate);
	void SetAM(bool set);
	void SetFMDeviationKHz(double value);
	void SetTurnOffTXWhenIdle(bool off);
	void Clear(); //Clear all samples for TX

	//Stop and start TX
	bool StartTX(); //If cleanUpPrevData - cleans all chunks and subchunks
	bool StopTX();
};

