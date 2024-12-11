/*
*  Subject: POCSAG::Encoder
*  Purpose: Encode text, numeric and tone messages for pagers into POCSAG protocol buffer.
*           Can be in raw form and in form of PCM modulated buffer.
*  Author: Goshante (http://github.com/goshante)
*  Year: 2023
*  Original project: https://github.com/goshante/pocsag-hackrf-tx
*
*  Comment: Free to use if you credit me in your project.
*/

#include "POCSAG.h"
#include <bitset>
#include <stdexcept>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <functional>

namespace POCSAG
{
	constexpr uint32_t NUMERIC_CHAR_SIZE_BITS = 4;
	constexpr uint32_t ALPHANUMERIC_CHAR_SIZE_BITS = 7;
	constexpr uint32_t ADDR_MAX = 2097151;
	constexpr uint32_t CW_MESSAGE_BIT = 0x80000000;
	constexpr uint32_t SYNC_CODEWORD = 0x7CD215D8;
	constexpr uint32_t IDLE_CODEWORD = 0x7A89C197;
	constexpr uint32_t CW_MSG_SIZE_BITS = 20;
	constexpr uint32_t FRAMES_PER_BATCH = 8;
	constexpr uint32_t CW_PER_FRAMES = 2;
	constexpr uint32_t BATCH_SIZE_IN_CW = (FRAMES_PER_BATCH * CW_PER_FRAMES) + 1;
	constexpr uint32_t BATCH_MESSAGE_MAX_BITS = FRAMES_PER_BATCH * CW_PER_FRAMES * CW_MSG_SIZE_BITS;
	constexpr uint32_t PREAMBLE_SIZE_BYTES = 72;
	constexpr uint8_t  PREAMBLE_SEQUENCE = 0xAA; //10101010
	constexpr uint16_t PCM_AMPLITUDE = 5000;
	constexpr uint32_t WAVE_FORMAT_PCM = 1;

	using Codeword_t = uint32_t;
	using NumericBuffer_t = std::vector<std::bitset<NUMERIC_CHAR_SIZE_BITS>>;
	using AlphanumericBuffer_t = std::vector<std::bitset<ALPHANUMERIC_CHAR_SIZE_BITS>>;
	using utf8char_t = unsigned long;

	/*
	* Pager message string encoder
	*/

	bool isUTF8(const std::string& str)
	{
		size_t i = 0;
		while (i < str.length())
		{
			if ((str[i] & 0x80) == 0)
				i++;
			else if ((str[i] & 0xE0) == 0xC0)
			{
				if (i + 1 >= str.length() || (str[i + 1] & 0xC0) != 0x80)
					return false;
				i += 2;
			}
			else if ((str[i] & 0xF0) == 0xE0)
			{
				if (i + 2 >= str.length() ||
					(str[i + 1] & 0xC0) != 0x80 || (str[i + 2] & 0xC0) != 0x80)
					return false;
				i += 3;
			}
			else if ((str[i] & 0xF8) == 0xF0)
			{
				if (i + 3 >= str.length() ||
					(str[i + 1] & 0xC0) != 0x80 || (str[i + 2] & 0xC0) != 0x80 || (str[i + 3] & 0xC0) != 0x80)
					return false;
				i += 4;
			}
			else
				return false;
		}
		return true;
	}

	void enumerateUTF8String(const std::string& u8str, std::function<void(utf8char_t ch, size_t n, size_t cpsz)> callback)
	{
		size_t n = 0;
		for (size_t i = 0; i < u8str.length();)
		{
			int cplen = 1;
			if ((u8str[i] & 0xf8) == 0xf0) cplen = 4;
			else if ((u8str[i] & 0xf0) == 0xe0) cplen = 3;
			else if ((u8str[i] & 0xe0) == 0xc0) cplen = 2;
			if ((i + cplen) > u8str.length()) cplen = 1;
			utf8char_t ch = 0;
			auto sch = u8str.substr(i, cplen);
			for (int k = 0; k < cplen; k++)
				reinterpret_cast<char*>(&ch)[(cplen - 1) - k] = sch[k];
			callback(ch, n, cplen);
			n++;
			i += cplen;
		}
	}

