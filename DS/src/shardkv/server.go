package shardkv

import "net"
import "net/rpc"
import "log"
import "time"
import "paxos"
import "sync"
import "os"
import "syscall"
import "encoding/gob"
import "math/rand"
import "shardmaster"
import "strconv"

const Debug=1

func DPrintf(format string, a ...interface{}) (n int, err error) {
        if Debug > 0 {
                log.Printf(format, a...)
        }
        return
}


type Op struct {
  // Your definitions here.
  Operation string
  Key string
  Value string
  DoHash bool
  ReqID int64
  N int
  ShardIDs []int
  Config shardmaster.Config
  ExpiredShards [shardmaster.NShards]bool
  OwnedShards [shardmaster.NShards]bool
  SendShards map[int64][]int 
  ShardsData [shardmaster.NShards]map[string]string
  Cache map[int64]string
}


type ShardKV struct {
  mu sync.Mutex
  l net.Listener
  me int
  dead bool // for testing
  unreliable bool // for testing
  sm *shardmaster.Clerk
  px *paxos.Paxos

  gid int64 // my replica group ID

  // Your definitions here.

  doneReqs map[int64]string // cache for serving duplicate reqs
  kvstore [shardmaster.NShards]map[string]string
  expiredShards [shardmaster.NShards]bool
  ownedShards [shardmaster.NShards]bool
  lastDoneReq int
  currConfig int
  Groups map[int64][]string
  config shardmaster.Config
  N int
}

func (kv *ShardKV) Get(args *GetArgs, reply *GetReply) error {
  // Your code here.
  kv.mu.Lock()
  // if kv.config.Num == 0 {
  //   kv.config = kv.sm.Query(-1)
  // }
  shard := key2shard(args.Key)
  // in the wrong replica group
  if kv.ownedShards[shard] == false || kv.expiredShards[shard] == true {
    DPrintf("EXIT::WRONG GROUP\n")
    DPrintf("Me: %v & %v & %v",kv.gid,kv.currConfig,shard)
    for i:=0;i<shardmaster.NShards;i++ {
      DPrintf("OWN: %v, EXP: %v\n",kv.ownedShards[i],kv.expiredShards[i])
    }
    reply.Err = ErrWrongGroup
    kv.mu.Unlock()
    return nil
  }

  op := Op{
    Operation: "Get",
    Key: args.Key,
    ReqID: args.ReqID,
  }
  
  DPrintf("Test: Apply get ...\n")
  reply.Value = kv.applyChanges(op)
  reply.Err = OK
  if reply.Value == ErrWrongGroup {
		reply.Err = ErrWrongGroup
    reply.Value = ""
	}
  kv.mu.Unlock()
  // kv.Cleanup(args.DoneID)
  return nil
}

func (kv *ShardKV) Put(args *PutArgs, reply *PutReply) error {
  // Your code here.
  kv.mu.Lock()
  // if kv.config.Num == 0 {
  //   kv.config = kv.sm.Query(-1)
  // }
  shard := key2shard(args.Key)
  // in the wrong replica group
  if !kv.ownedShards[shard] || kv.expiredShards[shard] {
    reply.Err = ErrWrongGroup
    kv.mu.Unlock()
    return nil
  }
  op := Op{
    Operation: "Put",
    Key: args.Key,
    Value: args.Value,
    DoHash: args.DoHash,
    ReqID: args.ReqID,
  }

  reply.PreviousValue = kv.applyChanges(op)
  reply.Err = OK
  if reply.PreviousValue == ErrWrongGroup {
		reply.Err = ErrWrongGroup
    reply.PreviousValue = ""
	}
  kv.mu.Unlock()
  // kv.Cleanup(args.DoneID)
  return nil
}

// func (kv *ShardKV) GetShard(args *GetShardArgs, reply *GetShardReply) error {
//   // Prepare shard data
//   shardData := ShardData{
//       KV:        make(map[string]string),
//       doneReqs: make(map[int64]string),
//   }
//   for key, value := range kv.kvstore {
//       if key2shard(key) == args.Shard {
//           shardData.KV[key] = value
//       }
//   }

//   for id, val := range kv.doneReqs {
//     shardData.doneReqs[id] = val
//   }

//   reply.Data = shardData
//   reply.Err = OK
//   return nil
// }
func (kv *ShardKV) GetShards(args *SendShardArgs, reply *SendShardReply) error {
	if kv.N > args.ConfigNum {
		reply.Err = OK
		return nil
	}
  op := Op{
    Operation: "Recieve",
    ShardsData: args.KV,
    ReqID: args.ReqID,
    Cache: args.DoneReqs,
    ShardIDs: args.ShardIDs,
    N: args.ConfigNum,
  }
	kv.mu.Lock()
	kv.applyChanges(op)
	kv.mu.Unlock()
	reply.Err = OK
	return nil
}

