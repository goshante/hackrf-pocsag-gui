/*
*  Subject: HackRFTransmitter
*  Purpose: Transmitting PCM buffer through HackRF device.
*  Author: Goshante (http://github.com/goshante)
*  Year: 2023
*  Original project: https://github.com/goshante/pocsag-hackrf-tx
*
*  Comment: Free to use if you credit me in your project.
*/

#include "HackRFTransmitter.h"

constexpr uint32_t BUF_NUM			= 256;
constexpr uint32_t BYTES_PER_SAMPLE	= 2;
constexpr double M_PI					= 3.14159265358979323846;
constexpr uint32_t BUF_LEN			= 262144;         //hackrf tx buf

using namespace std::chrono_literals;

HackRFTransmitter::HackRFTransmitter(float localGain)
	: m_localGain(localGain / (float)100.0)
	, m_ready(false)
	, m_FM_phase(0)
	, m_queueThread(nullptr)
	, m_stop(true)
	, m_emptyQueue(true)
	, m_pcmSampleRate(0)
	, m_TX_On(false)
{
	memset(m_last_in_samples, 0, sizeof(m_last_in_samples));
	
	m_leftToSend = m_tail = m_head = 0;

	m_workerBuf.resize(BUF_NUM);
	for (auto& buf : m_workerBuf)
		buf.resize(BUF_LEN);

	m_sample_count = 0;
	m_interpolatedBuf.resize(BUF_LEN);
	m_IQ_buf.resize(BUF_LEN * BYTES_PER_SAMPLE);

	m_subchunkSizeSamples = 2048;
	m_subchunkOffset = 0;
	m_FMdeviationKHz = 75.0e3;
	m_AM = false;
	m_noIdleTx = false;
	m_hackrf_sample = 0;

	if (!m_device.Open(this))
		throw std::runtime_error("Failed to open HackRF device.");
}

HackRFTransmitter::~HackRFTransmitter()
{
	if (m_TX_On)
		StopTX();

	std::lock_guard<std::mutex> lock(m_deviceMutex);
	m_device.Close();
}

void HackRFTransmitter::SetFMDeviationKHz(double value)
{
	if (m_TX_On)
		throw std::runtime_error("Attempting to change TX deviation while transmission is active");
	m_FMdeviationKHz = value * 1000;
}

void HackRFTransmitter::SetTurnOffTXWhenIdle(bool off)
{
	m_noIdleTx = off;
}

void HackRFTransmitter::SetFrequency(uint64_t mhz, uint64_t khz, uint64_t hz)
{
	if (m_TX_On)
		throw std::runtime_error("Attempting to change TX frequency while transmission is active");
	m_device.SetFrequency((mhz * 1000000) + (khz * 1000) + hz);
}

void HackRFTransmitter::SetFrequency(uint64_t hz)
{
	if (m_TX_On)
		throw std::runtime_error("Attempting to change TX frequency while transmission is active");
	m_device.SetFrequency(hz);
}

void HackRFTransmitter::SetGainRF(float gain)
{
	if (m_TX_On)
		throw std::runtime_error("Attempting to change TX gain while transmission is active");
	m_device.SetGain(gain);
}

void HackRFTransmitter::SetLocalGain(float gain)
{
	if (m_TX_On)
		throw std::runtime_error("Attempting to change TX local gain while transmission is active");
	m_localGain = gain;
}

void HackRFTransmitter::SetAMP(bool enableamp)
{
	if (m_TX_On)
		throw std::runtime_error("Attempting to change TX amp while transmission is active");
	m_device.SetAMP(enableamp);
}

void HackRFTransmitter::Clear()
{
	if (m_TX_On)
		throw std::runtime_error("Attempting to clear queue while transmission is active");

	m_currentChunk.clear();
	while (!m_waveQueue.empty())
		m_waveQueue.pop();
	m_subchunkOffset = 0;
	m_FM_phase = 0;
}

bool HackRFTransmitter::StartTX()
{
	if (m_TX_On)
		return false;

	if (m_currentChunk.empty())
	{
		m_subchunkOffset = 0;
		m_FM_phase = 0;

		if (!m_waveQueue.empty() && m_pcmSampleRate != 0)
		{
			m_hackrf_sample = uint32_t((m_pcmSampleRate * 1.0 / m_subchunkSizeSamples) * BUF_LEN);
			m_device.SetSampleRate(m_hackrf_sample);
		}
	}
	m_stopped = {};
	m_started = {};
	m_stop = false;
	m_ready = true;
	m_TX_On = true;

	if (m_queueThread)
		delete m_queueThread;

	m_queueThread = new std::thread(&HackRFTransmitter::_workerThread, this);
	auto fut = m_started.get_future();
	if (fut.wait_for(10s) != std::future_status::ready)
		return false;

	return fut.get();
}