	std::string encodeString7bit(const std::string& input, Charset charset)
	{
		char cyrLower[33] =
		{
			'E', 'A', 'B', 'W', 'G', 'D', 'E', 'V',
			'Z', 'I', 'J', 'K', 'L', 'M', 'N', 'O',
			'P', 'R', 'S', 'T', 'U', 'F', 'H', 'C',
			'^', '[', ']', '_', 'Y', 'X', '\\', '@', 'Q'
		};

		char cyrUpper[33] =
		{
			'e', 'a', 'b', 'w', 'g', 'd', 'e', 'v',
			'z', 'i', 'j', 'k', 'l', 'm', 'n', 'o',
			'p', 'r', 's', 't', 'u', 'f', 'h', 'c',
			'~', '{', '}', '_', 'y', 'x', '|', '`', 'q'
		};

		auto convertSpecial = [&](char c) -> char
		{
			if (charset == Charset::Latin)
				return c;

			if (c == ']')
				return 0x1E;
			else if (c == '[')
				return 0x1F;
			else if (c == 'U')
				return 0x1B;
			else
				return c;
		};

		if (charset == Charset::Raw)
			return input;

		std::string str;

		if (charset == Charset::Latin)
		{
			for (char c : input)
			{
				if (c == '\r')
					continue;

				if ((c < 26 || c > 126) && c != '\n')
					str.push_back('?');
				else
					str.push_back(convertSpecial(c));
			}
		}
		else if (charset == Charset::Cyrilic)
		{
			if (isUTF8(input))
			{
				enumerateUTF8String(input, [&](utf8char_t ch, size_t n, size_t cpsz)
					{
						if (ch == '\r')
							return;

						if (ch == 0xD191) //Small Yo
							str.push_back(cyrLower[0]);
						else if (ch == 0xD081) //Capital Yo
							str.push_back(cyrUpper[0]);
						else if (ch >= 0xD090 && ch < 0xD0B0) //Capital cyrilic
							str.push_back(cyrUpper[(ch + 1) - 0xD090]);
						else if (ch >= 0xD0B0 && ch <= 0xD0BF) //Small cyrilic (part 1)
							str.push_back(cyrLower[(ch + 1) - 0xD0B0]);
						else if (ch >= 0xD180 && ch <= 0xD18F) //Small cyrilic (part 2)
							str.push_back(cyrLower[(ch + 1) - 0xD180 + 0x10]);
						else if ((ch < 0x20 || ch > 0x7E) && ch != '\n')
							str.push_back('?');
						else
							str.push_back(convertSpecial(char(ch)));
					});
			}
			else
			{
				for (unsigned char c : input)
				{
					if (c == '\r')
						continue;

					if (c == 0xB8) //Small Yo
						str.push_back(cyrLower[0]);
					else if (c == 0xA8) //Capital Yo
						str.push_back(cyrUpper[0]);
					else if ((c < 26 || c < 0xC0 && c > 0x7E) && c != '\n')
						str.push_back('?');
					else if (c >= 0xC0 && c < 0xE0) //Capital cyrilic
						str.push_back(cyrUpper[c - 0xC0 + 1]);
					else if (c >= 0xE0) //Small cyrilic
						str.push_back(cyrLower[c - 0xE0 + 1]);
					else
						str.push_back(convertSpecial(c));
				}
			}
		}
		str.push_back(0x0);

		return str;
	}

	/*
	*  Vector utils
	*/
	void insert32bit(std::vector<unsigned char>& data, uint32_t u)
	{
		uint8_t* ptr = reinterpret_cast<uint8_t*>(&u);
		for (int i = 0; i < 4; i++)
			data.push_back(ptr[i]);
	}

	template <typename T>
	static void append(std::vector<uint8_t>& buf, T data)
	{
		const uint8_t* bData = reinterpret_cast<const uint8_t*>(&data);
		for (size_t i = 0; i < sizeof(T); i++)
			buf.push_back(bData[i]);
	}