func (kv *ShardKV) applyChanges(op Op) string {
  to := 10*time.Millisecond
  // kv.mu.Lock()
  // check in donereqs cache to ans duplicates
  cacheVal, exists := kv.doneReqs[op.ReqID]
  if exists {
    // kv.mu.Unlock()
    return cacheVal
  }
  var ret string
  for {
    decided, value := kv.px.Status(kv.lastDoneReq)
    if decided {
      // DPrintf("Decided the value\n")
      if to > 100*time.Millisecond {
				to = 100 * time.Millisecond
			}
      decidedVal := value.(Op)
      ret = ""
      switch decidedVal.Operation {
      case "Get":
        shard := key2shard(decidedVal.Key)
        if kv.ownedShards[shard] || !kv.expiredShards[shard]{
          ret = kv.kvstore[shard][decidedVal.Key]
        } else {
          ret = ErrWrongGroup
        }
      case "Put":
        DPrintf("Do the Put\n")
        shard := key2shard(decidedVal.Key)
        if decidedVal.DoHash {
          if kv.ownedShards[shard] || !kv.expiredShards[shard]{
            ret = kv.kvstore[shard][decidedVal.Key]
            kv.kvstore[shard][decidedVal.Key] = strconv.Itoa(int(hash(ret + decidedVal.Value)))
          } else {
            ret = ErrWrongGroup
          }
        } else {
          if kv.ownedShards[shard] || !kv.expiredShards[shard]{
            kv.kvstore[shard][decidedVal.Key] = decidedVal.Value
          } else {
            ret = ErrWrongGroup
          }
        }
      case "Reconfig":
        kv.handleReconfig(decidedVal)
      case "Receive":
        for k,v := range decidedVal.Cache {
          kv.doneReqs[k] = v
        }
        for _,s := range decidedVal.ShardIDs {
          kv.kvstore[s] = decidedVal.ShardsData[s]
          kv.expiredShards[s] = false
        }
        kv.doneReqs[decidedVal.ReqID] = ret
        if op.Operation != "Reconfig" && op.Operation != "Receive" {
          s := key2shard(op.Key)
          if kv.expiredShards[s] {
            return ErrWrongGroup
          }
        }
      }
      // cache the value to ans duplicates
      if ret != ErrWrongGroup {
        kv.doneReqs[decidedVal.ReqID] = ret
      }
      if op.ReqID == decidedVal.ReqID {
        break
      }
      kv.lastDoneReq++
    } else {
      // DPrintf("Start the value\n")
      kv.px.Start(kv.lastDoneReq,op)
      time.Sleep(to)
      // do exponential backoff to reduce rpc counts (need to tweak it!!)
      if to < 100*time.Millisecond {
				to *= time.Duration(2)
			}
    }
  }
  // kv.px.Done(kv.lastDoneReq)
  kv.lastDoneReq++
  // kv.mu.Unlock()
  return ret
}

func (kv *ShardKV) prepareSendShards(op Op) {
  sendShards := op.SendShards
  for gid,shards := range sendShards {
    go func(ID int64,N int,gid int64, shards []int) {
      kv.mu.Lock()
      args := SendShardArgs{}
      args.ConfigNum = N
      args.ReqID = ID
      args.ShardIDs = shards
      for _,s := range shards {
				args.KV[s] = make(map[string]string)
				for k, v := range kv.kvstore[s] {
					args.KV[s][k] = v
				}
			}
      args.DoneReqs = make(map[int64]string)
      for k,v := range kv.doneReqs {
        args.DoneReqs[k] = v
      }
      kv.mu.Unlock()
      for {
				var reply SendShardReply
        ok := false
				for _, server := range kv.Groups[gid] {
					ok = call(server, "ShardKV.GetShards", args, &reply)
				}
        if ok {
          if reply.Err == OK || reply.Err == ErrWrongGroup {
            break
          }
        }
			}
    }(op.ReqID,op.N,gid,shards)
  }
}

