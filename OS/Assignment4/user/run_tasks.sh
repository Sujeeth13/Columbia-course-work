#!/bin/bash

# Script to launch a list of tasks
# How it works:
# - Change ourselves to SCHED_FIFO at the highest priority,
#   to ensure we get CPU time to launch all our tasks
# - For each task, launch it as SCHED_FIFO but with a slightly
#   lower priority, to ensure that tasks get an equal chance
#   to set its policy and weight
# - Each task is then responsible to set its own scheduling
#   policy and weight using sched_setscheduler
chrt --fifo -p 99 $$

FILE=$1
if [[ -z "$FILE" ]]; then
        echo "Usage: $0 <job file>"
        exit
fi

pids=""
while read line; do
        chrt --fifo 90 ./fibonacci $line &
        pids="$pids $!"
done <<< $(cat $FILE)

echo $pids
wait $pids