	static void append(std::vector<uint8_t>& buf, const uint8_t* bytes, size_t count)
	{
		for (size_t i = 0; i < count; i++)
			buf.push_back(bytes[i]);
	}

	/*
	*  PCM utils
	*/

	template<typename T>
	static uint8_t getBitReversed(T cw, size_t bit)
	{
		uint8_t max = sizeof(T) * 8;
		T mask = 1;
		mask <<= (max - 1 - bit);
		return (cw & mask) > 0 ? 1 : 0;
	}


	static void MakePCM(const std::vector<Encoder::PCMSample_t>& samples, std::vector<uint8_t>& wave, uint32_t sampleRate)
	{
		wave.clear();
		uint16_t wBPS = static_cast<uint16_t>(sizeof(Encoder::PCMSample_t) * 8);
		append(wave, (const uint8_t*)"RIFF", 4);
		append<long>(wave, 36 + (int)(samples.size() * sizeof(Encoder::PCMSample_t)));
		append(wave, (const uint8_t*)"WAVE", 4);
		append(wave, (const uint8_t*)"fmt ", 4);
		append<long>(wave, 16);
		append<uint16_t>(wave, WAVE_FORMAT_PCM);
		append<uint16_t>(wave, 1); //Mono
		append<uint32_t>(wave, sampleRate);
		append<long>(wave, sampleRate * 1 * (wBPS / 8));
		append<uint16_t>(wave, 1 * (wBPS / 8));
		append<uint16_t>(wave, wBPS);
		append(wave, (const uint8_t*)"data", 4);
		append<uint32_t>(wave, (uint32_t)samples.size() * sizeof(Encoder::PCMSample_t));
		append(wave, reinterpret_cast<const uint8_t*>(&samples[0]), samples.size() * sizeof(Encoder::PCMSample_t));
	}

	/*
	*  POCSAG utils
	*/
	static uint32_t SignFrame(uint32_t in) //CRC and parity bit are added to the end of the frame
	{
		uint32_t cw = in, newCw = in, parity = 0;

		for (int bit = 1; bit <= 21; bit++, cw <<= 1)
		{
			if (cw & 0x80000000)
				cw ^= 0xED200000;
		}

		newCw |= (cw >> 21);
		cw = newCw;
		for (int bit = 1; bit <= 32; bit++, cw <<= 1)
		{
			if (cw & 0x80000000)
				parity++;
		}

		if (parity % 2)
			newCw++;

		return newCw;
	}

	static Codeword_t MakeAddressCodeword(RIC addr, Function func)
	{
		Codeword_t cw = 0; //First bit is 0, stands for address cw
		addr >>= 3; //Get rid of the last 3 bits, they will be recovered from frame position in batch
		addr <<= 13; //Move address on it's place in cw
		cw = cw | addr | (((uint32_t)(uint32_t(func) & 0x03)) << 11); // Frame code | Addr | Function | Place for CRC | Place for parity bit
		return SignFrame(cw); //Signs with CRC and parity bit
	}

	static uint8_t ReverseChar(char c) //We need this function to send each character bits in reversed order
	{
		uint8_t result = 0;
		for (int i = 0; i < 8; i++)
			result = (result << 1) | ((c >> i) & 1);
		return result >> (8 - ALPHANUMERIC_CHAR_SIZE_BITS); //Because our character is 7 bit
	}

	static uint8_t ReverseNum(uint8_t c) //Same for numbers
	{
		uint8_t result = 0;
		for (int i = 0; i < 8; i++)
			result = (result << 1) | ((c >> i) & 1);
		return result >> NUMERIC_CHAR_SIZE_BITS; //Because our nibble number is 4 bit
	}

