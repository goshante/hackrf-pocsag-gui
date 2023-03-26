/*
*  Subject: HackRFDevice
*  Purpose: Direct access to HackRF device. Not intended for external use.
*  Author: Goshante (http://github.com/goshante)
*  Year: 2023
*  Original project: https://github.com/goshante/pocsag-hackrf-tx
*
*  Comment: Free to use if you credit me in your project.
*/

#include "HackRFDevice.h"

static int _hackrf_tx_callback(hackrf_transfer *transfer) 
{
	HackRFDevice *obj = (HackRFDevice *)transfer->tx_ctx;
	return obj->HackRFCallback((int8_t *)transfer->buffer, transfer->valid_length);
}

HackRFDevice::HackRFDevice()
	:_dev(NULL), mRunning(false)
{
}


HackRFDevice::~HackRFDevice()
{
	if (_dev) {
		this->Close();
	}
}

bool HackRFDevice::Open(IHackRFData *handler)
{
	mHandler = handler;
	hackrf_init();

	int ret = hackrf_open(&_dev);
	if (ret != HACKRF_SUCCESS) 
	{
		printf("Failed to open HackRF device");
		hackrf_close(_dev);
		return false;
	}
	return true;
}

int HackRFDevice::HackRFCallback(int8_t* buffer, uint32_t length)
{
	return mHandler->onData(buffer, length);
}

bool HackRFDevice::StartTx()
{
	if (mRunning)
	{
		printf("Failed to Start TX. Stream already running.");
		return false;
	}

	
	int ret = hackrf_start_tx(_dev, _hackrf_tx_callback, (void *)this);
	if (ret != HACKRF_SUCCESS) 
	{
		printf("Failed to start TX streaming");
		hackrf_close(_dev);
		return false;
	}

	mRunning = true;
	return mRunning;
}

bool HackRFDevice::StopTx()
{
	if (!mRunning)
		return true;

	int ret = hackrf_stop_tx(_dev);
	if (ret != HACKRF_SUCCESS) 
	{
		printf("Failed to stop TX streaming");
		hackrf_close(_dev);
		return false;
	}

	mRunning = false;
	return mRunning;
}

void HackRFDevice::SetFrequency(uint64_t freg)
{
	hackrf_set_freq(_dev, freg);
}

void HackRFDevice::SetGain(float gain)
{
	hackrf_set_txvga_gain(_dev, uint32_t(gain));
}

void HackRFDevice::SetAMP(bool enableamp)
{
	hackrf_set_amp_enable(_dev, enableamp);
}

void HackRFDevice::SetSampleRate(uint32_t sample_rate)
{
	hackrf_set_sample_rate(_dev, sample_rate);
	hackrf_set_baseband_filter_bandwidth(_dev, 1750000);
}

void HackRFDevice::Close() 
{
	mRunning = false;
	hackrf_stop_tx(_dev);
	hackrf_close(_dev);
	_dev = NULL;
}

bool HackRFDevice::IsRunning() const
{
	return mRunning;
}