void HackRFTransmitter::_workerThread()
{
	auto& processSubChunk = [&]()
	{
		while (!m_stop)
		{
			if (!m_ready)
				continue;

			if (!m_device.IsRunning()) // Start TX if it is down.
				m_device.StartTx();

			_nextSubChunk(); // Transmit the last prepared subchunk.

			if (_prepareNext())
				continue;

			// Stop TX if No-TX when idle feature is enabled and the queue is empty.
			if (m_noIdleTx && m_waveQueue.empty())
				m_device.StopTx();
			break;
		}

		if (!m_stop) // Clear the current chunk only if not stopped.
			m_currentChunk.clear();
	};

	if (!m_device.StartTx()) //Fail and return if we cannot start TX
	{
		m_TX_On = false;
		m_started.set_value(false);
		return;
	}
	else
		m_started.set_value(true);	//Release waiter in StartTX() method

	//Stop if no data for TX (when m_noIdleTx). We start at least to test that we are able to start
	if (m_noIdleTx && m_currentChunk.empty() && m_waveQueue.empty())
		m_device.StopTx();

	while (!m_stop) //Continue untill we tell to stop
	{
		std::lock_guard<std::mutex> lock(m_queueMutex);
		if (!m_currentChunk.empty()) //If we still have unfinished subchunk - finish it first
		{
			processSubChunk();
			continue;
		}
		
		if (m_waveQueue.empty()) //When queue is empty and no chunks for TX
			m_emptyQueue = true;
		else
		{
			//If we finished last chunk - pop out next from queue
			if (m_subchunkOffset >= m_currentChunk.size())
			{
				m_currentChunk = std::move(m_waveQueue.front()); //Should be faster
				m_waveQueue.pop();

				//Reset FM phase and subchunk offset before transmitting new subchunk of our new chunk
				m_subchunkOffset = 0;
				m_FM_phase = 0;
				if (!_prepareNext()) //Prepare first subchunk to be transmitted
				{
					m_currentChunk.clear();
					continue;
				}
			}

			processSubChunk();
		}
	}

	m_TX_On = false;
	m_stopped.set_value(!m_device.StopTx());
}

bool HackRFTransmitter::StopTX()
{
	if (!m_TX_On && !m_queueThread)
		return false;

	m_stop = true;
	auto fut = m_stopped.get_future();
	if (fut.wait_for(30s) != std::future_status::ready)
		throw std::runtime_error("Failed to stop TX. Timeout.");
	bool stopped = fut.get();

	m_queueThread->join();
	delete m_queueThread;
	m_queueThread = nullptr;
	m_hackrf_sample = 0;

	return stopped;
}

void HackRFTransmitter::SetAM(bool set)
{
	if (m_TX_On)
		throw std::runtime_error("Attempting to change TX modulation while transmission is active");

	m_AM = set;
}

void HackRFTransmitter::SetSubChunkSizeSamples(size_t count)
{
	if (m_TX_On)
		throw std::runtime_error("Attempting to change subchunk sample count while transmission is active");

	m_subchunkSizeSamples = (uint32_t)count;
}

uint32_t HackRFTransmitter::GetDeviceSampleRate() const
{
	return m_hackrf_sample;
}

void HackRFTransmitter::SetPCMSamplingRate(size_t sampleRate)
{
	if (m_TX_On)
		throw std::runtime_error("Trying to change PCM sample rate while TX is active.");

	m_pcmSampleRate = (uint32_t)sampleRate;
}

void HackRFTransmitter::PushSamples(const HackRF_PCMSource& samples)
{
	std::lock_guard<std::mutex> lock(m_queueMutex);

	if (!m_TX_On || m_TX_On && m_pcmSampleRate == 0)
		m_pcmSampleRate = samples.GetSamplingRate();
	
	m_waveQueue.push(samples.GetRawBuf());
	m_emptyQueue = false;
}

uint32_t HackRFTransmitter::GetChunkSizeSamples() const
{
	return m_subchunkSizeSamples;
}

void HackRFTransmitter::_interpolation() 
{
	size_t i;		/* Input buffer index + 1. */
	uint32_t j = 0;	/* Output buffer index. */
	float pos;		/* Position relative to the input buffer
					* + 1.0. */

					/* We always "stay one sample behind", so what would be our first sample
					* should be the last one wrote by the previous call. */
	float* in_buf = &m_currentChunk[m_subchunkOffset];
	pos = (float)m_sample_count / (float)BUF_LEN;
	while (pos < 1.0)
	{
		m_interpolatedBuf[j] = m_last_in_samples[3] + (in_buf[0] - m_last_in_samples[3]) * pos;
		j++;
		pos = (float)(j + 1) * (float)m_sample_count / (float)BUF_LEN;
	}

	/* Interpolation cycle. */
	i = (uint32_t)pos;
	while (j < (BUF_LEN - 1))
	{

		m_interpolatedBuf[j] = in_buf[i - 1] + (in_buf[i] - in_buf[i - 1]) * (pos - (float)i);
		j++;
		pos = (float)(j + 1) * (float)m_sample_count / (float)BUF_LEN;
		i = (uint32_t)pos;
	}

	/* The last sample is always the same in input and output buffers. */
	m_interpolatedBuf[j] = in_buf[m_sample_count - 1];

	/* Copy last samples to m_last_in_samples (reusing i and j). */
	for (i = m_sample_count - 4, j = 0; j < 4; i++, j++)
		m_last_in_samples[j] = in_buf[i];
}

