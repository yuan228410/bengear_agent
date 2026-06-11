#include "ben_gear/test/test_framework.hpp"
#include "ben_gear/base/platform/file_lock.hpp"
#include "test_util.hpp"

#include <fstream>
#include <string>
#include <thread>

#ifndef _WIN32
#include <fcntl.h>
#endif

using bengear::test::TmpDirTest;
using FileLock = ben_gear::base::platform::FileLock;

// --- FileLock 基本操作 ---

class FileLockTest : public TmpDirTest {
protected:
    std::filesystem::path lock_file() const { return dir() / "test.lock"; }
};

TEST_F(FileLockTest, ExclusiveLockNewFile) {
    auto lock = FileLock::exclusive(lock_file(), true);
    ASSERT_TRUE(lock.has_value());
    EXPECT_GE(lock->fd(), 0);
}

TEST_F(FileLockTest, ExclusiveLockExistingFile) {
    // 先创建文件
    {
        std::ofstream f(lock_file(), std::ios::binary);
        f << "initial content";
    }

    auto lock = FileLock::exclusive(lock_file(), false);
    ASSERT_TRUE(lock.has_value());
}

TEST_F(FileLockTest, WriteAndReadBack) {
    auto lock = FileLock::exclusive(lock_file(), true);
    ASSERT_TRUE(lock.has_value());

    const char* data = "hello file lock";
    auto written = lock->write(data, std::strlen(data));
    EXPECT_EQ(written, static_cast<ssize_t>(std::strlen(data)));

    // 解锁后读回
    lock.reset();

    std::ifstream f(lock_file(), std::ios::binary);
    std::string content{std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
    EXPECT_EQ(content, "hello file lock");
}

TEST_F(FileLockTest, SyncSucceeds) {
    auto lock = FileLock::exclusive(lock_file(), true);
    ASSERT_TRUE(lock.has_value());

    lock->write("data", 4);
    EXPECT_TRUE(lock->sync());
}

TEST_F(FileLockTest, TruncateShrinksFile) {
    auto lock = FileLock::exclusive(lock_file(), true);
    ASSERT_TRUE(lock.has_value());

    lock->write("hello world", 11);
    lock->sync();

    EXPECT_TRUE(lock->truncate(5));

    // seek to end to check size
    auto size = lock->seek(0, SEEK_END);
    EXPECT_EQ(size, 5);
}

TEST_F(FileLockTest, SeekChangesPosition) {
    auto lock = FileLock::exclusive(lock_file(), true);
    ASSERT_TRUE(lock.has_value());

    lock->write("abcdefghij", 10);

    auto pos = lock->seek(5, SEEK_SET);
    EXPECT_EQ(pos, 5);
}

TEST_F(FileLockTest, DefaultConstructNoLock) {
    FileLock lock;
    EXPECT_EQ(lock.fd(), -1);
}

TEST_F(FileLockTest, MoveConstruction) {
    auto lock1 = FileLock::exclusive(lock_file(), true);
    ASSERT_TRUE(lock1.has_value());
    int fd = lock1->fd();

    FileLock lock2(std::move(*lock1));
    EXPECT_EQ(lock2.fd(), fd);
    EXPECT_EQ(lock1->fd(), -1);  // NOLINT：移动后检查
}

TEST_F(FileLockTest, LockNonExistentFileNoCreate) {
    auto path = dir() / "nonexistent.lock";
    auto lock = FileLock::exclusive(path, false);
    EXPECT_FALSE(lock.has_value());
}

#ifndef _WIN32
TEST_F(FileLockTest, UnlockOnDestruct) {
    {
        auto lock = FileLock::exclusive(lock_file(), true);
        ASSERT_TRUE(lock.has_value());
        EXPECT_GE(lock->fd(), 0);
    }
    // 析构后应能重新获取锁
    auto lock2 = FileLock::exclusive(lock_file(), true);
    ASSERT_TRUE(lock2.has_value());
}
#endif
