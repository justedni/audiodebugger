#include "core/UnitTests.h"

#include "core/ReportFile.h"
#include "core/DebugProcessor.h"

#include "core/RingBuffer.h"
#include "core/DiffDetector.h" 

#include <assert.h>

void UnitTestData::getInputData(FileData& fileData, int currentPos, int samplesToRead, float** inputs)
{
	int inputId = fileData.fileId;
	std::ifstream& file = fileData.file;
	int startTime = fileData.startTime;
	int endTime = (fileData.duration >= 0)
		? (fileData.startTime + fileData.duration)
		: (currentPos + samplesToRead);

	int inputLeft = inputId * 2;
	int inputRight = inputLeft + 1;

	//assert(inputLeft >= 0 && inputRight < numChannelsIn);

	for (int i = 0; i < samplesToRead; ++i)
	{
		if (currentPos + i >= startTime && currentPos + i < endTime)
		{
			for (int j = 0; j < 2; j++)
			{
				// Read 3 bytes and construct a 32-bit integer
				uint8_t bytes[3];
				file.read(reinterpret_cast<char*>(bytes), 3);
				int32_t sample24 = (bytes[2] << 16) | (bytes[1] << 8) | bytes[0];
				float sample = short24_to_float(sample24);
				inputs[inputLeft + j][i] = sample;
			}
		}
	}
}

