#include <spdlog/cfg/env.h>
#include <spdlog/spdlog.h>
#include <sys/sem.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <random>

#include "semaphore_set.h"

/// max reader numbers
constexpr int16_t MAX_READERS = 3;
constexpr int16_t MAX_WRITERS = 1;

class ReaderWriterProblem {
  private:
    void write_to(const char* file_name) {
        std::srand(std::time(nullptr));
        std::fstream fs(file_name, std::ios::out);
        auto val = std::rand();
        spdlog::info("╰─ I Write this: Hello World {}", val);
        fs << "Hello World " << val << std::endl;
    }

    void read_from(const char* file_name, int32_t id) {
        std::fstream fs(file_name, std::ios::in);
        std::string line;
        while (std::getline(fs, line)) {
            spdlog::info("Reader id: {} I Read this: {}", id, line);
        }
    }

  private:
    lap::SemaphoreSet semSet;

    enum SemaphoreNames { READ_LEFT = 0, RW_MUTEX, WRITER_WAIT };

  public:
    ReaderWriterProblem()
        : semSet{
            IPC_PRIVATE,
            {{READ_LEFT, MAX_READERS}, {RW_MUTEX, 1}, {WRITER_WAIT, 1}}
    } {}

    /**
     * @brief: when Swait's sem value >= specify min resources value, distribute
     * resources and continue;
     * when Swait's sem value < specify min resources value, block itself
     */
    void reader(int32_t id) {
        semSet.Swait({
          {READ_LEFT,   {1, -1}},
          {WRITER_WAIT, {1, 0} },
          {RW_MUTEX,    {0, 0} },
        });

        // Reading
        spdlog::info("Reader id:{} Readers left:{} Write Mutex:{}"
                     " Reader Mutex:{}",
          id, semSet.getVal(READ_LEFT), semSet.getVal(WRITER_WAIT),
          semSet.getVal(RW_MUTEX));
        read_from("file.txt", id);

        semSet.Ssignal(READ_LEFT);
    }

    void writer(int32_t id) {
        semSet.Swait({
          {WRITER_WAIT, {1, -1}},
        });
        semSet.Swait({
          {RW_MUTEX,  {1, -1}},
          {READ_LEFT, {3, 0} }
        });

        // Writing
        spdlog::info("╭─ Writer id:{} Readers left:{} Write Mutex:{} Reader "
                     "Mutex:{}",
          id, semSet.getVal(READ_LEFT), semSet.getVal(RW_MUTEX),
          semSet.getVal(WRITER_WAIT));
        write_to("file.txt");

        semSet.Ssignal(WRITER_WAIT);
        semSet.Ssignal(RW_MUTEX);
    }
};

int main() {
    spdlog::set_pattern("[%^--%l--%$] [Process %P] %v");
    spdlog::cfg::load_env_levels();
    ReaderWriterProblem rwp;

    std::array< int32_t, 8 > fork_seq = {0, 1, 2, 3, 4, 5, 6, 7};
    std::shuffle(
      fork_seq.begin(), fork_seq.end(), std::mt19937(std::random_device()()));

    // fork 5 writers and 3 readers
    for (int32_t id : fork_seq) {
        if (id < 4) {
            if (fork() == 0) {
                rwp.writer(id);
                break;
            }
        }
        else if (id == 4) {
            rwp.writer(id);
        }
        else {
            if (fork() == 0) {
                rwp.reader(id);
                break;
            }
        }
    }

    return EXIT_SUCCESS;
}
