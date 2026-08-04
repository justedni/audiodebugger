// Copyright (c) 2026 Vincent Dortel
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/FileWriter.h"
#include "core/BlockWriter.h"

#include <assert.h>

namespace AudioDebug {

struct WriteBlock
{
    BlockType type;
    uint32_t block_id;
    std::vector<uint8_t> data;
    uint32_t dataSize;
};

class AudioBlockQueue
{
public:
    AudioBlockQueue(size_t capacity, size_t maxBlockSize)
        : m_mask(capacity - 1)
        , m_blocks(capacity)
    {
        assert(capacity >= 2 && (capacity & (capacity - 1)) == 0 && "capacity must be a power of two");

        for (auto& b : m_blocks)
        {
            b.data.resize(maxBlockSize);
            b.dataSize = 0;
            b.type = BlockType::INVALID;
            b.block_id = 0;
        }
    }

    WriteBlock* beginWrite() noexcept
    {
        const auto w = m_writeIdx.load(std::memory_order_relaxed);
        const auto r = m_readIdx.load(std::memory_order_acquire);
        if (w - r == m_blocks.size())
            return nullptr;
        return &m_blocks[w & m_mask];
    }

    void commitWrite() noexcept
    {
        m_writeIdx.store(m_writeIdx.load(std::memory_order_relaxed) + 1, std::memory_order_release);
    }

    WriteBlock* beginRead() noexcept
    {
        const auto r = m_readIdx.load(std::memory_order_relaxed);
        const auto w = m_writeIdx.load(std::memory_order_acquire);
        if (r == w)
            return nullptr;
        return &m_blocks[r & m_mask];
    }

    void commitRead() noexcept
    {
        m_readIdx.store(m_readIdx.load(std::memory_order_relaxed) + 1, std::memory_order_release);
    }