namespace UnitTests {

void read_file(const std::string& filePath, FileData& out_data)
{
	std::ifstream file(filePath, std::ios::in | std::ios::binary | std::ios::ate);

	if (!file.is_open())
	{
		__debugbreak();
		bool bStep = true;
	}

	assert(file.is_open());

	std::streamsize fileSize = file.tellg();
	file.seekg(0, std::ios::beg);

	const int headerSize = sizeof(WavFormat::WavHeader);
	std::vector<char> buffer(headerSize);
	auto& ret = file.read(buffer.data(), headerSize);
	assert(ret);

	out_data.totalSize = fileSize;
	out_data.file = std::move(file);
	out_data.headerBuffer = std::move(buffer);
}


bool compare_binaries(const std::string& in_leftFilePath, const std::string& in_rightFilePath)
{
	auto bSuccess = [&]()
	{
		std::ifstream fileA(in_leftFilePath, std::ios::in | std::ios::binary | std::ifstream::ate);
		std::ifstream fileB(in_rightFilePath, std::ios::in | std::ios::binary | std::ifstream::ate);

		if (fileA.fail() || fileB.fail())
			return false;

		if (fileA.tellg() != fileB.tellg())
			return false;

		fileA.seekg(0, std::ifstream::beg);
		fileB.seekg(0, std::ifstream::beg);
		auto bAreEqual = std::equal(std::istreambuf_iterator<char>(fileA.rdbuf()),
			std::istreambuf_iterator<char>(),
			std::istreambuf_iterator<char>(fileB.rdbuf()));

		return bAreEqual;
	}();

	return bSuccess;
}

bool compare_binaries_verbose(const std::string& testName, const std::string& in_leftFilePath, const std::string& in_rightFilePath)
{
	std::cout << "Unit Test: " << testName << "...\n";
	std::cout << "Left: " << in_leftFilePath << " - Right: " << in_rightFilePath << "\n";

	FileData fileData[2];
	UnitTests::read_file(in_leftFilePath, fileData[0]);
	UnitTests::read_file(in_rightFilePath, fileData[1]);

	const auto* wavHeader1 = reinterpret_cast<const WavFormat::WavHeader*>(fileData[0].headerBuffer.data());
	int bytesPerSample = wavHeader1->bitDepth / 8;
	int totalSamples = wavHeader1->dataChunkSize / bytesPerSample / 2;

	int32 remainingSamples = totalSamples;
	int32 sampleId = 0;
	int errorsCount = 0;

	int currentLeftFlags[2] = { 0, 0 };
	int currentRightFlags[2] = { 0, 0 };
	int identicalFlagsCount[2] = { 0, 0 };
	int rangeSampleStart[2] = { 0, 0 };

	auto printError = [&](auto chan, auto sampleId, auto count)
	{
		auto rangeTimeStart = rangeSampleStart[chan] / wavHeader1->sampleRate;
		auto rangeTimeEnd = sampleId / wavHeader1->sampleRate;

		char buf[1024];
		sprintf_s(buf, "[%d-%d][%d-%dsec] Chan %d - Left flags: %d - Right flags: %d [%d samples]\n",
			rangeSampleStart[chan], sampleId, rangeTimeStart, rangeTimeEnd, chan, currentLeftFlags[chan], currentRightFlags[chan], count);
		std::cout << std::string(buf);
		//std::cout << std::format("[{}][{}sec] Left:{}\n", sampleId, time);
	};

	while (remainingSamples > 0)
	{
		//unitTest.getInputData(iFileId, leftFileData.file, 0, readSamples, samplesToRead);

		for (int j = 0; j < 2; j++)
		{
			uint8_t bytes[3];

			fileData[0].file.read(reinterpret_cast<char*>(bytes), 3);
			int32_t sample24_First = (bytes[2] << 16) | (bytes[1] << 8) | bytes[0];

			fileData[1].file.read(reinterpret_cast<char*>(bytes), 3);
			int32_t sample24_Second = (bytes[2] << 16) | (bytes[1] << 8) | bytes[0];

			int8_t leftFlags = sample24_First & 0xFF;
			int8_t rightFlags = sample24_Second & 0xFF;

			if (sample24_First != sample24_Second)
			{
				if (errorsCount == 417)
				{
					bool bStep = true;
				}

				if (identicalFlagsCount[j] == 0)
				{
					currentLeftFlags[j] = leftFlags;
					currentRightFlags[j] = rightFlags;
					rangeSampleStart[j] = sampleId;
					identicalFlagsCount[j]++;
				}
				else if (currentLeftFlags[j] == leftFlags && currentRightFlags[j] == rightFlags)
				{
					identicalFlagsCount[j]++;
				}
				else
				{
					printError(j, sampleId, identicalFlagsCount[j]);

					currentLeftFlags[j] = leftFlags;
					currentRightFlags[j] = rightFlags;
					identicalFlagsCount[j] = 0;
				}

				errorsCount++;
			}
			else if (identicalFlagsCount[j] > 0)
			{
				printError(j, sampleId, identicalFlagsCount[j]);
				identicalFlagsCount[j] = 0;
			}
		}

		remainingSamples--;
		sampleId++;
	}

	for (int j = 0; j < 2; j++)
	{
		if (identicalFlagsCount[j] > 0)
		{
			printError(j, totalSamples - 1, identicalFlagsCount[j]);
			identicalFlagsCount[j] = 0;
		}
	}

	if (errorsCount == 0)
		std::cout << "\tSuccess!\n";
	else
	{
		std::cerr << "\tErrors:" << std::to_string(errorsCount) << "!\n";
#ifdef _DEBUG
		//assert(bSuccess, "Unit test failed!");
#endif
	}

	return (errorsCount == 0);
}


using ChanStates = std::array<AudioDebug::ChannelReaderState, 3>;

enum ESampleSlot : uint8_t
{
	In = 0,
	Out,
	Ref,
	Count
};

void processAudioBlock(ChanStates& states, AudioDebug::ReadBlock& block, AudioDebug::EBitDepth bitDepth, int numChannels, int32_t& out_audioSamplesProcessed)
{
	using namespace AudioDebug;

	if (block.data.empty())
		return;

	assert(block.type == BlockType::AUDIOIN || block.type == BlockType::AUDIOOUT);
	auto& state = (block.type == BlockType::AUDIOIN) ? states[ESampleSlot::In] : states[ESampleSlot::Out];

	int64_t startSample = state.samplesCount.load();

	auto samples = block.as_samples(bitDepth);

	int remainingSamples = samples.size();
	const int numberOfSamplesPerBlock = remainingSamples / (numChannels);
	auto it = samples.begin();
	int outSampleCount = 0;

	assert(numberOfSamplesPerBlock <= state.m_tempBuffer[0].size());

	while (remainingSamples > 0)
	{
		assert(it != samples.end());

		for (int iChan = 0; iChan < numChannels; iChan++)
		{
			int32_t sampleInt = *it;
			float sample = sampleInt / 8388608.0f;

			state.m_tempBuffer[iChan][outSampleCount] = sample;

			it++;
			remainingSamples--;
		}

		outSampleCount++;
	}

	while (outSampleCount < state.m_tempBuffer[0].size())
	{
		for (int iChan = 0; iChan < numChannels; iChan++)
		{
			state.m_tempBuffer[iChan][outSampleCount] = 0.0f;
		}

		outSampleCount++;
	}

	state.samplesCount.store(startSample + numberOfSamplesPerBlock);

	out_audioSamplesProcessed = numberOfSamplesPerBlock;
}

bool processOneBlock(AudioDebug::ReportFile* file, ChanStates& states, AudioDebug::ReadBlock& block, bool bProcessAudioIn, int32_t& out_audioSamplesProcessed)
{
	using namespace AudioDebug;

	uint32_t data_size = 0;
	bool bSuccess = file->readFileBlock(block, data_size);
	if (!bSuccess)
	{
		// Stop at EOF
		return false;
	}
	else
	{
		auto& header = file->getHeader();

		if (block.type == BlockType::AUDIOIN)
		{
			if (bProcessAudioIn)
				processAudioBlock(states, block, (AudioDebug::EBitDepth)header.bit_depth, header.in_channels, out_audioSamplesProcessed);
		}
		else if (block.type == BlockType::AUDIOOUT)
		{
			processAudioBlock(states, block, (AudioDebug::EBitDepth)header.bit_depth, header.out_channels, out_audioSamplesProcessed);
		}
	}

	return true;
}

UnitTestProcessor::UnitTestProcessor()
{
	m_diffDetector = std::make_unique<AudioDebug::DiffDetector>();
}

UnitTestProcessor::~UnitTestProcessor()
{
}

const std::vector<AudioDebug::DifferenceRange>& UnitTestProcessor::getDifferences() const
{
	return m_diffDetector->getAll();
}

void UnitTestProcessor::processUnitTestFromRpt(const TestDesc& montage, uint64_t totalDuration, const std::string& rptFilePath)
{
	m_rptFile = std::make_unique<AudioDebug::ReportFile>(AudioDebug::ReportFile::EFileSlot::Rpt, rptFilePath);
	auto ret = m_rptFile->openFile(false);
	assert(ret);

	auto outRptPath = rptFilePath;
	auto pos = outRptPath.find(".rpt", outRptPath.size() - 5);
	if (pos != std::string::npos) {
		outRptPath.replace(pos, outRptPath.length(), "_temp.rpt");
	}

	const auto& header = m_rptFile->getHeader();
	m_rptFile->initChanStates(true, header.in_channels, header.out_channels, header.block_size);

	initProcessor(montage, outRptPath, header.in_channels, header.out_channels);

	//processor->prepareToPlay(header.sample_rate, header.block_size);

	if (totalDuration == 0)
		totalDuration = header.block_size * header.blocks_count;

	regenerateRpt(totalDuration);
}

int UnitTestProcessor::getSampleRate() const
{
	assert(m_rptFile);
	const auto& header = m_rptFile->getHeader();
	return header.sample_rate;
}

void UnitTestProcessor::initProcessor(const TestDesc& montage, const std::string& outRptFilePath, int inChannelsCount, int outChannelsCount)
{
	assert(!processor);
	processor.reset(createProcessor());
	assert(processor);

	initializeUnitTest(montage, inChannelsCount, outChannelsCount);

	auto& pDebugger = processor->getDebugger();
	pDebugger.setNumInputsOutputs(inChannelsCount, outChannelsCount);

	pDebugger.setRptFileMaxLength(-1);
	pDebugger.resetWriter();
	pDebugger.setCaptureName(outRptFilePath, false);
	pDebugger.setAutoWriteOutputFile(true);
}

// Copied/adapted from WaveformOverview::updateRegenerateThread
void UnitTestProcessor::regenerateRpt(uint64_t totalDuration)
{
	using namespace AudioDebug;

	auto& pDebugger = processor->getDebugger();
	//pDebugger->setSampleToDebug(sampleItToDebug);

	ReadBlock block;
	ReadBlock genBlock;

	m_rptFile->seekToDataStart();

	m_rptFile->onMarker = [this](Marker&& m) {};
	//m_rptFile->onMidi = [this](int64_t ts, std::vector<RawMidiMessage> msgs) {};
	m_rptFile->onAudioBlock = [](auto globalChanId, float value, int64_t blockStartSample, int blockSize) {};

	auto& header = m_rptFile->getHeader();
	const int numInChannels = header.in_channels;
	const int numOutChannels = header.out_channels;

	std::vector<float*> tempInPtrs;
	auto& tempIn = m_rptFile->getInTempBuffer();
	tempInPtrs.reserve(tempIn.size());
	for (const auto& buff : tempIn)
		tempInPtrs.push_back(const_cast<float*>(buff.data()));

	std::vector<AudioDebug::RawMidiMessage> unusedMidiMessages;

	std::string genFileName;
	pDebugger.startRegenerate(header, processor.get(), genFileName);

	processor->prepareToPlayHook(header.sample_rate, header.block_size);

	std::unique_ptr<ReportFile> m_genFile = std::make_unique<ReportFile>(AudioDebug::ReportFile::EFileSlot::Gen, genFileName);

	const int latencySamples = processor->getLatencySamples(header.block_size);

	AudioRingBuffer** refOutChannelsCache = new AudioRingBuffer * [numOutChannels];
	AudioRingBuffer** genOutChannelsCache = new AudioRingBuffer * [numOutChannels];
	for (int i = 0; i < numOutChannels; i++)
	{
		refOutChannelsCache[i] = new AudioRingBuffer(header.block_size, header.block_size + latencySamples);
		genOutChannelsCache[i] = new AudioRingBuffer(header.block_size, header.block_size + latencySamples);
	}

	EBitDepth bitDepth = (EBitDepth)header.bit_depth;

	m_diffDetector->startDetection(header.out_channels);


	pDebugger.setBlockAddedCallback([&](auto type, const auto* bytes, auto size)
	{
		ReadBlock tempBlock;
		tempBlock.type = static_cast<BlockType>(type);
		tempBlock.data.resize(size);
		// TODO: can be done better than copying the whole thing
		std::memcpy(tempBlock.data.data(), bytes, size);

		m_genFile->simulateReadBlock(tempBlock, EProcessType::Normal);

		if (tempBlock.type == BlockType::AUDIOOUT)
		{
			auto samples = tempBlock.as_samples((EBitDepth)header.bit_depth);
			int remainingSamples = samples.size();
			const int numberOfSamplesPerBlock = remainingSamples / (header.out_channels);
			auto it = samples.begin();

			assert(numberOfSamplesPerBlock <= header.block_size);

			while (remainingSamples > 0)
			{
				assert(it != samples.end());

				for (int iChan = 0; iChan < header.out_channels; iChan++)
				{
					int32_t sampleInt = *it;
					float sample = AudioDebugger::intToFloat(sampleInt, header.bit_depth);

					genOutChannelsCache[iChan]->push(&sample, 1);

					it++;
					remainingSamples--;
				}
			}

			for (int chan = 0; chan < numOutChannels; chan++)
			{
				const auto& originalBuffer = refOutChannelsCache[chan]->readToTemp(numberOfSamplesPerBlock, -latencySamples);
				const auto& generatedBuffer = genOutChannelsCache[chan]->readToTemp(numberOfSamplesPerBlock, 0);
				m_diffDetector->detectDifferences(originalBuffer, generatedBuffer, chan, header.bit_depth);
			}
		}
	});


	auto ret = m_genFile->openFile(true);
	assert(ret);

	bool rptFileEnded = false;

	int64_t samplesProcessed = 0;

	m_genFile->seekToDataStart();
	m_genFile->onMarker = [this](Marker&& m) {};
	m_genFile->onAudioBlock = [](auto globalChanId, float value, int64_t blockStartSample, int blockSize) {};
	m_genFile->initChanStates(false, header.in_channels, header.out_channels, header.block_size);

	int currentBlockId = 0;

	auto processBuffer = [&]()
	{
		processor->processBlockHook(header.block_size, tempInPtrs.data(), unusedMidiMessages);
		samplesProcessed += header.block_size;
	};

	while (true)
	{
		BlockHeader nextHeader;
		if (m_rptFile->peekNextBlockHeader(nextHeader) && nextHeader.block_id != currentBlockId)
		{
			processBuffer();
			currentBlockId = nextHeader.block_id;
		}

		int32_t processedThisBlock = 0;
		if (m_rptFile->processOneBlock(block, EProcessType::DryRun, true, processedThisBlock) == EProcessRet::Stop)
		{
			rptFileEnded = true;
			processBuffer();
		}

		if (block.type == BlockType::AUDIOIN)
		{
		}
		else if (block.type == BlockType::AUDIOOUT)
		{
			for (int i = 0; i < numOutChannels; i++)
			{
				const auto& sourceBuff = m_rptFile->getOutTempBuffer()[i];
				refOutChannelsCache[i]->push(sourceBuff.data(), sourceBuff.size());
			}
		}

		if (rptFileEnded)
		{
			pDebugger.waitUntilWriterHasFinished();
			break;
		}
	}

	pDebugger.endRegenerate();

	for (int i = 0; i < numOutChannels; i++)
	{
		delete refOutChannelsCache[i];
		delete genOutChannelsCache[i];
	}
	delete[] refOutChannelsCache;
	delete[] genOutChannelsCache;

	processor->releaseResourcesHook();
}

} // namespace UnitTests