	template<class T>
	static Codeword_t MakeMessageCodeword(const T& msg, size_t& offset, size_t maxBits)
	{
		if (offset >= maxBits || msg.empty())
			return IDLE_CODEWORD;

		Codeword_t cw = 0;
		const size_t wordSize = msg[0].size();
		const size_t count = CW_MSG_SIZE_BITS;
		size_t cellNum = offset / wordSize;
		size_t bitIndex = offset - (cellNum * wordSize);

		size_t counter = 0;
		for (size_t i = cellNum; i < msg.size() && counter < count && counter + offset < maxBits; i++)
		{
			auto cell = msg[i];
			for (size_t j = bitIndex; j < wordSize && counter < count && counter + offset < maxBits; j++)
			{
				cw <<= 1;
				cw |= cell[wordSize - 1 - j] ? 1 : 0;
				counter++;
			}
			bitIndex = 0;
		}

		if (counter < count)
		{
			if (wordSize == NUMERIC_CHAR_SIZE_BITS)
			{
				size_t rest = (count - counter) / wordSize;
				for (size_t i = 0; i < rest; i++)
				{
					cw <<= wordSize;
					cw |= ReverseNum(0xC); //Fill with spaces rest of empty space for numeric messages
				}
			}
			else
			{
				for (size_t i = 0; i < count - counter; i++)
				{
					cw <<= 1;
					cw |= 0; //Fill with zeroes rest of empty space for alphanumeric messages
				}
			}
		}

		offset += counter;
		cw <<= 11; //Move message on it's place in cw
		cw |= CW_MESSAGE_BIT; //Message codeword marker
		return SignFrame(cw);
	}

	static bool ValidateMessage(const std::string& msg, Type msgType)
	{
		if (msgType != Type::Alphanumeric)
			return true;

		for (char c : msg)
		{
			if (c < 0)
				return false;
		}

		return true;
	}

	static uint8_t ConvertToNumeric(char c)
	{
		if (c >= 0x30 && c <= 0x39)
			return ReverseNum(c - 0x30);
		else if (c == '*')
			return ReverseNum(0xA);
		else if (c == 'U' || c == 'u')
			return ReverseNum(0xB);
		else if (c == ' ' || c == '\n')
			return ReverseNum(0xC);
		else if (c == '-')
			return ReverseNum(0xD);
		else if (c == ')' || c == ']')
			return ReverseNum(0xE);
		else if (c == '(' || c == '[')
			return ReverseNum(0xF);
		else
			throw std::runtime_error("Unknown numeric value.");
	}

	static NumericBuffer_t EncodeMessageNumeric(const std::string& msg, size_t charSizeBits, size_t& maxBits)
	{
		NumericBuffer_t encoded;
		for (char c : msg)
		{
			if (c == 0 || c == '\r')
				break;
			uint8_t n = ConvertToNumeric(c); //Returns already reversed bit order, but char order is normal
			encoded.push_back(std::bitset<NUMERIC_CHAR_SIZE_BITS>(n));
			maxBits += NUMERIC_CHAR_SIZE_BITS;
		}
		return encoded;
	}

	static AlphanumericBuffer_t EncodeMessageAlphanumeric(const std::string& msg, size_t charSizeBits, size_t& maxBits)
	{
		AlphanumericBuffer_t encoded;
		for (char c : msg)
		{
			uint8_t n = ReverseChar(c); //Reversed bit order, but character order is normal
			encoded.push_back(std::bitset<ALPHANUMERIC_CHAR_SIZE_BITS>(n));
			maxBits += ALPHANUMERIC_CHAR_SIZE_BITS;
		}

		//Zero character as the end of the message
		if (encoded.size() > 0 && encoded[encoded.size() - 1].to_ulong() != 0)
		{
			encoded.push_back(std::bitset<ALPHANUMERIC_CHAR_SIZE_BITS>(0));
			maxBits += ALPHANUMERIC_CHAR_SIZE_BITS;
		}

		return encoded;
	}

	/*
	*  POCSAG Encoder class implementation
	*/

	Encoder::Encoder(size_t maxBatches, uint32_t sampleRate)
		: m_sampleRate(sampleRate)
		, m_amplitude(PCM_AMPLITUDE)
		, m_maxBatches(maxBatches)
		, m_dateFormat(DateTimePosition::None)
	{
	}