void HackRFTransmitter::_modulation() 
{
	double fm_deviation = 2.0 * M_PI * m_FMdeviationKHz / m_hackrf_sample;

	//AM mode
	//Works so poor and unstable, needs to be re-implemented
	if (m_AM) 
	{
		for (uint32_t i = 0; i < BUF_LEN; i++) 
		{
			double	audio_amp = m_interpolatedBuf[i] * m_localGain;

			if (fabs(audio_amp) > 1.0)
				audio_amp = (audio_amp > 0.0) ? 1.0 : -1.0;

			m_IQ_buf[i * BYTES_PER_SAMPLE] = float(audio_amp);
			m_IQ_buf[i * BYTES_PER_SAMPLE + 1] = 0;
		}
	}
	//FM mode
	//Also is FSK mode too! Just try to send something like POCSAG samples in PCM format
	else 
	{
		for (uint32_t i = 0; i < BUF_LEN; i++) 
		{
			double audio_amp = m_interpolatedBuf[i] * m_localGain;

			if (fabs(audio_amp) > 1.0)
				audio_amp = (audio_amp > 0.0) ? 1.0 : -1.0;

			m_FM_phase += fm_deviation * audio_amp;
			while (m_FM_phase > (float)(M_PI))
				m_FM_phase -= (float)(2.0 * M_PI);
			while (m_FM_phase < (float)(-M_PI))
				m_FM_phase += (float)(2.0 * M_PI);

			m_IQ_buf[i * BYTES_PER_SAMPLE] = (float)sin(m_FM_phase);
			m_IQ_buf[i * BYTES_PER_SAMPLE + 1] = (float)cos(m_FM_phase);
		}
	}
}

void HackRFTransmitter::_work(size_t offset) 
{
	std::lock_guard<std::mutex> lock(m_deviceMutex);
	auto& buf = m_workerBuf[m_head];
	for (uint32_t i = 0; i < BUF_LEN; i++) 
		buf[i] = (int8_t)(m_IQ_buf[i + offset] * 127.0);
	m_head = (m_head + 1) % BUF_NUM;
	m_leftToSend++;
}

int HackRFTransmitter::onData(int8_t* buffer, uint32_t length)
{
	std::lock_guard<std::mutex> lock(m_deviceMutex);

	if (m_leftToSend == 0)
		memset(buffer, 0, length);
	else 
	{
		int8_t* p = &(m_workerBuf[m_tail][0]);
		memcpy(buffer, p, length);
		m_tail = (m_tail + 1) % BUF_NUM;
		m_leftToSend--;
		if (m_leftToSend == 0)
			m_ready = true;
	}

	return 0;
}

bool HackRFTransmitter::_prepareNext()
{
	auto samples = m_currentChunk.size();
	if (m_subchunkOffset >= samples)
		return false;

	if (m_subchunkOffset + m_subchunkSizeSamples > samples)
		m_sample_count = samples - m_subchunkOffset;
	else
		m_sample_count = m_subchunkSizeSamples;


	uint32_t newRFSampleRate = 0;
	{
		std::lock_guard<std::mutex> lock(m_queueMutex);
		newRFSampleRate = uint32_t((m_pcmSampleRate * 1.0 / m_subchunkSizeSamples) * BUF_LEN);
	}
	
	if (m_hackrf_sample != newRFSampleRate)
	{
		m_hackrf_sample = newRFSampleRate;
		m_device.SetSampleRate(m_hackrf_sample);
	}

	_interpolation();
	_modulation();

	m_subchunkOffset += m_sample_count;
	return true;
}

void HackRFTransmitter::_nextSubChunk()
{
	auto samples = m_currentChunk.size();
	if (m_subchunkOffset >= samples)
		return;

	m_ready = false;
	for (uint32_t i = 0; i < (BUF_LEN * BYTES_PER_SAMPLE); i += BUF_LEN)
		_work(i);
}

bool HackRFTransmitter::IsIdle() const
{
	return m_currentChunk.empty() && m_emptyQueue && m_TX_On;
}

bool HackRFTransmitter::IsRunning() const
{
	return m_TX_On;
}

bool HackRFTransmitter::WaitForEnd(const std::chrono::milliseconds timeout) const
{
	std::chrono::milliseconds waiting = 0ms;

	while (waiting < timeout)
	{
		if (!m_TX_On)
			return true;

		std::this_thread::sleep_for(10ms);
		waiting += 10ms;
	}

	return false;
}

bool HackRFTransmitter::WaitForIdle(const std::chrono::milliseconds timeout) const
{
	std::chrono::milliseconds waiting = 0ms;

	while (waiting < timeout)
	{
		if (IsIdle())
			return true;

		std::this_thread::sleep_for(10ms);
		waiting += 10ms;
	}

	return false;
}