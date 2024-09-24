#pragma once

#include <spdlog/spdlog.h>
#include <sys/ipc.h>
#include <sys/sem.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>

namespace lap {

using sem_nameid_t = uint16_t;

/// (semname_id, initial_value)
using sem_name_id_map_t = std::unordered_map< sem_nameid_t, int32_t >;

using SemIdToReduce = struct {
    int32_t min_val; /// min resource value
    int16_t sem_op;  /// operation to semaphore
};

class SemaphoreSet {
  private:
    static constexpr int8_t Psemop = -1; // semaphore operation for P
    static constexpr int8_t Vsemop = 1;  // semaphore operation for V
    int16_t semid;                       // semaphore set ID
    int32_t num_sems;                    // number of semaphores in the set

    int16_t inner_semid; // the class maintain semaphore, use to handle check
                         // and sem_op == 0

    using semun = union {
        int val;               /* Value for SETVAL */
        struct semid_ds *buf;  /* Buffer for IPC_STAT, IPC_SET */
        unsigned short *array; /* Array for GETALL, SETALL */
        struct seminfo *__buf; /* Buffer for IPC_INFO
                                  (Linux-specific) */
    };

  public:
    SemaphoreSet(key_t key, const sem_name_id_map_t &sem_names)
        : num_sems(sem_names.size()) {
        semid       = semget(key, num_sems, IPC_CREAT | 0666);
        inner_semid = semget(key, 1, IPC_CREAT | 0666);

        if (semid == -1 || inner_semid == -1) {
            perror("Error creating semaphore set");
            exit(1);
        }

        semun arg;
        for (const auto &[num_id, num_val] : sem_names) {
            arg.val = num_val;
            if (semctl(semid, num_id, SETVAL, arg) == -1) {
                spdlog::error("Error initializing semaphore in {}", __LINE__);
                exit(1);
            }
        }

        arg.val = 0; // inner_semid initializing 0

        if (semctl(inner_semid, 0, SETVAL, arg) == -1) {
            spdlog::error("Error initializing semaphore in {}", __LINE__);
            exit(1);
        }
    }

    /// {    sem_nameid  P,v op     min_val
    ///     {0,         { -1 ,       1 } }
    /// }
    void Swait(const std::vector< std::pair< uint16_t, lap::SemIdToReduce > >
        &sem_op_min_val_vector) {
        spdlog::trace(
          "Inner_sem val before Swait : {}", semctl(inner_semid, 0, GETVAL));

        for (auto &sem_op_with_min_val : sem_op_min_val_vector) {
            spdlog::trace("sem {}'s val before Swait : {}",
              sem_op_with_min_val.first,
              semctl(semid, sem_op_with_min_val.first, GETVAL));
            if (semctl(semid, sem_op_with_min_val.first, GETVAL) >=
                sem_op_with_min_val.second.min_val)
            {
                if (sem_op_with_min_val.second.sem_op != 0) {
                    /// @note: we have resource to distribute
                    sembuf ops = {sem_op_with_min_val.first,
                      sem_op_with_min_val.second.sem_op, SEM_UNDO};
                    if (semop(semid, &ops, 1) == -1) {
                        perror("Error waiting on semaphore");
                        exit(1);
                    }
                }
            }
            else {
                sembuf ops = {0, Psemop, SEM_UNDO};
                if (semop(inner_semid, &ops, 1) == -1) {
                    perror("Error waiting on semaphore");
                    exit(1);
                }
            }

            spdlog::trace("sem {}'s val After Swait : {}",
              sem_op_with_min_val.first,
              semctl(semid, sem_op_with_min_val.first, GETVAL));
        }
        spdlog::trace(
          "Inner_sem val after Swait : {}", semctl(inner_semid, 0, GETVAL));
    }

    void Ssignal(sem_nameid_t sem_numid, int16_t sem_op = Vsemop) {
        spdlog::trace(
          "Inner_sem val before Ssignal : {}", semctl(inner_semid, 0, GETVAL));

        sembuf ops = {sem_numid, sem_op, SEM_UNDO};
        if (semop(semid, &ops, 1) == -1) {
            perror("Error signaling semaphore");
            exit(1);
        }

        if (semctl(inner_semid, 0, GETVAL) <= 0) {
            ops = {0, Vsemop, SEM_UNDO};
            if (semop(inner_semid, &ops, 1) == -1) {
                perror("Error signaling semaphore");
                exit(1);
            }
        }

        spdlog::trace(
          "Inner_sem val after Ssignal : {}", semctl(inner_semid, 0, GETVAL));
    }

    int32_t getSemid() const { return semid; }

    int32_t getVal(sem_nameid_t sem_numid) {
        return semctl(semid, sem_numid, GETVAL);
    }
};

} // namespace lap