	Encoder::~Encoder()
	{
	}

	uint32_t Encoder::GetSampleRate() const
	{
		return m_sampleRate;
	}
	void Encoder::SetSampleRate(uint32_t sampleRate)
	{
		m_sampleRate = sampleRate;
	}

	Encoder::PCMSample_t Encoder::GetAmplitude() const
	{
		return m_amplitude;
	}

	void Encoder::SetAmplitude(uint32_t sampleRate)
	{
		m_amplitude = sampleRate;
	}

	void Encoder::SetDateTimePosition(DateTimePosition position)
	{
		m_dateFormat = position;
	}

	void Encoder::_modulatePOCSAG(std::vector<PCMSample_t>& output, const std::vector<uint8_t>& data, uint16_t bps)
	{
		Encoder::PCMSample_t neutralSample = 0;
		uint32_t samplesPerBit = m_sampleRate / bps;

		//Some silence at the beginning
		for (size_t j = 0; j < m_sampleRate / 2; j++)
			output.push_back(neutralSample);

		//Preamble
		for (size_t i = 0; i < 72; i++)
		{
			uint8_t cw = data[i];

			for (size_t j = 0; j < 8; j++)
			{
				auto bit = getBitReversed(cw, j);

				PCMSample_t sample;

				if (bit == 1)
					sample = m_amplitude;
				else
					sample = -m_amplitude;

				for (size_t j = 0; j < samplesPerBit; j++)
					output.push_back(sample);
			}

		}

		//Message body
		for (size_t i = 72; i < data.size(); i += 4)
		{
			uint32_t cw = *reinterpret_cast<const uint32_t*>(&data[i]);

			for (size_t j = 0; j < 32; j++)
			{
				auto bit = getBitReversed(cw, j);

				PCMSample_t sample;

				if (bit == 1)
					sample = m_amplitude;
				else
					sample = -m_amplitude;

				for (size_t j = 0; j < samplesPerBit; j++)
					output.push_back(sample);
			}
		}

		//Some silence at the end
		for (size_t j = 0; j < m_sampleRate / 2; j++)
			output.push_back(neutralSample);
	}

	std::string MakeDateAndTime()
	{
		// Get current time
		auto now = std::chrono::system_clock::now();

		// Convert to time_t (number of seconds since epoch)
		auto time_t_now = std::chrono::system_clock::to_time_t(now);

		// Convert to struct tm using localtime_s
		std::tm tm_now;
		localtime_s(&tm_now, &time_t_now);

		// Format string
		std::ostringstream oss;
		oss << std::put_time(&tm_now, "%d.%m.%Y %H:%M:%S") << " \n";

		return oss.str();
	}

