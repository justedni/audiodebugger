#include "core/FileWriter.h"

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
    AudioBlockQueue(size_t capacity, int maxBlockSize)
        : m_mask(capacity - 1)
        , m_blocks(capacity)
        , m_blockMaxSize(maxBlockSize)
    {
        assert((capacity & (capacity - 1)) == 0 && "capacity must be power of two");
        for (auto& b : m_blocks)
            b.data.resize(maxBlockSize);
    }

    void resizeIfNecessary(int requiredSize, WriteBlock* block)
    {
        if (requiredSize > m_blockMaxSize)
            m_blockMaxSize = requiredSize;

        // Ideally, the cache should always be ready in advance, but
        // in emergency worst case scenario, resize it (in audio thread)
        if (m_blockMaxSize > block->data.size())
        {
            block->data.resize(m_blockMaxSize);
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
    int m_blockMaxSize;
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
    int maxChannels = std::max(header.in_channels, header.out_channels);
    int maxSize = maxChannels * header.block_size * getBitSize((EBitDepth)header.bit_depth);
    m_queue.reset(new AudioBlockQueue(4, maxSize));
}

FileWriter::~FileWriter()
{
    stop();
}

bool FileWriter::start()
{
    m_file.open(m_filename, std::ios::binary | std::ios::in | std::ios::out | std::ios::trunc);
    if (!m_file.is_open()) {
        std::cerr << "Failed to open file: " << m_filename << std::endl;
        return false;
    }

    writeFileHeader(m_file, m_fileHeader);
    m_file.flush();

    m_running = true;
    m_writerThread = std::thread(&FileWriter::writerLoop, this);
    return true;
}

void FileWriter::stop()
{
    m_running = false;

    if (m_writerThread.joinable())
        m_writerThread.join();

    drainRemaining();

    if (m_file.is_open())
    {
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

void FileWriter::writeBlock(BlockType type, uint32_t blockId, const std::vector<uint8_t>& data)
{
    if (auto* slot = m_queue->beginWrite())
    {
        slot->type = type;
        slot->block_id = blockId;

        m_queue->resizeIfNecessary(data.size(), slot);

        std::memcpy(slot->data.data(), data.data(), data.size());
        slot->dataSize = data.size();

        if (m_blockAddedCallback)
            m_blockAddedCallback((uint32_t)type, slot->data.data(), slot->dataSize);

        m_queue->commitWrite();
    }
    else
    {
        assert(false);
    }
}

void FileWriter::writeAudioBlockDirect(BlockType type, uint32_t blockId, int numSamples, const std::function<void(SampleWriter&)>& f)
{
    if (auto* slot = m_queue->beginWrite())
    {
        slot->type = type;
        slot->block_id = blockId;

        const size_t requiredBytes = static_cast<size_t>(numSamples) * getBitSize((EBitDepth)m_fileHeader.bit_depth);
        m_queue->resizeIfNecessary(requiredBytes, slot);

        SampleWriter writer(slot->data.data(), slot->data.size(), m_fileHeader.bit_depth);
        f(writer);

        slot->dataSize = writer.bytesWritten();

        if (m_blockAddedCallback)
            m_blockAddedCallback((uint32_t)type, slot->data.data(), slot->dataSize);

        m_queue->commitWrite();
    }
    else
    {
        assert(false);
    }
}

bool FileWriter::hasPendingWrites() const
{
    return m_queue->hasPendingBlocks();
}

void FileWriter::writerLoop()
{
    while (m_running.load(std::memory_order_acquire))
    {
        if (auto* block = m_queue->beginRead())
        {
            writeBlockToFile(*block);
            m_queue->commitRead();
        }
        else
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

void FileWriter::writeBlockToFile(const WriteBlock& block)
{
    if (!m_file.is_open())
    {
        return;
    }

    BlockHeader header;
    header.block_type = static_cast<uint32_t>(block.type);
    header.data_size = static_cast<uint32_t>(block.dataSize);
    header.block_id = block.block_id;
    header.checksum = calculateChecksum(block.data);

    std::streampos end_pos = m_file.tellp();

    writeBlockHeader(m_file, header);
    m_file.write(reinterpret_cast<const char*>(block.data.data()), block.dataSize);

    if (block.type == BlockType::AUDIOOUT)
    {
        m_writtenAudioBlocksCount++;
        m_fileHeader.blocks_count = m_writtenAudioBlocksCount.load();

        constexpr std::streamoff total_blocks_offset = offsetof(FileHeader, blocks_count);

        m_file.seekp(total_blocks_offset);
        m_file.write(reinterpret_cast<const char*>(&m_fileHeader.blocks_count), sizeof(uint32_t));
        m_file.seekp(0, std::ios::end);
    }

    m_file.flush();
}

} // namespace AudioDebug
