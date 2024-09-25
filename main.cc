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
#include <vector>

#include "semaphore_set.h"

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

/// max reader numbers
#if __cplusplus >= 201402L
    static constexpr uint8_t MAX_READERS = 3;
#else
    const uint8_t MAX_READERS = 3;
#endif

    enum SemaphoreNames : uint16_t { READ_COUNT = 0, MUTEX = 1 };

  public:
    ReaderWriterProblem()
        : semSet{
            IPC_PRIVATE, {{READ_COUNT, MAX_READERS}, {MUTEX, 1}}
    } {}

    /**
     * @brief: when Swait's sem value >= specify min resources value, distribute
     * resources and continue;
     * when Swait's sem value < specify min resources value, block itself
     */
    void reader(int32_t id) {
        // do {
        spdlog::debug("Reader {} want ", id);
        semSet.Swait({
          {READ_COUNT, {1, -1}},
          {MUTEX,      {1, 0} }
        });

        // Reading
        spdlog::info("Reader id:{} Readers left:{} Mutex: {}", id,
          semSet.getVal(READ_COUNT), semSet.getVal(MUTEX));
        read_from("file.txt", id);

        semSet.Ssignal(READ_COUNT);
        // }

        // while (true);
    }

    void writer(int32_t id) {
        // do {
        spdlog::debug("Writer {} want ", id);
        semSet.Swait({
          {MUTEX,      {1, -1}         },
          {READ_COUNT, {MAX_READERS, 0}}
        });

        // Writing
        spdlog::info("╭─ Writer id:{} Readers left:{} Mutex:{}", id,
          semSet.getVal(READ_COUNT), semSet.getVal(MUTEX));
        write_to("file.txt");

        semSet.Ssignal(MUTEX);
        // } while (true);
    }
};

int main() {
    spdlog::set_pattern("[%^--%l--%$] [Process %P] %v");
    spdlog::cfg::load_env_levels();
    ReaderWriterProblem rwp;

    // fork 3 readers and 1 writer

    std::array< int32_t, 4 > fork_seq = {0, 1, 2, 3};
    std::shuffle(
      fork_seq.begin(), fork_seq.end(), std::mt19937(std::random_device()()));

    for (auto fork_ind : fork_seq) {
        if (fork_ind == 3) {
            rwp.writer(3);
        }
        else {
            if (fork() == 0) {
                rwp.reader(fork_ind);
                break;
            }
        }
    }

    return EXIT_SUCCESS;
}
