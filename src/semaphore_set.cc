#include "semaphore_set.h"

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

#include <stack>

namespace lap {

#if __cplusplus >= 202002L

void SemaphoreSet::sem_operation(
  sem_nameid_t sem_numid, int16_t sem_op, std::source_location loc) {
    sembuf ops = {sem_numid, sem_op, SEM_UNDO};
    if (semop(semid, &ops, 1) == -1) {
        spdlog::error("Error signaling semaphore  happen in {} func {}",
          loc.line(), loc.function_name());
        exit(1);
    }
}

/// @note: use to block oneself
/// when we find the resource is not enough to distribute(value < min_val),
void SemaphoreSet::block_oneself_or_release(
  sem_nameid_t who, int16_t sem_op, std::source_location loc) {
    sembuf ops = {who, sem_op, SEM_UNDO};
    if (semop(block_oneself_semid, &ops, 1) == -1) {
        spdlog::error("Error signaling semaphore  happen in {} func {}",
          loc.line(), loc.function_name());
        exit(1);
    }
}

#else

void SemaphoreSet::sem_operation(sem_nameid_t sem_numid, int16_t sem_op) {
    sembuf ops = {sem_numid, sem_op, SEM_UNDO};
    if (semop(semid, &ops, 1) == -1) {
        spdlog::error("Error signaling semaphore  happen in {} func {}",
          __LINE__, __FUNCTION__);
        exit(1);
    }
}

/// @note: use to block oneself
/// when we find the resource is not enough to distribute(value < min_val),
void SemaphoreSet::block_oneself_or_release(sem_nameid_t who, int16_t sem_op) {
    sembuf ops = {who, sem_op, SEM_UNDO};
    if (semop(block_oneself_semid, &ops, 1) == -1) {
        spdlog::error("Error signaling semaphore  happen in {} func {}",
          __LINE__, __FUNCTION__);
        exit(1);
    }
}

#endif

SemaphoreSet::SemaphoreSet(key_t key, const sem_name_id_map_t &sem_names)
    : num_sems(sem_names.size()) {
    semid               = semget(key, num_sems, IPC_CREAT | 0666);
    block_oneself_semid = semget(key, num_sems, IPC_CREAT | 0666);

    /// @note: every sem has a record to record who block me
    /// new get memory is not shared, but mmap can be shared
    ptr_record_who_block_me = new int16_t *;
    (*ptr_record_who_block_me) =
      (int16_t *)mmap(nullptr, num_sems * sizeof(int16_t),
        PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    std::fill(
      (*ptr_record_who_block_me), (*ptr_record_who_block_me) + num_sems, -1);

    if (semid == -1 || block_oneself_semid == -1) {
        spdlog::error("Error creating semaphore set in {}", __LINE__);
        this->~SemaphoreSet();
        check_semctl_error();
        exit(1);
    }

    semun arg;
#if __cplusplus >= 201703L
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
#else
    for (const auto &sem_name : sem_names) {
        arg.val = sem_name.second;
        spdlog::trace("semid: {} num_id: {} num_val: {}", semid, sem_name.first,
          sem_name.second);
        if (semctl(semid, sem_name.first, SETVAL, arg) == -1) {
            spdlog::error("Error initializing semaphore in {}", __LINE__);
            this->~SemaphoreSet();
            check_semctl_error();
            exit(1);
        }

        arg.val = 0; // block_oneself_semid initializing 0

        if (semctl(block_oneself_semid, sem_name.first, SETVAL, arg) == -1) {
            spdlog::error("Error initializing semaphore in {}", __LINE__);
            this->~SemaphoreSet();

            check_semctl_error();
            exit(1);
        }
    }
#endif
}

/// {    sem_nameid  P,v op     min_val
///     {0,         { -1 ,       1 } }
/// }
void SemaphoreSet::Swait(
  const std::vector< std::pair< uint16_t, lap::SemIdToReduce > >
    &sem_op_min_val_vector) {
    /// @note: when resource release and the process goes out of
    /// block to continue, we judge the resource again to avoid if
    /// the resource changed less than 0 during the block period
    ///
    /// @note: see the only goto below
judge_again:

    /// push the smaller one into what_smaller
    std::stack< std::pair< uint16_t, lap::SemIdToReduce > > what_have_changed;

    for (auto &sem_op_with_min_val : sem_op_min_val_vector) {
        if (semctl(this->semid, sem_op_with_min_val.first, GETVAL) >=
            sem_op_with_min_val.second.min_val)
        {
            if (sem_op_with_min_val.second.sem_op != 0) {
                what_have_changed.push(sem_op_with_min_val);
                /// @note: we have resource to distribute
                sem_operation(
                  sem_op_with_min_val.first, sem_op_with_min_val.second.sem_op);
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
                spdlog::trace(
                  "Block sem {}'s var :{}", i, (*ptr_record_who_block_me)[i]);
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

void SemaphoreSet::Ssignal(sem_nameid_t sem_numid, int16_t sem_op) {
    spdlog::trace("Release find {}, sem {}'s",
      (*ptr_record_who_block_me)[sem_numid] != -1, sem_numid);
    if ((*ptr_record_who_block_me)[sem_numid] != -1 &&
        semctl(block_oneself_semid, sem_numid, GETVAL) <= 0)
    {
        block_oneself_or_release(sem_numid, Vsemop);
    }

    sem_operation(sem_numid, sem_op);
}

int32_t SemaphoreSet::getSemid() const { return semid; }

int32_t SemaphoreSet::getVal(sem_nameid_t sem_numid) {
    return semctl(semid, sem_numid, GETVAL);
}

SemaphoreSet::~SemaphoreSet() {
    munmap((*ptr_record_who_block_me), num_sems * sizeof(int16_t));
    delete ptr_record_who_block_me;
}

} // namespace lap
