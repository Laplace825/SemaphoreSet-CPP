#pragma once

#include <spdlog/spdlog.h>
#include <sys/ipc.h>
#include <sys/mman.h>
#include <sys/sem.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <source_location>
#include <stack>
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

    /// @note: use to change semid's value
    /// this is actually to operate the semid
    void sem_operation(sem_nameid_t sem_numid, int16_t sem_op,
      std::source_location loc = std::source_location::current()) {
        sembuf ops = {sem_numid, sem_op, SEM_UNDO};
        if (semop(semid, &ops, 1) == -1) {
            spdlog::error(
              "Error signaling semaphore in {} happen in {} func {}", __LINE__,
              loc.line(), loc.function_name());
            exit(1);
        }
    }

    /// @note: use to block oneself
    /// when we find the resource is not enough to distribute(value < min_val),
    void block_oneself_or_release(sem_nameid_t who, int16_t sem_op,
      std::source_location loc = std::source_location::current()) {
        sembuf ops = {who, sem_op, SEM_UNDO};
        if (semop(block_oneself_semid, &ops, 1) == -1) {
            spdlog::error(
              "Error signaling semaphore in {} happen in {} func {}", __LINE__,
              loc.line(), loc.function_name());
            exit(1);
        }
    }

    void check_semctl_error() {
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
    SemaphoreSet(key_t key, const sem_name_id_map_t &sem_names)
        : num_sems(sem_names.size()) {
        semid               = semget(key, num_sems, IPC_CREAT | 0666);
        block_oneself_semid = semget(key, num_sems, IPC_CREAT | 0666);

        /// @note: every sem has a record to record who block me
        /// new get memory is not shared, but mmap can be shared
        ptr_record_who_block_me = new int16_t *;
        (*ptr_record_who_block_me) =
          (int16_t *)mmap(nullptr, num_sems * sizeof(int16_t),
            PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
        std::fill((*ptr_record_who_block_me),
          (*ptr_record_who_block_me) + num_sems, -1);

        if (semid == -1 || block_oneself_semid == -1) {
            spdlog::error("Error creating semaphore set in {}", __LINE__);
            this->~SemaphoreSet();
            check_semctl_error();
            exit(1);
        }

        semun arg;
        for (const auto &[num_id, num_val] : sem_names) {
            arg.val = num_val;
            spdlog::trace(
              "semid: {} num_id: {} num_val: {}", semid, num_id, num_val);
            if (semctl(semid, num_id, SETVAL, arg) == -1) {
                spdlog::error("Error initializing semaphore in {}", __LINE__);
                this->~SemaphoreSet();

                check_semctl_error();
                exit(1);
            }

            arg.val = 0; // block_oneself_semid initializing 0

            if (semctl(block_oneself_semid, num_id, SETVAL, arg) == -1) {
                spdlog::error("Error initializing semaphore in {}", __LINE__);
                this->~SemaphoreSet();

                check_semctl_error();
                exit(1);
            }
        }
    }

    /// {    sem_nameid  P,v op     min_val
    ///     {0,         { -1 ,       1 } }
    /// }
    void Swait(const std::vector< std::pair< uint16_t, lap::SemIdToReduce > >
        &sem_op_min_val_vector) {
        /// @note: when resource release and the process goes out of
        /// block to continue, we judge the resource again to avoid if
        /// the resource changed less than 0 during the block period
        ///
        /// @note: see the only goto below
    judge_again:

        /// push the smaller one into what_smaller
        std::stack< std::pair< uint16_t, lap::SemIdToReduce > >
          what_have_changed;

        for (auto &sem_op_with_min_val : sem_op_min_val_vector) {
            if (semctl(semid, sem_op_with_min_val.first, GETVAL) >=
                sem_op_with_min_val.second.min_val)
            {
                if (sem_op_with_min_val.second.sem_op != 0) {
                    what_have_changed.push(sem_op_with_min_val);
                    /// @note: we have resource to distribute
                    sem_operation(sem_op_with_min_val.first,
                      sem_op_with_min_val.second.sem_op);
                }
            }
            else {
                (*ptr_record_who_block_me)[sem_op_with_min_val.first] =
                  sem_op_with_min_val.first;

                spdlog::trace("Who Block sem {}'s var :{}",
                  sem_op_with_min_val.first,
                  (*ptr_record_who_block_me)[sem_op_with_min_val.first]);
                /// @note: we don't have resource to distribute
                /// go back
                while (!what_have_changed.empty()) {
                    auto go_back_sem_op_with_min_val = what_have_changed.top();
                    what_have_changed.pop();
                    spdlog::trace("go back sem {}'s val : {}",
                      go_back_sem_op_with_min_val.first,
                      semctl(semid, go_back_sem_op_with_min_val.first, GETVAL));

                    sem_operation(go_back_sem_op_with_min_val.first,
                      -go_back_sem_op_with_min_val.second.sem_op);
                    spdlog::trace("After go back sem {}'s val : {}",
                      go_back_sem_op_with_min_val.first,
                      semctl(semid, go_back_sem_op_with_min_val.first, GETVAL));
                }
                // matian_atomic(Vsemop);
                for (int16_t i = 0; i < num_sems; ++i) {
                    spdlog::trace("Block sem {}'s var :{}", i,
                      (*ptr_record_who_block_me)[i]);
                    if ((*ptr_record_who_block_me)[i] != -1)
                        block_oneself_or_release(
                          (*ptr_record_who_block_me)[i], Psemop);
                }

                /// @note: when resource release and the process goes out of
                /// block to continue, we judge the resource again to avoid if
                /// the resource changed less than 0 during the block period
                goto judge_again;
            }
        }

        return;
    }

    void Ssignal(sem_nameid_t sem_numid, int16_t sem_op = Vsemop) {
        spdlog::trace("Release find {}, sem {}'s",
          (*ptr_record_who_block_me)[sem_numid] != -1, sem_numid);
        if ((*ptr_record_who_block_me)[sem_numid] != -1 &&
            semctl(block_oneself_semid, sem_numid, GETVAL) <= 0)
        {
            block_oneself_or_release(sem_numid, Vsemop);
        }

        sem_operation(sem_numid, sem_op);
    }

    int32_t getSemid() const { return semid; }

    int32_t getVal(sem_nameid_t sem_numid) {
        return semctl(semid, sem_numid, GETVAL);
    }

    ~SemaphoreSet() {
        munmap((*ptr_record_who_block_me), num_sems * sizeof(int16_t));
        delete ptr_record_who_block_me;
    }
};

} // namespace lap
