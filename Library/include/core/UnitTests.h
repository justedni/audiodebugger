#include <iostream>
#include <sstream>
#include <fstream>
#include <string>
#include <vector>
#include <tuple>
#include <chrono>

using int32 = int32_t;
using uint64 = uint64_t;
typedef float Sample32;

namespace UnitTests
{
	class RegenerationCache;
}

namespace AudioDebug
{
	class DebugProcessor;
	class ReportFile;
	class DiffDetector;
	struct DifferenceRange;
}

#ifdef __GNUC__
#define PACK( __Declaration__ ) __Declaration__ __attribute__((__packed__))
#endif

#ifdef _MSC_VER
#define PACK( __Declaration__ ) __pragma( pack(push, 1) ) __Declaration__ __pragma( pack(pop))
#endif

//------------------------------------------------------------------------
namespace WavFormat {

constexpr static char kRIFF[4] = { 'R', 'I', 'F', 'F' };
constexpr static char kWAVE[4] = { 'W', 'A', 'V', 'E' };
constexpr static char kfmt[4] = { 'f', 'm', 't', ' ' };

PACK(struct WavHeader
{
	// RIFF header
	char tagRIFF[4];
	uint32_t wavSize;
	char tagWave[4];

	// Fmt header
	char tagFmt[4];
	uint32_t fmtChunkSize;
	short audioFormat;
	short numChannels;
	uint32_t sampleRate;
	uint32_t byteRate;
	short sampleAlignment;
	short bitDepth;

	// Data
	char tagData[4];
	uint32_t dataChunkSize;
});
static_assert(sizeof(WavHeader) == 44, "");

} // namespace WavFormat


//------------------------------------------------------------------------

struct FileData
{
	int fileId = -1;
	std::streamsize totalSize;
	std::ifstream file;
	std::vector<char> headerBuffer;
	uint64 startTime = 0;
	int64_t duration = -1;
};

class UnitTestData
{
public:
	UnitTestData(int in_numSamples, int32 in_lNumChannelsInput, int32 in_numChannelsOut)
		: numSamples(in_numSamples)
		, numChannelsIn(in_lNumChannelsInput)
		, numChannelsOut(in_numChannelsOut)
	{
		inputs = new float* [numChannelsIn];
		for (int i = 0; i < numChannelsIn; i++)
		{
			inputs[i] = new float[numSamples];
		}

		outputs = new float* [numChannelsOut];
		for (int i = 0; i < numChannelsOut; i++)
		{
			outputs[i] = new float[numSamples];
		}
	}

	~UnitTestData()
	{
		for (int i = 0; i < numChannelsIn; i++)
		{
			delete[] inputs[i];
		}
		delete[] inputs;

		for (int i = 0; i < numChannelsOut; i++)
		{
			delete[] outputs[i];
		}
		delete[] outputs;
	}

	static void fix_short24_sign(int32_t& sample)
	{
		// Handle negative numbers
		if (sample & 0x800000) {
			sample |= 0xFF000000;
		}
	}

	static float short24_to_float(int32_t sample)
	{
		fix_short24_sign(sample);
		float conv = sample / 8388608.0f;  // Normalize to [-1.0, 1.0]
		return conv;
	}

	int32_t float_to_short24(float sample)
	{
		// Clamp the sample to [-1.0, 1.0]
		float clamped = std::max(-1.0f, std::min(1.0f, sample));

		// Convert to 24-bit integer
		int32_t sample24 = static_cast<int32_t>(clamped * 8388608.0f); // 2^23 = 8388608
		return sample24;
	}

	static void getInputData(FileData& fileData, int currentPos, int samplesToRead, float** inputs);

	void fillAllInputData(std::vector<FileData>& inputFiles, int32 readSamples, int32 samplesToRead)
	{
		eraseInputData(samplesToRead);

		//for (int iFileId = 0; iFileId < inputFiles.size(); iFileId++)
		for (auto& inputFileData : inputFiles)
		{
			getInputData(inputFileData, readSamples, samplesToRead, inputs);
		}
	}

	void eraseInputData(int32 samplesToRead)
	{
		for (int iChannel = 0; iChannel < numChannelsIn; iChannel++)
		{
			memset(inputs[iChannel], 0, samplesToRead * sizeof(float));
		}

		//for (auto& inputFileData : inputFiles)
		//{
		//	int inputLeft = inputFileData.fileId * 2;
		//	int inputRight = inputLeft + 1;

		//	memset(inputs[inputLeft], 0, samplesToRead * sizeof(float));
		//	memset(inputs[inputRight], 0, samplesToRead * sizeof(float));
		//}
	}

	void writeInt24(std::ofstream& os, int32_t sample24)
	{
		uint8_t bytes[3];
		bytes[0] = sample24 & 0xFF;
		bytes[1] = (sample24 >> 8) & 0xFF;
		bytes[2] = (sample24 >> 16) & 0xFF;

		os.write(reinterpret_cast<const char*>(bytes), 3);
	}

	void writeOutput(std::ofstream& out_stream, int samplesToRead)
	{
		for (int i = 0; i < samplesToRead; ++i)
		{
			for (int j = 0; j < 2; j++)
			{
				float outSample = outputs[j][i];
				int32_t sample24 = float_to_short24(outSample);

				// Write the 24-bit sample as 3 bytes
				writeInt24(out_stream, sample24);
			}
		}
	}

	float** inputs = nullptr;
	float** outputs = nullptr;

private:
	const int numSamples = 0;
	const int32 numChannelsIn;
	const int32 numChannelsOut;
};

namespace UnitTests {

void read_file(const std::string& filePath, FileData& out_data);
bool compare_binaries(const std::string& in_leftFilePath, const std::string& in_rightFilePath);
bool compare_binaries_verbose(const std::string& testName, const std::string& in_leftFilePath, const std::string& in_rightFilePath);


struct TestDesc
{
	TestDesc(std::string&& inName, std::string&& inBaseName)
		: name(std::move(inName))
		, baseName(std::move(inBaseName))
	{}

	std::string name;
	std::string baseName;
};

class UnitTestProcessor
{
public:
	UnitTestProcessor();
	~UnitTestProcessor();

	const std::vector<AudioDebug::DifferenceRange>& getDifferences() const;

	virtual AudioDebug::DebugProcessor* createProcessor() = 0;
	virtual void initializeUnitTest(const TestDesc& montage, int inChannelsCount, int outChannelsCount) = 0;

	void initProcessor(const TestDesc& montage, const std::string& outRptFilePath, int inChannelsCount, int outChannelsCount);
	void processUnitTestFromRpt(const TestDesc& montage, uint64_t totalDuration, const std::string& rptFilePath);

	int getSampleRate() const;

private:
	void regenerateRpt(uint64_t totalDuration);

protected:
	std::unique_ptr<AudioDebug::ReportFile> m_rptFile;
	std::unique_ptr<AudioDebug::DebugProcessor> processor = nullptr;
	std::unique_ptr<AudioDebug::DiffDetector> m_diffDetector;
};

} // namespace UnitTests