	size_t Encoder::encode(std::vector<uint8_t>& output, RIC addr, Type msgType, std::string msg, BPS bps, Charset charset, Function func, bool rawPOCSAG)
	{
		if (msgType == Type::Alphanumeric)
		{
			//Pager 7-bit string encoding
			msg = encodeString7bit(msg, charset);

			//If we want to specify sending date and time in our message than append it according to position
			if (m_dateFormat == DateTimePosition::Begin)
				msg = MakeDateAndTime() + msg;
			else if (m_dateFormat == DateTimePosition::End)
				msg += "\n" + MakeDateAndTime();
		}

		size_t len = msg.length();
		size_t charSize = (msgType == Type::Alphanumeric ? ALPHANUMERIC_CHAR_SIZE_BITS : NUMERIC_CHAR_SIZE_BITS); //Bits
		size_t msgSize = len * charSize;
		size_t addrFrameNum = addr & 0b111; //Last 3 bits

		if (addr > ADDR_MAX)
			throw std::runtime_error("Address value is too big.");

		if (!ValidateMessage(msg, msgType))
			throw std::runtime_error("Message is invalid.");

		size_t msgCWCount = msgSize / CW_MSG_SIZE_BITS;
		if (msgSize % CW_MSG_SIZE_BITS != 0)
			msgCWCount++;
		if (msgType == Type::Tone)
			msgCWCount = 0;

		//We have different containers for numeric of alphanumeric messages
		NumericBuffer_t messageBitsN;
		AlphanumericBuffer_t messageBitsA;
		size_t maxBits = 0;

		if (msgType == Type::Numeric)
			messageBitsN = EncodeMessageNumeric(msg, charSize, maxBits);
		else
			messageBitsA = EncodeMessageAlphanumeric(msg, charSize, maxBits);

		//How much bits we skip before message
		size_t addrBitSkip = (addrFrameNum * CW_MSG_SIZE_BITS * CW_PER_FRAMES) + CW_MSG_SIZE_BITS;

		size_t batchCount = ((addrBitSkip + maxBits) / BATCH_MESSAGE_MAX_BITS);
		if ((addrBitSkip + maxBits) % BATCH_MESSAGE_MAX_BITS != 0)
			batchCount++;

		size_t lastFrameNum = (addrBitSkip + maxBits) / BATCH_MESSAGE_MAX_BITS;
		lastFrameNum = (addrBitSkip + maxBits) - (BATCH_MESSAGE_MAX_BITS * lastFrameNum);
		lastFrameNum = lastFrameNum / (CW_PER_FRAMES * CW_MSG_SIZE_BITS);

		//Otherwise trash characters could be displayed on pager at the end of the message
		if (lastFrameNum == (FRAMES_PER_BATCH - 1))
			batchCount++;

		if (batchCount > m_maxBatches)
			throw std::runtime_error("Message is too long, batch count exceeded.");

		output.clear();
		for (int i = 0; i < PREAMBLE_SIZE_BYTES; i++)
			output.push_back(PREAMBLE_SEQUENCE);



		bool addrIsSet = false;
		size_t offset = 0;
		//Batch enum
		for (size_t i = 0; i < batchCount; i++)
		{
			insert32bit(output, SYNC_CODEWORD); //Must be at the begining of every batch

			//Frame enum
			//Each frame consists of 2 codewords, 32 bit each
			for (size_t j = 0; j < FRAMES_PER_BATCH; j++)
			{
				bool addrFrame = false;
				if (!addrIsSet && j != addrFrameNum) //Skip frames untill address
				{
					for (size_t k = 0; k < CW_PER_FRAMES; k++)
						insert32bit(output, IDLE_CODEWORD); //Idle codeword means empty codeword part of frame
					continue;
				}
				else if (!addrIsSet) //Set address and begin set message codewords
				{
					insert32bit(output, MakeAddressCodeword(addr, func));

					if (msgType == Type::Numeric)
						insert32bit(output, MakeMessageCodeword(messageBitsN, offset, maxBits));
					else if (msgType == Type::Alphanumeric)
						insert32bit(output, MakeMessageCodeword(messageBitsA, offset, maxBits));
					else
						insert32bit(output, IDLE_CODEWORD);

					addrIsSet = true;
					addrFrame = true;

					continue;
				}

				else if (msgType == Type::Numeric)
				{
					for (size_t k = 0; k < CW_PER_FRAMES; k++)
						insert32bit(output, MakeMessageCodeword(messageBitsN, offset, maxBits));
				}
				else if (msgType == Type::Alphanumeric)
				{
					for (size_t k = 0; k < CW_PER_FRAMES; k++)
						insert32bit(output, MakeMessageCodeword(messageBitsA, offset, maxBits));
				}
				else //Tone messages have no content
				{
					for (size_t k = 0; k < CW_PER_FRAMES; k++)
						insert32bit(output, IDLE_CODEWORD);
				}
			}
		}

		if (rawPOCSAG)
			return output.size() * 8;

		std::vector<PCMSample_t> pcmSamples;
		_modulatePOCSAG(pcmSamples, output, uint16_t(bps));
		size_t sampleCount = pcmSamples.size();
		MakePCM(pcmSamples, output, m_sampleRate); //Clears output before produce PCM buffer in it
		return sampleCount;
	}
}