func (kv *ShardKV) handleReconfig(op Op) {
  kv.doneReqs[op.ReqID] = ""
  if kv.N < op.N {
    kv.prepareSendShards(op)
    kv.ownedShards = op.OwnedShards
    kv.N = op.N
    for i := 0; i < shardmaster.NShards; i++ {
      if kv.expiredShards[i] == false && op.ExpiredShards[i] == false {
        kv.expiredShards[i] = false
      } else {
        kv.expiredShards[i] = true
      }
    }
  }
  // oldConfig := kv.config
  // kv.config = config
  // if oldConfig.Num >= config.Num {
  //   return
  // }
  // shardsToGet := []int{}
  // for shard, gid := range config.Shards {
  //   if gid == kv.gid && oldConfig.Shards[shard] != kv.gid {
  //     shardsToGet = append(shardsToGet,shard)
  //   }
  // }
  // // make server a client and send RPC to get shard from curr owner
  // for _,shard := range shardsToGet {
  //   oGID := oldConfig.Shards[shard]
  //   servers := oldConfig.Groups[oGID]
  //   shardReceived := false
  //   for !shardReceived{
  //     var shardData ShardData
  //     for _,server := range servers {
  //         args := &GetShardArgs{
  //             Shard: shard,
  //         }
  //         var reply GetShardReply
  //         ok := call(server, "ShardKV.GetShard", args, &reply)
  //         if ok && reply.Err == OK {
  //             shardData = reply.Data
  //             shardReceived = true
  //             break
  //         }
  //     }
  //     // merge the data if received
  //     if shardReceived {
  //       for key, value := range shardData.KV {
  //         kv.kvstore[key] = value
  //       }
  //       for id,val := range shardData.doneReqs {
  //         kv.doneReqs[id] = val
  //       }
  //     }
  //   }
  // }
}
//
// Ask the shardmaster if there's a new configuration;
// if so, re-configure.
//
func (kv *ShardKV) tick() {

  // newConfig := kv.sm.Query(-1)
  // if newConfig.Num > kv.config.Num { // newconfig so do reconfiguration
  //   op := Op{
  //     Operation: "Reconfig",
  //     Config: newConfig,
  //     ReqID: nrand(),
  //   }
  //   kv.applyChanges(op)
  // }
  kv.mu.Lock()
  defer kv.mu.Unlock()

	newConfig := kv.sm.Query(kv.currConfig)
  kv.currConfig += 1
  kv.Groups = newConfig.Groups
	if newConfig.Shards[0] == kv.gid && newConfig.Num == 1 {
    // DPrintf("Init Shards!!!!!")
		for i := 0; i < shardmaster.NShards; i++ {
			kv.ownedShards[i] = true
      kv.expiredShards[i] = false
		}
	}
  var newOwned [shardmaster.NShards]bool
  var newExpired [shardmaster.NShards]bool
  shardIDs := make(map[int64][]int)
  for shard,gid := range newConfig.Shards {
    if gid == kv.gid {
      newOwned[shard] = true
    } else {
      newOwned[shard] = false
      newExpired[shard] = true
      if kv.ownedShards[shard] {
        shardIDs[gid] = append(shardIDs[gid], shard)
      }
    }
  }
  if newConfig.Num >= kv.N {
    kv.config = newConfig
    op := Op{
      Operation: "Reconfig",
      ReqID: nrand(),
      Config: newConfig,
      SendShards: shardIDs,
      OwnedShards: newOwned,
      ExpiredShards : newExpired,
      N: newConfig.Num,
    }
    kv.applyChanges(op)
  }
}

func (kv *ShardKV) Cleanup(ID int64) {
  if ID == 0 {
    return
  }
  kv.mu.Lock()
  defer kv.mu.Unlock()
  _, exists := kv.doneReqs[ID]
  if exists {
    delete(kv.doneReqs,ID)
  }
}

// tell the server to shut itself down.
func (kv *ShardKV) kill() {
  kv.dead = true
  kv.l.Close()
  kv.px.Kill()
}

//
// Start a shardkv server.
// gid is the ID of the server's replica group.
// shardmasters[] contains the ports of the
//   servers that implement the shardmaster.
// servers[] contains the ports of the servers
//   in this replica group.
// Me is the index of this server in servers[].
//
func StartServer(gid int64, shardmasters []string,
                 servers []string, me int) *ShardKV {
  gob.Register(Op{})

  kv := new(ShardKV)
  kv.me = me
  kv.gid = gid
  kv.sm = shardmaster.MakeClerk(shardmasters)

  // Your initialization code here.
  kv.doneReqs = make(map[int64]string)
  kv.Groups = make(map[int64][]string)
  kv.currConfig = 0
  kv.N = -1
  kv.lastDoneReq = 0
  // Don't call Join().
	for k := 0; k < shardmaster.NShards; k++ {
		kv.kvstore[k] = make(map[string]string)
		kv.expiredShards[k] = true
	}
  rpcs := rpc.NewServer()
  rpcs.Register(kv)

  kv.px = paxos.Make(servers, me, rpcs)

  os.Remove(servers[me])
  l, e := net.Listen("unix", servers[me]);
  if e != nil {
    log.Fatal("listen error: ", e);
  }
  kv.l = l

  // please do not change any of the following code,
  // or do anything to subvert it.

  go func() {
    for kv.dead == false {
      conn, err := kv.l.Accept()
      if err == nil && kv.dead == false {
        if kv.unreliable && (rand.Int63() % 1000) < 100 {
          // discard the request.
          conn.Close()
        } else if kv.unreliable && (rand.Int63() % 1000) < 200 {
          // process the request but force discard of reply.
          c1 := conn.(*net.UnixConn)
          f, _ := c1.File()
          err := syscall.Shutdown(int(f.Fd()), syscall.SHUT_WR)
          if err != nil {
            DPrintf("shutdown: %v\n", err)
          }
          go rpcs.ServeConn(conn)
        } else {
          go rpcs.ServeConn(conn)
        }
      } else if err == nil {
        conn.Close()
      }
      if err != nil && kv.dead == false {
        DPrintf("ShardKV(%v) accept: %v\n", me, err.Error())
        kv.kill()
      }
    }
  }()

  go func() {
    for kv.dead == false {
      kv.tick()
      time.Sleep(250 * time.Millisecond)
    }
  }()

  return kv
}
