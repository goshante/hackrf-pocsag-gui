#pragma once

/*
*  Subject: IHackRFData
*  Purpose: Callback interface for HackRFDevice
*  Author: Goshante (http://github.com/goshante)
*  Year: 2023
*  Original project: https://github.com/goshante/pocsag-hackrf-tx
*
*  Comment: Free to use if you credit me in your project.
*/

#include <stdint.h>

class IHackRFData
{
public:
	IHackRFData() {};
	~IHackRFData() {};

protected:
	virtual int onData(int8_t* buffer, uint32_t length) = 0;

	friend class HackRFDevice;
};