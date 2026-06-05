#pragma once

// 跨平台文件锁封装
// POSIX: fcntl(F_SETLKW) 进程级劝告锁
// Windows: LockFileEx 强制锁

#include "ben_gear/base/log/logger.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#endif

#include <filesystem>
#include <string>

namespace ben_gear::base::platform {

/// RAII 文件锁：构造时加锁，析构时解锁
/// 跨进程安全（POSIX fcntl / Windows LockFileEx）
class FileLock {
public:
    /// 打开文件并获取排他锁
    /// @param path 文件路径
    /// @param create_if_missing 文件不存在时是否创建
    static std::optional<FileLock> exclusive(const std::filesystem::path& path,
                                             bool create_if_missing = true) {
        FileLock lock;
        if (!lock.open_and_lock(path, create_if_missing)) {
            return std::nullopt;
        }
        return lock;
    }

    FileLock() = default;

    FileLock(FileLock&& other) noexcept
        : fd_(other.fd_)
#ifdef _WIN32
        , handle_(other.handle_)
#endif
    {
        other.fd_ = -1;
#ifdef _WIN32
        other.handle_ = INVALID_HANDLE_VALUE;
#endif
    }

    FileLock& operator=(FileLock&& other) noexcept {
        if (this != &other) {
            unlock();
            fd_ = other.fd_;
#ifdef _WIN32
            handle_ = other.handle_;
#endif
            other.fd_ = -1;
#ifdef _WIN32
            other.handle_ = INVALID_HANDLE_VALUE;
#endif
        }
        return *this;
    }

    FileLock(const FileLock&) = delete;
    FileLock& operator=(const FileLock&) = delete;

    ~FileLock() { unlock(); }

    /// 获取底层文件描述符（用于写入）
    int fd() const noexcept { return fd_; }

#ifdef _WIN32
    HANDLE handle() const noexcept { return handle_; }
#endif

    /// 写入数据到已锁定的文件
    /// @return 实际写入字节数，-1 表示错误
    ssize_t write(const char* data, size_t size) {
#ifdef _WIN32
        DWORD written = 0;
        if (!WriteFile(handle_, data, static_cast<DWORD>(size), &written, nullptr)) {
            return -1;
        }
        return static_cast<ssize_t>(written);
#else
        ssize_t total = 0;
        while (static_cast<size_t>(total) < size) {
            auto n = ::write(fd_, data + total, size - total);
            if (n < 0) {
                if (errno == EINTR) continue;
                return -1;
            }
            total += n;
        }
        return total;
#endif
    }

    /// 刷新到磁盘（fsync / FlushFileBuffers）
    bool sync() {
#ifdef _WIN32
        return FlushFileBuffers(handle_) != 0;
#else
        return ::fsync(fd_) == 0;
#endif
    }

    /// 截断文件到指定大小
    bool truncate(size_t size) {
#ifdef _WIN32
        LARGE_INTEGER li;
        li.QuadPart = static_cast<LONGLONG>(size);
        if (!SetFilePointerEx(handle_, li, nullptr, FILE_BEGIN)) return false;
        return SetEndOfFile(handle_) != 0;
#else
        return ::ftruncate(fd_, static_cast<off_t>(size)) == 0;
#endif
    }

    /// 移动文件偏移量
    /// @param offset 偏移量
    /// @param whence SEEK_SET / SEEK_CUR / SEEK_END
    /// @return 新的文件偏移量，-1 表示错误
    off_t seek(off_t offset, int whence) {
#ifdef _WIN32
        LARGE_INTEGER li;
        li.QuadPart = offset;
        LARGE_INTEGER new_pos;
        DWORD method = FILE_BEGIN;
        if (whence == SEEK_CUR) method = FILE_CURRENT;
        else if (whence == SEEK_END) method = FILE_END;
        if (!SetFilePointerEx(handle_, li, &new_pos, method)) return -1;
        return new_pos.QuadPart;
#else
        return ::lseek(fd_, offset, whence);
#endif
    }

private:
    bool open_and_lock(const std::filesystem::path& path, bool create_if_missing) {
#ifdef _WIN32
        DWORD access = GENERIC_WRITE | GENERIC_READ;
        DWORD createDisposition = create_if_missing ? OPEN_ALWAYS : OPEN_EXISTING;
        handle_ = CreateFileW(
            path.wstring().c_str(), access, 0, nullptr,
            createDisposition, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE) {
            log::error_fmt("FileLock: failed to open: {}", path.string());
            return false;
        }

        OVERLAPPED overlapped = {};
        if (!LockFileEx(handle_, LOCKFILE_EXCLUSIVE_LOCK, 0,
                        MAXDWORD, MAXDWORD, &overlapped)) {
            log::error_fmt("FileLock: failed to lock: {}", path.string());
            CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
            return false;
        }

        fd_ = _open_osfhandle(reinterpret_cast<intptr_t>(handle_), 0);
        return true;
#else
        int flags = O_RDWR;
        if (create_if_missing) flags |= O_CREAT;
        fd_ = ::open(path.string().c_str(), flags, 0644);
        if (fd_ < 0) {
            log::error_fmt("FileLock: failed to open: {}", path.string());
            return false;
        }

        struct flock fl{};
        fl.l_type = F_WRLCK;
        fl.l_whence = SEEK_SET;
        if (::fcntl(fd_, F_SETLKW, &fl) != 0) {
            log::error_fmt("FileLock: failed to lock: {}", path.string());
            ::close(fd_);
            fd_ = -1;
            return false;
        }

        return true;
#endif
    }

    void unlock() {
#ifdef _WIN32
        if (handle_ != INVALID_HANDLE_VALUE) {
            OVERLAPPED overlapped = {};
            UnlockFileEx(handle_, 0, MAXDWORD, MAXDWORD, &overlapped);
            CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
        }
#else
        if (fd_ >= 0) {
            struct flock fl{};
            fl.l_type = F_UNLCK;
            fl.l_whence = SEEK_SET;
            ::fcntl(fd_, F_SETLKW, &fl);
            ::close(fd_);
            fd_ = -1;
        }
#endif
    }

#ifdef _WIN32
    HANDLE handle_ = INVALID_HANDLE_VALUE;
#endif
    int fd_ = -1;
};

}  // namespace ben_gear::base::platform
