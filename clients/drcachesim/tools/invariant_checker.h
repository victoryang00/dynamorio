/* **********************************************************
 * Copyright (c) 2016-2022 Google, Inc.  All rights reserved.
 * **********************************************************/

/*
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * * Neither the name of Google, Inc. nor the names of its contributors may be
 *   used to endorse or promote products derived from this software without
 *   specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL VMWARE, INC. OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 */

/* invariant_checker: a memory trace invariants checker.
 */

#ifndef _INVARIANT_CHECKER_H_
#define _INVARIANT_CHECKER_H_ 1

#include "analysis_tool.h"
#include "memref.h"
#include <memory>
#include <mutex>
#include <stack>
#include <unordered_map>
#include <vector>

class invariant_checker_t : public analysis_tool_t {
public:
    invariant_checker_t(bool offline = true, unsigned int verbose = 0,
                        std::string test_name = "",
                        std::istream *serial_schedule_file = nullptr,
                        std::istream *cpu_schedule_file = nullptr);
    virtual ~invariant_checker_t();
    std::string
    initialize_stream(memtrace_stream_t *serial_stream) override;
    bool
    process_memref(const memref_t &memref) override;
    bool
    print_results() override;
    bool
    parallel_shard_supported() override;
    void *
    parallel_shard_init_stream(int shard_index, void *worker_data,
                               memtrace_stream_t *shard_stream) override;
    void *
    parallel_shard_init(int shard_index, void *worker_data) override;
    bool
    parallel_shard_exit(void *shard_data) override;
    bool
    parallel_shard_memref(void *shard_data, const memref_t &memref) override;
    std::string
    parallel_shard_error(void *shard_data) override;

protected:
    // For performance we support parallel analysis.
    // Most checks are thread-local; for thread switch checks we rely
    // on identifying thread switch points via timestamp entries.
    struct per_shard_t {
        per_shard_t()
        {
            // We need a sentinel different from type 0.
            prev_xfer_marker_.marker.marker_type = TRACE_MARKER_TYPE_VERSION;
            last_xfer_marker_.marker.marker_type = TRACE_MARKER_TYPE_VERSION;
        }
        memtrace_stream_t *stream = nullptr;
        memref_t prev_entry_ = {};
        memref_t prev_instr_ = {};
        memref_t prev_xfer_marker_ = {}; // Cleared on seeing an instr.
        memref_t last_xfer_marker_ = {}; // Not cleared: just the prior xfer marker.
        addr_t last_retaddr_ = 0;
#ifdef UNIX
        // We only support sigreturn-using handlers so we have pairing: no longjmp.
        std::stack<addr_t> prev_xfer_int_pc_;
        memref_t prev_prev_entry_ = {};
        std::stack<memref_t> pre_signal_instr_;
        // These are only available via annotations in signal_invariants.cpp.
        int instrs_until_interrupt_ = -1;
        int memrefs_until_interrupt_ = -1;
#endif
        bool saw_kernel_xfer_after_prev_instr_ = false;
        bool saw_timestamp_but_no_instr_ = false;
        bool found_cache_line_size_marker_ = false;
        bool found_instr_count_marker_ = false;
        bool found_page_size_marker_ = false;
        uint64_t last_instr_count_marker_ = 0;
        std::string error_;
        // Track the location of errors.
        memref_tid_t tid_ = -1;
        uint64_t ref_count_ = 0;
        // We do not expect these to vary by thread but it is simpler to keep
        // separate values per thread as we discover their values during parallel
        // operation.
        addr_t app_handler_pc_ = 0;
        offline_file_type_t file_type_ = OFFLINE_FILE_TYPE_DEFAULT;
        uintptr_t last_window_ = 0;
        bool window_transition_ = false;
        uint64_t chunk_instr_count_ = 0;
        uint64_t instr_count_ = 0;
        uint64_t last_timestamp_ = 0;
        std::vector<schedule_entry_t> sched_;
        std::unordered_map<uint64_t, std::vector<schedule_entry_t>> cpu2sched_;
        bool skipped_instrs_ = false;
    };

    // We provide this for subclasses to run these invariants with custom
    // failure reporting.
    virtual void
    report_if_false(per_shard_t *shard, bool condition,
                    const std::string &invariant_name);
    virtual void
    check_schedule_data();

    // The keys here are int for parallel, tid for serial.
    std::unordered_map<memref_tid_t, std::unique_ptr<per_shard_t>> shard_map_;
    // This mutex is only needed in parallel_shard_init.  In all other accesses to
    // shard_map (process_memref, print_results) we are single-threaded.
    std::mutex shard_map_mutex_;

    bool knob_offline_;
    unsigned int knob_verbose_;
    std::string knob_test_name_;
    bool has_annotations_ = false;

    std::istream *serial_schedule_file_ = nullptr;
    std::istream *cpu_schedule_file_ = nullptr;

    memtrace_stream_t *serial_stream_ = nullptr;
};

#endif /* _INVARIANT_CHECKER_H_ */
