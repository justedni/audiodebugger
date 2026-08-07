// Copyright (c) 2026 Vincent Dortel
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <iostream>
#include <fstream>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <cstring>
#include <memory>
#include <atomic>

#include "core/ReportFile.h"

namespace AudioDebug {

class AudioBlockQueue;
class BlockWriter;

class FileWriter {
public:
    FileWriter(const std::string& filename, const FileHeader& header);
    ~FileWriter();

    bool setPluginState(const void* data, size_t size, uint32_t stateVersion = 0, uint32_t pluginId = 0);
    bool hasPluginState() const { return !m_pluginState.empty(); }

    bool start();
    void stop();

    bool beginFrame(uint32_t numSamples) noexcept;
    void endFrame() noexcept;

    bool isFrameOpen() const noexcept { return m_frameOpen; }
    uint32_t currentFrameId() const noexcept { return m_currentFrameId; }
    uint32_t currentFrameSamples() const noexcept { return m_currentFrameSamples; }
    int64_t nextFrameStartSample() const noexcept { return m_nextFrameStartSample; }

    BlockWriter beginBlock(BlockType type, uint32_t blockId) noexcept;
    bool writeAudioBlockDirect(BlockType type, uint32_t blockId, int numSamples, const std::function<void(SampleWriter&)>& f);
    bool writeBlock(BlockType type, uint32_t blockId, const std::vector<uint8_t>& data);

    bool hasPendingWrites() const;
    FileHeader getCurrentHeader() const { return m_fileHeader; }
    const std::string& getFileName() const { return m_filename; }

    using BlockAddedFunc = std::function<void(uint32_t, const uint8_t*, size_t)>;
    void setBlockAddedCallback(BlockAddedFunc func) { m_blockAddedCallback = func; }

protected:
    friend class BlockWriter;
    bool finishBlock(bool committed, size_t size) noexcept;
    void abandonBlock() noexcept;

private:
    void drainRemaining();

    void writePluginStateBlock();
    void ensureFrameForBlock(uint32_t blockId) noexcept;

    void writerLoop();
    void writeBlockToFile(const struct WriteBlock& block);
    void updateBlockCountInFile();

    std::string m_filename;
    FileHeader m_fileHeader;
    size_t m_maxBlockBytes = 0;

    std::vector<uint8_t> m_pluginState;
    uint32_t m_pluginId = 0;

    std::ofstream m_file;

    std::thread m_writerThread;
    std::unique_ptr<AudioBlockQueue> m_queue;
    std::atomic<bool> m_running;

    struct WriteBlock* m_openSlot = nullptr;

    bool m_frameOpen = false;
    uint32_t m_currentFrameId = 0;
    uint32_t m_nextFrameId = 0;
    uint32_t m_currentFrameSamples = 0;
    int64_t m_nextFrameStartSample = 0;

    std::atomic<uint32_t> m_writtenFramesCount = 0;
    std::atomic<uint64_t> m_writtenSamplesCount = 0;
    uint32_t m_lastWrittenFrameCount = 0;
    uint64_t m_lastWrittenSampleCount = 0;
    bool m_pendingFlush = false;

    BlockAddedFunc m_blockAddedCallback;
};

} // namespace AudioDebug
