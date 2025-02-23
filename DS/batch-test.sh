#!/bin/bash
cd $1
if [ $? -ne 0 ] || [[ ! $1 ]]; then
    echo "Invalid directory '${1}' provided! Could not cd."
    echo "Usage: bash batch-test.sh src/folder"
    exit 1
fi
oneTests=("TestBasic" "TestOneFailure" "TestManyFailures")
twoATests=("Test1")
twoBTests=("TestBasicFail" "TestAtMostOnce" "TestFailPut" "TestConcurrentSame" "TestConcurrentSameUnreliable" "TestRepeatedCrash" "TestRepeatedCrashUnreliable" "TestPartition1" "TestPartition2")
threeATests=("TestBasic" "TestDeaf" "TestForget" "TestManyForget" "TestForgetMem" "TestRPCCount" "TestMany" "TestOld" "TestManyUnreliable" "TestPartition" "TestLots")
threeBTests=("TestBasic"  "TestPartition" "TestUnreliable" "TestHole" "TestDone" "TestManyPartition")
fourATests=("TestBasic" "TestUnreliable" "TestFreshQuery")
fourBTests=("TestBasic" "TestMove" "TestLimp" "TestConcurrent" "TestConcurrentUnreliable")
# 

#
# How many times to run each test case (eg 50)
num_executions=100

echo "Enter $1"
echo "Running these tests ${num_executions} times: ${@:2}"

runTests() {
  echo $1":"
  count=0
  runs=0
  for i in $(seq 1 $num_executions)
  do
    echo -ne "Passed: $count/$runs times"'\r';
    go test -run "^${1}$" -timeout 2m > ./log-${1}-${i}.txt
    result=$(grep -E '^PASS$' log-${1}-${i}.txt| wc -l)
    count=$((count + result))
    runs=$((runs + 1))
    if [ $result -eq 1 ]
    then
      # python3 ../../extract.py ./log-${t}-${i}
      rm ./log-${1}-${i}.txt
    else
      python3 ../../extract.py ./log-${1}-${i}
      rm ./log-${1}-${i}.txt
    fi
  done
  echo -ne "Passed: $count/$runs times"'\n';
}

if [ $1 = src/mapreduce ] || [ $1 = src/mapreduce/ ]
then
  for t in ${oneTests[@]}
  do
    runTests $t
  done
elif [ $1 = src/viewservice ] || [ $1 = src/viewservice/ ]
then
  for t in ${twoATests[@]}
  do
    runTests $t
  done
elif [ $1 = src/pbservice ] || [ $1 = src/pbservice/ ]
then
  for t in ${twoBTests[@]}
  do
    runTests $t
  done
elif [ $1 = src/paxos ] || [ $1 = src/paxos/ ]
then
  for t in ${threeATests[@]}
  do
    runTests $t
  done
elif [ $1 = src/kvpaxos ] || [ $1 = src/kvpaxos/ ]
then
  for t in ${threeBTests[@]}
  do
    runTests $t
  done
elif [ $1 = src/shardmaster  ] || [ $1 = src/shardmaster/ ]
then
  for t in ${fourATests[@]}
  do
    runTests $t
  done
elif [ $1 = src/shardkv  ] || [ $1 = src/shardkv/ ]
then
  for t in ${fourBTests[@]}
  do
    runTests $t
  done
else
  echo "Usage: bash batch-test.sh src/folder"
fi

