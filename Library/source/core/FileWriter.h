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

class FileWriter {
public:
    FileWriter(const std::string& filename, const FileHeader& header);
    ~FileWriter();

    bool start();
    void stop();

    void writeAudioBlockDirect(BlockType type, uint32_t blockId, int numSamples, const std::function<void(SampleWriter&)>& f);

    void writeBlock(BlockType type, uint32_t blockId, const std::vector<uint8_t>& data);

    bool hasPendingWrites() const;
    FileHeader getCurrentHeader() const { return m_fileHeader; }
    const std::string& getFileName() const { return m_filename; }

    using BlockAddedFunc = std::function<void(uint32_t, const uint8_t*, size_t)>;
    void setBlockAddedCallback(BlockAddedFunc func) { m_blockAddedCallback = func; }
private:
    void drainRemaining();

    void writerLoop();
    void writeBlockToFile(const struct WriteBlock& block);

    std::string m_filename;
    FileHeader m_fileHeader;
    std::ofstream m_file;

    std::thread m_writerThread;
    std::unique_ptr<AudioBlockQueue> m_queue;
    std::atomic<bool> m_running;

    std::atomic<uint32_t> m_writtenAudioBlocksCount = 0;

    BlockAddedFunc m_blockAddedCallback;
};

} // namespace AudioDebug
