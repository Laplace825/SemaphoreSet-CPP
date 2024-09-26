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

namespace lap {

#if __cplusplus >= 202002L

void SemaphoreSet::sem_operation(
  sem_nameid_t sem_numid, int16_t sem_op, std::source_location loc) {
    sembuf ops = {sem_numid, sem_op, SEM_UNDO};
    if (semop(this->semid, &ops, 1) == -1) {
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
    if (semop(this->block_oneself_semid, &ops, 1) == -1) {
        spdlog::error("Error signaling semaphore  happen in {} func {}",
          loc.line(), loc.function_name());
        exit(1);
    }
}

#else

void SemaphoreSet::sem_operation(sem_nameid_t sem_numid, int16_t sem_op) {
    sembuf ops = {sem_numid, sem_op, SEM_UNDO};
    if (semop(this->semid, &ops, 1) == -1) {
        spdlog::error("Error signaling semaphore  happen in {} func {}",
          __LINE__, __FUNCTION__);
        exit(1);
    }
}

/// @note: use to block oneself
/// when we find the resource is not enough to distribute(value < min_val),
void SemaphoreSet::block_oneself_or_release(sem_nameid_t who, int16_t sem_op) {
    sembuf ops = {who, sem_op, SEM_UNDO};
    if (semop(this->block_oneself_semid, &ops, 1) == -1) {
        spdlog::error("Error signaling semaphore  happen in {} func {}",
          __LINE__, __FUNCTION__);
        exit(1);
    }
}

#endif

SemaphoreSet::SemaphoreSet(key_t key, const sem_name_id_map_t &sem_names)
    : num_sems(sem_names.size()) {
    this->semid               = semget(key, num_sems, IPC_CREAT | 0666);
    this->block_oneself_semid = semget(key, num_sems, IPC_CREAT | 0666);

    /// @note: every sem has a record to record who block me
    /// new get memory is not shared, but mmap can be shared
    this->ptr_record_who_block_me = new int16_t *;
    (*(this->ptr_record_who_block_me)) =
      (int16_t *)mmap(nullptr, num_sems * sizeof(int16_t),
        PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    std::fill((*(this->ptr_record_who_block_me)),
      (*(this->ptr_record_who_block_me)) + num_sems, -1);

    if (this->semid == -1 || this->block_oneself_semid == -1) {
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
          "semid: {} num_id: {} num_val: {}", this->semid, num_id, num_val);
        if (semctl(this->semid, num_id, SETVAL, arg) == -1) {
            spdlog::error("Error initializing semaphore in {}", __LINE__);
            this->~SemaphoreSet();

            check_semctl_error();
            exit(1);
        }

        arg.val = 0; // block_oneself_semid initializing 0

        if (semctl(this->block_oneself_semid, num_id, SETVAL, arg) == -1) {
            spdlog::error("Error initializing semaphore in {}", __LINE__);
            this->~SemaphoreSet();

            check_semctl_error();
            exit(1);
        }
    }
#else
    for (const auto &sem_name : sem_names) {
        arg.val = sem_name.second;
        spdlog::trace("semid: {} num_id: {} num_val: {}", this->semid,
          sem_name.first, sem_name.second);
        if (semctl(this->semid, sem_name.first, SETVAL, arg) == -1) {
            spdlog::error("Error initializing semaphore in {}", __LINE__);
            this->~SemaphoreSet();
            check_semctl_error();
            exit(1);
        }

        arg.val = 0; // block_oneself_semid initializing 0

        if (semctl(this->block_oneself_semid, sem_name.first, SETVAL, arg) ==
            -1)
        {
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
  const sem_nameid_min_val_vec_t &sem_op_min_val_vector) {
    /// @note: when resource release and the process goes out of
    /// block to continue, we judge the resource again to avoid if
    /// the resource changed less than 0 during the block period
    ///
    /// @note: see the only goto below
judge_again:
    // this->mantain_atomic(Psemop);

    /// push the smaller one into what_smaller
    std::pair< uint16_t, lap::SemIdToReduce > what_block_me;
    bool is_block = false;

    for (auto &sem_op_with_min_val : sem_op_min_val_vector) {
        if (semctl(this->semid, sem_op_with_min_val.first, GETVAL) <
            sem_op_with_min_val.second.min_val)
        {
            what_block_me = sem_op_with_min_val;
            is_block      = true;
            break;
        }
    }

    if (is_block) {
        (*(this->ptr_record_who_block_me))[what_block_me.first] =
          what_block_me.first;
        spdlog::trace("Who Block sem {}'s var :{}", what_block_me.first,
          (*(this->ptr_record_who_block_me))[what_block_me.first]);
        this->block_oneself_or_release(what_block_me.first, Psemop);

        /// @note: when resource release and the process goes out of
        /// block to continue, we judge the resource again to avoid if
        /// the resource changed less than 0 during the block period
        goto judge_again;
    }
    else {
        for (auto &sem_op_with_min_val : sem_op_min_val_vector) {
            if (sem_op_with_min_val.second.sem_op != 0) {
                /// @note: we have resource to distribute
                this->sem_operation(
                  sem_op_with_min_val.first, sem_op_with_min_val.second.sem_op);
            }
        }
    }
}

void SemaphoreSet::Ssignal(sem_nameid_t sem_numid, int16_t sem_op) {
    spdlog::trace("Release find {}, sem {}'s",
      (*(this->ptr_record_who_block_me))[sem_numid] != -1, sem_numid);
    if ((*(this->ptr_record_who_block_me))[sem_numid] != -1 &&
        semctl(this->block_oneself_semid, sem_numid, GETVAL) <= 0)
    {
        this->block_oneself_or_release(sem_numid, Vsemop);
    }

    this->sem_operation(sem_numid, sem_op);
    spdlog::trace(
      "After Release sem {}'s value: {}", sem_numid, getVal(sem_numid));
}

int32_t SemaphoreSet::getSemid() const { return this->semid; }

int32_t SemaphoreSet::getVal(sem_nameid_t sem_numid) const {
    return semctl(this->semid, sem_numid, GETVAL);
}

SemaphoreSet::~SemaphoreSet() {
    munmap((*ptr_record_who_block_me), num_sems * sizeof(int16_t));
    delete ptr_record_who_block_me;
}

} // namespace lap
