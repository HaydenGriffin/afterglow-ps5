// Afterglow - Sees which song file the console's music player has open, and where it is reading.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Nothing the player says tells next from previous, or a pause from a slow drive, but its open
// files do. This only reads kernel memory, every pointer checked before it is followed. The
// player reads through a null mount, so its vnode is matched to ours by the one beneath it. The
// layouts were read off firmware 7.20; self_check says whether they hold on this console.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <map>
#include <ps5/kernel.h>
#include <sys/types.h>
// sysctl.h needs the types above first.
#include <sys/sysctl.h>
#include <unistd.h>

namespace afterglow::usbflac
{
struct PlayerSeen
{
    int slot = -1;          // The song file it has open, or -1 for none of them.
    std::uint64_t read = 0; // How far it has read.
    bool player_alive = false;
};

class PlayerWatch
{
  public:
    // Records the vnode of a song file this process has open.
    void add_slot(int slot, int fd)
    {
        slots_[vnode_of(getpid(), fd)] = slot;
    }
    void clear_slots()
    {
        slots_.clear();
    }
    // Reads a file of ours and looks for the read position where it should be.
    bool self_check(const char *path)
    {
        const int fd = open(path, O_RDONLY);
        if (fd < 0)
            return false;
        char bytes[16];
        const bool read_some = read(fd, bytes, sizeof bytes) == static_cast<ssize_t>(sizeof bytes);
        std::uint64_t position = 0, vnode = 0;
        const auto file = file_of(getpid(), fd);
        const bool good = read_some && file &&
                          copy(file + kFileNextOffset, &position, sizeof position) &&
                          copy(file + kFileVnode, &vnode, sizeof vnode) &&
                          position == sizeof bytes && is_kernel(vnode);
        close(fd);
        return good;
    }
    PlayerSeen look()
    {
        PlayerSeen seen;
        if (pid_ <= 0 || !kernel_get_proc(pid_))
            pid_ = find_player();
        if (pid_ <= 0)
            return seen;
        seen.player_alive = true;
        const auto table = table_of(pid_);
        int count = 0;
        if (!table || !copy(table, &count, sizeof count) || count <= 0 || count > 4096)
            return seen;
        for (int fd = 0; fd < count; ++fd)
        {
            std::uint64_t file = 0;
            if (!copy(table + kTableFiles + kEntryBytes * fd, &file, sizeof file) ||
                !is_kernel(file))
                continue;
            std::uint8_t raw[kFileBytes];
            if (!copy(file, raw, sizeof raw))
                continue;
            std::int16_t type = 0;
            std::uint32_t flag = 0;
            std::uint64_t vnode = 0, position = 0;
            std::memcpy(&type, raw + kFileType, sizeof type);
            std::memcpy(&flag, raw + kFileFlag, sizeof flag);
            std::memcpy(&vnode, raw + kFileVnode, sizeof vnode);
            std::memcpy(&position, raw + kFileNextOffset, sizeof position);
            if (type != kTypeVnode || flag != kReadOnly)
                continue;
            const auto found = slots_.find(lower_of(vnode));
            if (found == slots_.end())
                continue;
            seen.slot = found->second;
            seen.read = position;
            return seen;
        }
        return seen;
    }

  private:
    static constexpr std::size_t kTableFiles = 8, kEntryBytes = 0x30, kFileVnode = 0x18,
                                 kFileType = 0x20, kFileFlag = 0x24, kFileNextOffset = 0x30,
                                 kFileBytes = 0x48, kVnodeData = 0x10, kNullLower = 0x10;
    static constexpr std::int16_t kTypeVnode = 1;
    static constexpr std::uint32_t kReadOnly = 1;

    static bool is_kernel(std::uint64_t address)
    {
        return (address >> 48) == 0xffff;
    }
    static bool copy(std::uint64_t address, void *out, std::size_t size)
    {
        return is_kernel(address) && kernel_copyout(static_cast<intptr_t>(address), out, size) >= 0;
    }
    static std::uint64_t table_of(int pid)
    {
        std::uint64_t table = 0;
        const auto descriptors = static_cast<std::uint64_t>(kernel_get_proc_filedesc(pid));
        return copy(descriptors, &table, sizeof table) ? table : 0;
    }
    static std::uint64_t file_of(int pid, int fd)
    {
        std::uint64_t file = 0;
        const auto table = table_of(pid);
        return table && copy(table + kTableFiles + kEntryBytes * static_cast<std::size_t>(fd),
                             &file, sizeof file)
                   ? file
                   : 0;
    }
    static std::uint64_t vnode_of(int pid, int fd)
    {
        std::uint64_t vnode = 0;
        return copy(file_of(pid, fd) + kFileVnode, &vnode, sizeof vnode) ? vnode : 0;
    }
    static std::uint64_t lower_of(std::uint64_t vnode)
    {
        std::uint64_t data = 0, lower = 0;
        return copy(vnode + kVnodeData, &data, sizeof data) &&
                       copy(data + kNullLower, &lower, sizeof lower)
                   ? lower
                   : 0;
    }
    static int find_player()
    {
        for (int pid = 1; pid < 1024; ++pid)
        {
            int name[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, pid};
            char info[4096];
            std::size_t size = sizeof info;
            if (sysctl(name, 4, info, &size, nullptr, 0) || !size)
                continue;
            constexpr char kName[] = "SystemMusicCore";
            for (std::size_t at = 0; at + sizeof kName <= size; ++at)
                if (!std::memcmp(info + at, kName, sizeof kName - 1))
                    return pid;
        }
        return -1;
    }

    int pid_ = -1;
    std::map<std::uint64_t, int> slots_;
};
} // namespace afterglow::usbflac
