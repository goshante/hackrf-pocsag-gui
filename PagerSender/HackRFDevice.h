#pragma once

/*
*  Subject: HackRFDevice
*  Purpose: Direct access to HackRF device. Not intended for external use.
*  Author: Goshante (http://github.com/goshante)
*  Year: 2023
*  Original project: https://github.com/goshante/pocsag-hackrf-tx
*
*  Comment: Free to use if you credit me in your project.
*/

#include "IHackRFData.h"
#include <hackrf.h>
#include <stdint.h>
#include <stdio.h>
#include <atomic>

//This class is incapsulated into HackRFTransmitter so you don't have to use it at all.
class HackRFDevice
{
private:
	hackrf_device *_dev;
	IHackRFData *mHandler;
	std::atomic<bool> mRunning;
	
public:
	HackRFDevice();
	~HackRFDevice();

public:
	int HackRFCallback(int8_t* buffer, uint32_t length);
	bool Open(IHackRFData *handler);
	void SetFrequency(uint64_t freg);
	void SetGain(float gain);
	void SetAMP(bool enableamp);
	void SetSampleRate(uint32_t sample_rate);
	bool StartTx();
	bool StopTx();
	void Close();
	bool IsRunning() const;
};
