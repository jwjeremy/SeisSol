/**
 * @file
 * This file is part of SeisSol.
 *
 * @author Sebastian Rettenberger (sebastian.rettenberger AT tum.de,
 * http://www5.in.tum.de/wiki/index.php/Sebastian_Rettenberger)
 *
 * @section LICENSE
 * Copyright (c) 2014-2016, SeisSol Group
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE  USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * @section DESCRIPTION
 * Main C++ SeisSol file
 */

#include "SeisSol.h"

#include <climits>
#include <sys/resource.h>

#ifdef _OPENMP
#include <omp.h>
#endif // _OPENMP

#include "utils/args.h"

#include "Initializer/Parameters/SeisSolParameters.h"
#include "Modules/Modules.h"
#include "Monitoring/Unit.hpp"
#include "Parallel/Helper.hpp"
#include "Parallel/MPI.h"
#include "Parallel/Pin.h"
#include "SeisSol.h"

// Autogenerated file
#include "version.h"

bool seissol::SeisSol::init(int argc, char* argv[]) {
  const int rank = MPI::mpi.rank();

  // Print welcome message
  logInfo(rank) << "Welcome to SeisSol";
  logInfo(rank) << "Copyright (c) 2012 -" << COMMIT_YEAR << " SeisSol Group";
  logInfo(rank) << "Version:" << VERSION_STRING;
  logInfo(rank) << "Built on:" << __DATE__ << __TIME__;
#ifdef COMMIT_HASH
  logInfo(rank) << "Last commit:" << COMMIT_HASH << "at" << COMMIT_TIMESTAMP;
#endif
  logInfo(rank) << "Compiled with HOST_ARCH =" << SEISSOL_HOST_ARCH;
#ifdef ACL_DEVICE
  logInfo(rank) << "Compiled with DEVICE_BACKEND =" << SEISSOL_DEVICE_BACKEND;
  logInfo(rank) << "Compiled with DEVICE_ARCH =" << SEISSOL_DEVICE_ARCH;
#endif

  if (MPI::mpi.rank() == 0) {
    const auto& hostNames = MPI::mpi.getHostNames();
    logInfo() << "Running on (rank=0):" << hostNames.front();
  }

#ifdef USE_MPI
  logInfo(rank) << "Using MPI with #ranks:" << MPI::mpi.size();
  logInfo(rank) << "Node-wide (shared memory) MPI with #ranks/node:" << MPI::mpi.sharedMemMpiSize();
  MPI::mpi.printAcceleratorDeviceInfo();
  // TODO (Ravil, David): switch to reading MPI options from the parameter-file.
  MPI::mpi.setDataTransferModeFromEnv();

  printPersistentMpiInfo(MPI::mpi);
#endif
#ifdef _OPENMP
  pinning.checkEnvVariables();
  logInfo(rank) << "Using OMP with #threads/rank:" << omp_get_max_threads();
  logInfo(rank) << "OpenMP worker affinity (this process):"
                << parallel::Pinning::maskToString(pinning.getWorkerUnionMask());
  logInfo(rank) << "OpenMP worker affinity (this node)   :"
                << parallel::Pinning::maskToString(pinning.getNodeMask());

  seissol::printCommThreadInfo(MPI::mpi);
  if (seissol::useCommThread(MPI::mpi)) {
    auto freeCpus = pinning.getFreeCPUsMask();
    logInfo(rank) << "Communication thread affinity        :"
                  << parallel::Pinning::maskToString(freeCpus);
    if (parallel::Pinning::freeCPUsMaskEmpty(freeCpus)) {
      logError()
          << "There are no free CPUs left. Make sure to leave one for the communication thread. If "
             "you want to run SeisSol without a communication thread (and instead use polling), "
             "then try running with the environment variable \"SEISSOL_COMMTHREAD=0\". ";
    }
  }
#endif // _OPENMP

  // Check if the ulimit for the stacksize is reasonable.
  // A low limit can lead to segmentation faults.
  rlimit rlim;
  if (getrlimit(RLIMIT_STACK, &rlim) == 0) {
    const auto rlimInKb = rlim.rlim_cur / 1024;
    // Softlimit (rlim_cur) is enforced by the kernel.
    // This limit is pretty arbitrarily set to 2GiB.
    constexpr auto reasonableStackLimitInKb = 0x200'000ULL;                    // [kiB] (2 GiB)
    constexpr auto reasonableStackLimit = reasonableStackLimitInKb * 0x400ULL; // [B] (2 GiB)
    if (rlim.rlim_cur == RLIM_INFINITY) {
      logInfo(rank) << "The stack size ulimit is unlimited.";
    } else {
      logInfo(rank) << "The stack size ulimit is" << rlimInKb
                    << "[kiB] ( =" << UnitByte.formatPrefix(rlim.rlim_cur).c_str() << ").";
    }
    if (rlim.rlim_cur < reasonableStackLimit) {
      logWarning(rank)
          << "Stack size of" << rlimInKb
          << "[kiB] ( =" << UnitByte.formatPrefix(rlim.rlim_cur).c_str()
          << ") is lower than recommended minimum of" << reasonableStackLimitInKb
          << "[kiB] ( =" << UnitByte.formatPrefix(reasonableStackLimit).c_str() << ")."
          << "You can increase the stack size by running the command: ulimit -Ss unlimited.";
    }
  } else {
    logError() << "Stack size cannot be determined because getrlimit syscall failed!";
  }

  // Call post MPI initialization hooks
  seissol::Modules::callHook<ModuleHook::PostMPIInit>();

  // Initialize the ASYNC I/O library
  if (!m_asyncIO.init()) {
    return false;
  }

  m_memoryManager->initialize();

  m_memoryManager->setInputParams(
      std::make_shared<seissol::initializer::parameters::SeisSolParameters>(m_seissolParameters));

  return true;
}

void seissol::SeisSol::finalize() {
  // Cleanup ASYNC I/O library
  m_asyncIO.finalize();

  Modules::callHook<ModuleHook::Shutdown>();

  const int rank = MPI::mpi.rank();

  m_timeManager.freeDynamicResources();

  MPI::mpi.finalize();

  logInfo(rank) << "SeisSol done. Goodbye.";
}

void seissol::SeisSol::setBackupTimeStamp(const std::string& stamp) {
  m_backupTimeStamp = stamp;
  MPI::mpi.broadcastContainer(m_backupTimeStamp, 0);
}

// seissol::SeisSol seissolInstance;
