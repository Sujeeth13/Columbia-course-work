package mapreduce
import "container/list"
import "fmt"

type WorkerInfo struct {
  address string
  // You can add definitions here.
}


// Clean up all workers by sending a Shutdown RPC to each one of them Collect
// the number of jobs each work has performed.
func (mr *MapReduce) KillWorkers() *list.List {
  l := list.New()
  for _, w := range mr.Workers {
    DPrintf("DoWork: shutdown %s\n", w.address)
    args := &ShutdownArgs{}
    var reply ShutdownReply;
    ok := call(w.address, "Worker.Shutdown", args, &reply)
    if ok == false {
      fmt.Printf("DoWork: RPC %s shutdown error\n", w.address)
    } else {
      l.PushBack(reply.Njobs)
    }
  }
  return l
}

func (mr *MapReduce) RunMaster() *list.List {
  // Your code here
  // assign the map workers first
  doneMap := make(chan bool,mr.nMap)
  for i:=0;i<mr.nMap;i++ {
    go func(id int) {
      for {
        wk := <-mr.registerChannel
        res := &DoJobReply{}
        args :=  &DoJobArgs{} 
        args.File = mr.file
        args.JobNumber = id
        args.Operation = Map
        args.NumOtherPhase = mr.nReduce
        success := call(wk,"Worker.DoJob",args,res)
        if success {
          doneMap <- true
          mr.registerChannel <- wk
          break
        }
      }
    }(i)
  }
  for i := 0; i < mr.nMap; i++ {
    <-doneMap
  }
  doneReduce := make(chan bool, mr.nReduce)
  for i:=0;i<mr.nReduce;i++ {
    go func(id int) {
      for {
        wk := <-mr.registerChannel
        res := &DoJobReply{}
        args :=  &DoJobArgs{} 
        args.File = mr.file
        args.JobNumber = id
        args.Operation = Reduce
        args.NumOtherPhase = mr.nMap
        success := call(wk,"Worker.DoJob",args,res)
        if success {
          doneReduce <- true
          mr.registerChannel <- wk
          break
        }
      }
    }(i)
  }
  for i := 0; i < mr.nReduce; i++ {
    <-doneReduce
  }
  return mr.KillWorkers()
}
