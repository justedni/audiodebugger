#pragma once

#include <vector>
#include <assert.h>

class AudioRingBuffer
{
public:
    explicit AudioRingBuffer(int oneBlockSize, int maxCapacity)
        : buffer(static_cast<std::size_t>(maxCapacity), 0.0f)
        , readBufferTemp(static_cast<std::size_t>(oneBlockSize), 0.0f)
    {
    }

    AudioRingBuffer(const AudioRingBuffer&) = delete;
    AudioRingBuffer(AudioRingBuffer&& other) = delete;
    AudioRingBuffer& operator=(const AudioRingBuffer&) = delete;

    void push(const float* data, int n)
    {
        const int capacity = buffer.size();
        if (n >= capacity)
        {
            std::copy(data + (n - capacity), data + n, buffer.begin());
            writeIndex = 0;
            return;
        }

        const int firstChunk = std::min(n, capacity - writeIndex);
        std::copy(data, data + firstChunk, buffer.begin() + writeIndex);

        const int secondChunk = n - firstChunk;
        if (secondChunk > 0)
            std::copy(data + firstChunk, data + n, buffer.begin());

        writeIndex = (writeIndex + n) % capacity;
    }

    void read(float* dest, int n, int offsetFromLatest) const
    {
        const int capacity = buffer.size();
        assert(n <= capacity);
        assert(offsetFromLatest <= 0);

        const int samplesBack = n - offsetFromLatest;
        assert(samplesBack <= capacity);

        int startIndex = writeIndex - samplesBack;
        while (startIndex < 0) startIndex += capacity;

        const int firstChunk = std::min(n, capacity - startIndex);
        std::copy_n(buffer.begin() + startIndex, firstChunk, dest);

        const int secondChunk = n - firstChunk;
        if (secondChunk > 0)
            std::copy_n(buffer.begin(), secondChunk, dest + firstChunk);
    }

    const std::vector<float>& readToTemp(int n, int offsetFromLatest)
    {
        read(readBufferTemp.data(), n, offsetFromLatest);
        return readBufferTemp;
    }

private:
    std::vector<float> buffer;
    int writeIndex = 0;

    std::vector<float> readBufferTemp;
};