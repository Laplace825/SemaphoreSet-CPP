#pragma once

#include <spdlog/spdlog.h>
#include <sys/ipc.h>
#include <sys/mman.h>
#include <sys/sem.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>

#if __cplusplus >= 202002L
#include <source_location>
#endif

#include <unordered_map>

namespace lap {

using sem_nameid_t = uint16_t;

/// (semname_id, initial_value)
using sem_name_id_map_t = std::unordered_map< sem_nameid_t, int32_t >;

using SemIdToReduce = struct {
    int32_t min_val; /// min resource value
    int32_t sem_op;  /// operation to semaphore
};

class SemaphoreSet {
  private:
    static constexpr int8_t Psemop = -1; // semaphore operation for P
    static constexpr int8_t Vsemop = 1;  // semaphore operation for V
    int32_t semid;                       // semaphore set ID
    int32_t num_sems;                    // number of semaphores in the set

    int32_t block_oneself_semid;       // to block oneself
    int16_t **ptr_record_who_block_me; // to record who block me

    using semun = union {
        int val;               /* Value for SETVAL */
        struct semid_ds *buf;  /* Buffer for IPC_STAT, IPC_SET */
        unsigned short *array; /* Array for GETALL, SETALL */
        struct seminfo *__buf; /* Buffer for IPC_INFO
                                  (Linux-specific) */
    };

#if __cplusplus >= 202002L

    /// @note: use to change semid's value
    /// this is actually to operate the semid
    void sem_operation(sem_nameid_t sem_numid, int16_t sem_op,
      std::source_location loc = std::source_location::current());

    /// @note: use to block oneself
    /// when we find the resource is not enough to distribute(value < min_val),
    void block_oneself_or_release(sem_nameid_t who, int16_t sem_op,
      std::source_location loc = std::source_location::current());

#else
    /// @note: use to change semid's value
    /// this is actually to operate the semid
    void sem_operation(sem_nameid_t sem_numid, int16_t sem_op);

    /// @note: use to block oneself
    /// when we find the resource is not enough to distribute(value < min_val),
    void block_oneself_or_release(sem_nameid_t who, int16_t sem_op);
#endif

    static void check_semctl_error() {
        spdlog::error("Error initializing semaphore in {} error {}", __LINE__,
          std::strerror(errno));
        switch (errno) {
            case EINVAL:
                spdlog::error("Invalid semaphore identifier, command, or "
                              "semaphore number.");
                break;
            case EACCES:
                spdlog::error("Permission denied.");
                break;
            case ENOMEM:
                spdlog::error("Not enough memory.");
                break;
            default:
                spdlog::error("Unknown error.");
                break;
        }
        exit(1);
    }

  public:
    SemaphoreSet(key_t key, const sem_name_id_map_t &sem_names);

    /// {    sem_nameid  P,v op     min_val
    ///     {0,         { -1 ,       1 } }
    /// }
    void Swait(const std::vector< std::pair< uint16_t, lap::SemIdToReduce > >
        &sem_op_min_val_vector);

    void Ssignal(sem_nameid_t sem_numid, int16_t sem_op = Vsemop);

    int32_t getSemid() const;

    int32_t getVal(sem_nameid_t sem_numid);
    ~SemaphoreSet();
};

} // namespace lap