    bool hasPendingBlocks() const noexcept
    {
        const auto r = m_readIdx.load(std::memory_order_acquire);
        const auto w = m_writeIdx.load(std::memory_order_acquire);
        return r != w;
    }

private:
    const size_t m_mask;
    std::vector<WriteBlock> m_blocks;
    alignas(64) std::atomic<size_t> m_writeIdx{ 0 };
    alignas(64) std::atomic<size_t> m_readIdx{ 0 };
};

uint8_t getBitSize(EBitDepth bitDepth)
{
    switch (bitDepth)
    {
    case EBitDepth::Int24: return 3;
    case EBitDepth::Int32: return sizeof(uint32_t);
    default:
        assert(false); // Not implemented case?
    }

    return sizeof(uint32_t);
}

FileWriter::FileWriter(const std::string& filename, const FileHeader& header)
    : m_filename(filename)
    , m_fileHeader(header)
    , m_running(false)
{
    const uint32_t maxChannels = std::max(header.in_channels, header.out_channels);
    const size_t audioWorstCase = static_cast<size_t>(maxChannels)
                                * static_cast<size_t>(header.block_size)
                                * getBitSize(static_cast<EBitDepth>(header.bit_depth));

    m_maxBlockBytes = std::max(audioWorstCase, FileWriterConfig::maxBlockBytes);

    m_queue.reset(new AudioBlockQueue(FileWriterConfig::queueCapacity, m_maxBlockBytes));
}

FileWriter::~FileWriter()
{
    stop();
}

bool FileWriter::start()
{
    m_file.open(m_filename, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!m_file.is_open()) {
        std::cerr << "Failed to open file: " << m_filename << std::endl;
        return false;
    }

    m_writtenAudioBlocksCount.store(0, std::memory_order_relaxed);
    m_lastWrittenBlockCount = 0;
    m_pendingFlush = false;

    writeFileHeader(m_file, m_fileHeader);
    m_file.flush();

    m_running.store(true, std::memory_order_release);
    m_writerThread = std::thread(&FileWriter::writerLoop, this);
    return true;
}

void FileWriter::stop()
{
    m_running.store(false, std::memory_order_release);

    if (m_writerThread.joinable())
        m_writerThread.join();

    drainRemaining();

    if (m_file.is_open())
    {
        updateBlockCountInFile();
        m_file.flush();
        m_file.close();
    }
}

void FileWriter::drainRemaining()
{
    while (auto* block = m_queue->beginRead())
    {
        writeBlockToFile(*block);
        m_queue->commitRead();
    }
}

BlockWriter FileWriter::beginBlock(BlockType type, uint32_t blockId) noexcept
{
    assert(m_openSlot == nullptr && "a BlockWriter is already open - commit it first");

    auto* slot = m_queue->beginWrite();
    if (slot == nullptr)
    {
        assert(false);
        return BlockWriter{};
    }

    slot->type     = type;
    slot->block_id = blockId;
    slot->dataSize = 0;

    m_openSlot = slot;

    return BlockWriter(this, slot->data.data(), slot->data.size(), m_fileHeader.bit_depth);
}

bool FileWriter::finishBlock(bool committed, size_t size) noexcept
{
    auto* slot = m_openSlot;
    m_openSlot = nullptr;

    if (slot == nullptr)
    {
        assert(false);
        return false;
    }

    if (!committed)
    {
        assert(false);
        return false;
    }

    slot->dataSize = static_cast<uint32_t>(size);

    if (m_blockAddedCallback)
        m_blockAddedCallback(static_cast<uint32_t>(slot->type), slot->data.data(), slot->dataSize);

    m_queue->commitWrite();
    return true;
}

void FileWriter::abandonBlock() noexcept
{
    if (m_openSlot != nullptr)
    {
        m_openSlot = nullptr;
    }
}

bool FileWriter::writeBlock(BlockType type, uint32_t blockId, const std::vector<uint8_t>& data)
{
    auto block = beginBlock(type, blockId);
    if (!block)
    {
        assert(false);
        return false;
    }

    block.append(data.data(), data.size());
    return block.commit();
}

bool FileWriter::writeAudioBlockDirect(BlockType type,
                                       uint32_t blockId,
                                       int numSamples,
                                       const std::function<void(SampleWriter&)>& f)
{
    const size_t requiredBytes = static_cast<size_t>(numSamples)
                               * getBitSize(static_cast<EBitDepth>(m_fileHeader.bit_depth));

    if (requiredBytes > m_maxBlockBytes)
    {
        assert(false && "audio block larger than maxBlockBytes - raise FileWriterConfig::maxBlockBytes");
        return false;
    }

    auto block = beginBlock(type, blockId);
    if (!block)
        return false;

    block.writeSamples(f);
    return block.commit();
}

bool FileWriter::hasPendingWrites() const
{
    return m_queue->hasPendingBlocks();
}

void FileWriter::writerLoop()
{
    auto lastFlush = std::chrono::steady_clock::now();

    while (m_running.load(std::memory_order_acquire))
    {
        bool bWorkWasDone = false;

        while (auto* block = m_queue->beginRead())
        {
            writeBlockToFile(*block);
            m_queue->commitRead();
            bWorkWasDone = true;
        }

        const auto now = std::chrono::steady_clock::now();
        if (m_pendingFlush && (now - lastFlush) >= FileWriterConfig::flushInterval)
        {
            updateBlockCountInFile();
            m_file.flush();
            m_pendingFlush = false;
            lastFlush = now;
        }

        if (!bWorkWasDone)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void FileWriter::writeBlockToFile(const WriteBlock& block)
{
    if (!m_file.is_open())
        return;

    BlockHeader header;
    header.block_type = static_cast<uint32_t>(block.type);
    header.data_size = block.dataSize;
    header.block_id = block.block_id;
    header.checksum = calculateChecksum(block.data.data(), block.dataSize);

    writeBlockHeader(m_file, header);
    m_file.write(reinterpret_cast<const char*>(block.data.data()), static_cast<std::streamsize>(block.dataSize));

    if (block.type == BlockType::AUDIOOUT)
        m_writtenAudioBlocksCount.fetch_add(1, std::memory_order_relaxed);

    m_pendingFlush = true;
}

void FileWriter::updateBlockCountInFile()
{
    if (!m_file.is_open())
        return;

    const uint32_t count = m_writtenAudioBlocksCount.load(std::memory_order_relaxed);
    if (count == m_lastWrittenBlockCount)
        return;

    m_fileHeader.blocks_count = count;

    constexpr std::streamoff total_blocks_offset = offsetof(FileHeader, blocks_count);

    const std::streampos end_pos = m_file.tellp();
    m_file.seekp(total_blocks_offset);
    m_file.write(reinterpret_cast<const char*>(&m_fileHeader.blocks_count), sizeof(uint32_t));
    m_file.seekp(end_pos);

    m_lastWrittenBlockCount = count;
}
} // namespace AudioDebug
