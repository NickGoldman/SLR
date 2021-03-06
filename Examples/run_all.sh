#!/usr/bin/env bash
SLR=`realpath -e ../bin/Slr`
prefix=$1
NPARALLEL=1

ls -d -p * | grep '/$' | xargs -n 1 -I %% -P ${NPARALLEL} sh -c "
        echo '* Running %%'
	cd %%
	if [ ! -e 'SKIP_RUN' ]
	then 
	    OPENBLAS_NUM_THREADS=1 time -p stdbuf -o 0 ${SLR} -seed 123 -skipsitewise 1 > ${prefix}.log
	fi
"
