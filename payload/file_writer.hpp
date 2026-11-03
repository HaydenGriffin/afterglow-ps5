// Afterglow - Writes to the song files on a thread of its own.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// USB sticks stall for hundreds of milliseconds at a time. Audio has to be taken from the
// client as it comes due, so a stall must never hold that up: it uses up some of the lead
// instead. Writes and cuts land in the order they were queued, and neighbouring writes are
// joined, since a stick takes one large write far better than many small ones.
#pragma once
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <ctime>
#include <deque>
#include <mutex>
#include <pthread.h>
#include <sys/types.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace afterglow::usbflac
{
class FileWriter
{
  public:
    using WriteFn = ssize_t (*)(int, const void *, std::size_t, off_t);
    using CutFn = int (*)(int, off_t);
    static constexpr std::size_t kBatchBytes = 192 * 1024;       // About a second of audio.
    static constexpr std::size_t kLargestWrite = 512 * 1024;     // Joined writes stop here.
    static constexpr std::size_t kMostQueued = 16 * 1024 * 1024; // About 80 s of audio.
    static constexpr std::int64_t kBatchWaitUs = 1000000;

    explicit FileWriter(WriteFn write = ::pwrite, CutFn cut = ::ftruncate)
        : write_(write), cut_(cut)
    {
    }
    ~FileWriter()
    {
        stop();
    }
    FileWriter(const FileWriter &) = delete;
    FileWriter &operator=(const FileWriter &) = delete;

    bool start()
    {
        stopping_ = false;
        started_ = pthread_create(&thread_, nullptr, entry, this) == 0;
        return started_;
    }
    // Everything queued is written first.
    void stop()
    {
        if (!started_)
            return;
        stopping_ = true;
        pthread_join(thread_, nullptr);
        started_ = false;
    }
    // Fails only after a write has failed.
    bool queue(int fd, std::uint64_t offset, const std::uint8_t *data, std::size_t size)
    {
        if (error_.load())
            return false;
        Job job{fd, false, offset, now(), std::vector<std::uint8_t>(data, data + size)};
        std::lock_guard<std::mutex> lock(mutex_);
        queued_ += size;
        jobs_.push_back(std::move(job));
        return true;
    }
    // Sets a file's length, after everything already queued has landed. It does not wait for a
    // batch to fill: the player may be about to reach the end of the file.
    bool queue_cut(int fd, std::uint64_t length)
    {
        if (error_.load())
            return false;
        Job job{fd, true, length, now(), {}};
        std::lock_guard<std::mutex> lock(mutex_);
        ++cuts_;
        jobs_.push_back(std::move(job));
        return true;
    }
    // Waits until everything queued is in the file.
    void flush()
    {
        flushing_.fetch_add(1);
        while ((queued_.load() || cuts_.load()) && !error_.load())
            usleep(1000);
        flushing_.fetch_sub(1);
    }
    bool backlogged() const
    {
        return queued_.load() >= kMostQueued;
    }
    std::size_t queued_bytes() const
    {
        return queued_.load();
    }
    int error() const
    {
        return error_.load();
    }
    void clear_error()
    {
        error_ = 0;
    }
    // The longest write since this was last asked, in seconds.
    double take_longest_write()
    {
        return static_cast<double>(longest_us_.exchange(0)) / 1e6;
    }
    std::uint64_t writes() const
    {
        return writes_.load();
    }

  private:
    struct Job
    {
        int fd;
        bool cut;
        std::uint64_t offset;
        std::int64_t queued_at;
        std::vector<std::uint8_t> bytes;
    };
    static std::int64_t now()
    {
        timespec value{};
        clock_gettime(CLOCK_MONOTONIC, &value);
        return static_cast<std::int64_t>(value.tv_sec) * 1000000 + value.tv_nsec / 1000;
    }
    static void *entry(void *self)
    {
        static_cast<FileWriter *>(self)->run();
        return nullptr;
    }
    // Takes the next cut, or run of neighbouring writes, once it is worth doing. False if not yet.
    bool take(Job &job)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (jobs_.empty())
            return false;
        const bool hurry = stopping_.load() || flushing_.load() > 0 || cuts_.load() > 0;
        if (!hurry && queued_.load() < kBatchBytes &&
            now() - jobs_.front().queued_at < kBatchWaitUs)
            return false;
        job = std::move(jobs_.front());
        jobs_.pop_front();
        if (job.cut)
            return true;
        while (!jobs_.empty() && !jobs_.front().cut && jobs_.front().fd == job.fd &&
               jobs_.front().offset == job.offset + job.bytes.size() &&
               job.bytes.size() + jobs_.front().bytes.size() <= kLargestWrite)
        {
            job.bytes.insert(job.bytes.end(), jobs_.front().bytes.begin(),
                             jobs_.front().bytes.end());
            jobs_.pop_front();
        }
        return true;
    }
    bool write_all(const Job &job)
    {
        const auto *data = job.bytes.data();
        auto offset = job.offset;
        auto left = job.bytes.size();
        while (left)
        {
            const auto count = write_(job.fd, data, left, static_cast<off_t>(offset));
            if (count < 0 && errno == EINTR)
                continue;
            if (count <= 0)
            {
                error_ = count < 0 ? errno : EIO;
                return false;
            }
            data += count;
            offset += static_cast<std::uint64_t>(count);
            left -= static_cast<std::size_t>(count);
        }
        return true;
    }
    bool cut_file(const Job &job)
    {
        const bool done = cut_(job.fd, static_cast<off_t>(job.offset)) == 0;
        if (!done)
            error_ = errno ? errno : EIO;
        return done;
    }
    void run()
    {
        Job job{};
        for (;;)
        {
            if (!take(job))
            {
                if (stopping_.load() && !queued_.load() && !cuts_.load())
                    return;
                usleep(5000);
                continue;
            }
            const auto began = now();
            if (job.cut ? cut_file(job) : write_all(job))
            {
                const auto took = now() - began;
                auto longest = longest_us_.load();
                while (took > longest && !longest_us_.compare_exchange_weak(longest, took))
                {
                }
                ++writes_;
                if (job.cut)
                    --cuts_;
                else
                    queued_ -= job.bytes.size();
            }
            else
            {
                std::lock_guard<std::mutex> lock(mutex_);
                jobs_.clear();
                queued_ = 0;
                cuts_ = 0;
            }
        }
    }

    WriteFn write_;
    CutFn cut_;
    pthread_t thread_{};
    bool started_ = false;
    std::mutex mutex_;
    std::deque<Job> jobs_;
    std::atomic<std::size_t> queued_{0};
    std::atomic<int> flushing_{0}, error_{0}, cuts_{0};
    std::atomic<bool> stopping_{false};
    std::atomic<std::int64_t> longest_us_{0};
    std::atomic<std::uint64_t> writes_{0};
};
} // namespace afterglow::usbflac
