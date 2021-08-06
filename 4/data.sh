#!/bin/bash
for alg in rand fifo custom
do
    for pat in alpha beta gamma delta
    do
        for i in {3..100}
        do
            ./virtmem 100 $i $alg $pat >> data/"$alg"_$pat.txt
        done
    done
done
