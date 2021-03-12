#!/bin/bash
set -euo pipefail

export CTEST_OUTPUT_ON_FAILURE=1
export OMPI_MCA_rmaps_base_oversubscribe=1

for build_type in Release Debug; do
    for equation in elastic viscoelastic2 anisotropic; do
        for precision in double single; do
            dirname="./SeisSol/build_${equation}_${precision}_${build_type}"
            mkdir -p $dirname && cd $dirname
            if [ "$equation" = viscoelastic2 ]; then
                mechanisms=3
            else
                mechanisms=0
            fi
            cmake .. -DNETCDF=OFF -DMETIS=ON -DCOMMTHREAD=ON -DASAGI=OFF -DHDF5=ON \
                -DCMAKE_BUILD_TYPE=$build_type -DTESTING=ON -DLOG_LEVEL=warning \
                -DLOG_LEVEL_MASTER=info -DHOST_ARCH=$HOST_ARCH -DPRECISION=$precision \
                -DEQUATIONS=$equation -DNUMBER_OF_MECHANISMS=$mechanisms
            make -j $NUM_PROC
            make test
            cd ../..
        done
    done
done
set +u
