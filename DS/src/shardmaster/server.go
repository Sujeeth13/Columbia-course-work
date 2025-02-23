package shardmaster

import "net"
import "fmt"
import "net/rpc"
import "log"
import "paxos"
import "sync"
import "os"
import "syscall"
import "encoding/gob"
import "math/rand"
import crand "crypto/rand"
import "math/big"
import "time"

type ShardMaster struct {
  mu sync.Mutex
  l net.Listener
  me int
  dead bool // for testing
  unreliable bool // for testing
  px *paxos.Paxos

  configs []Config // indexed by config num
  lastDoneSeq int
}


type Op struct {
  // Your data here.
  OpID int64
  OpType string
  GID int64
  ShardID int
  Servers []string
  QueryNum int
}

func nrand() int64 {
  max := big.NewInt(int64(1) << 62)
  bigx, _ := crand.Int(crand.Reader, max)
  x := bigx.Int64()
  return x
}

func (sm *ShardMaster) Join(args *JoinArgs, reply *JoinReply) error {
  // Your code here.
  op := Op{
    OpID : nrand(),
    OpType : "Join",
    GID : args.GID,
    Servers : args.Servers,
  }
  sm.reconfigure(op)
  return nil
}

func (sm *ShardMaster) Leave(args *LeaveArgs, reply *LeaveReply) error {
  // Your code here.
  op := Op{
    OpID : nrand(),
    OpType : "Leave",
    GID : args.GID,
  }
  sm.reconfigure(op)
  return nil
}

func (sm *ShardMaster) Move(args *MoveArgs, reply *MoveReply) error {
  // Your code here.
  op := Op{
    OpID : nrand(),
    OpType : "Move",
    GID : args.GID,
    ShardID : args.Shard,
  }
  sm.reconfigure(op)
  return nil
}

func (sm *ShardMaster) Query(args *QueryArgs, reply *QueryReply) error {
  // Your code here.
  op := Op{
    OpID : nrand(),
    OpType : "Query",
    QueryNum : args.Num,
  }
  sm.reconfigure(op)
  queryNum := args.Num
  if queryNum == -1 || queryNum >= len(sm.configs) {
    queryNum = len(sm.configs)-1
  }
  reply.Config = sm.configs[queryNum]
  return nil
}

func (sm *ShardMaster) reconfigure(op Op) {
  sm.mu.Lock()
  to := 10*time.Millisecond

  for {
    decided,value := sm.px.Status(sm.lastDoneSeq)
    if decided {
      if to > 100*time.Millisecond {
				to = 100 * time.Millisecond
			}
      decidedVal := value.(Op)
      lastConfig := sm.configs[len(sm.configs)-1]
      var newConfig Config
      newConfig.Groups = make(map[int64][]string)
      for k,v := range lastConfig.Groups {
        newConfig.Groups[k] = v
      }
      newConfig.Shards = lastConfig.Shards
      newConfig.Num = lastConfig.Num + 1
      switch decidedVal.OpType {
      case "Join":
        newConfig.Groups[decidedVal.GID] = decidedVal.Servers
        newConfig = sm.assignShards(newConfig)
        sm.configs = append(sm.configs,newConfig)
      case "Leave":
        delete(newConfig.Groups,decidedVal.GID)
        newConfig = sm.assignShards(newConfig)
        sm.configs = append(sm.configs,newConfig)
      case "Move":
        newConfig.Shards[decidedVal.ShardID] = decidedVal.GID
        sm.configs = append(sm.configs,newConfig)
      }
      if decidedVal.OpID == op.OpID {
        break
      }
      sm.lastDoneSeq++
    } else {
      sm.px.Start(sm.lastDoneSeq,op)
      time.Sleep(to)
      if to < 100*time.Millisecond {
				to *= time.Duration(2)
			}
    }
  }
  sm.px.Done(sm.lastDoneSeq)
  sm.lastDoneSeq++
  sm.mu.Unlock()
}

func (sm *ShardMaster) assignShards(config Config) Config {
  if len(config.Groups) == 0 {
    return config
  }
  NGroups := len(config.Groups)
  shardsPerGroup := NShards / NGroups
  remShards := NShards % NGroups

  shardCount := make(map[int64]int)
  for k,v := range config.Shards {
    _, ok := config.Groups[v]
    if !ok || v == 0 {
      if v != 0 {
        config.Shards[k] = 0
      }
      continue
    }
    shardCount[v] += 1
  }
  for k,v := range shardCount {
    if v == 0 {
      continue
    } else if v == shardsPerGroup+1 && remShards > 0 {
      remShards--
    } else if v >= shardsPerGroup+1 {
      excess := v - shardsPerGroup
      if remShards > 0 {
        excess--
        remShards--
      }
      for i,gid := range config.Shards {
        if gid == k {
          config.Shards[i] = 0
          excess--
        }
        if excess == 0 {
          break
        }
      }
    }
  }
  for k,_ := range config.Groups {
    cnt := shardCount[k]
    if cnt < shardsPerGroup || (cnt == shardsPerGroup && remShards > 0) {
      deficit := shardsPerGroup - cnt 
      if remShards > 0 {
        deficit++
        remShards--
      }
      for id,gid := range config.Shards {
        if gid == 0 {
          config.Shards[id] = k
          deficit--
        }
        if deficit == 0 {
          break
        }
      }
    }
  }
  return config
}
// please don't change this function.
func (sm *ShardMaster) Kill() {
  sm.dead = true
  sm.l.Close()
  sm.px.Kill()
}

//
// servers[] contains the ports of the set of
// servers that will cooperate via Paxos to
// form the fault-tolerant shardmaster service.
// me is the index of the current server in servers[].
// 
func StartServer(servers []string, me int) *ShardMaster {
  gob.Register(Op{})

  sm := new(ShardMaster)
  sm.me = me

  sm.configs = make([]Config, 1)
  sm.configs[0].Groups = map[int64][]string{}

  rpcs := rpc.NewServer()
  rpcs.Register(sm)

  sm.px = paxos.Make(servers, me, rpcs)

  os.Remove(servers[me])
  l, e := net.Listen("unix", servers[me]);
  if e != nil {
    log.Fatal("listen error: ", e);
  }
  sm.l = l

  // please do not change any of the following code,
  // or do anything to subvert it.

  go func() {
    for sm.dead == false {
      conn, err := sm.l.Accept()
      if err == nil && sm.dead == false {
        if sm.unreliable && (rand.Int63() % 1000) < 100 {
          // discard the request.
          conn.Close()
        } else if sm.unreliable && (rand.Int63() % 1000) < 200 {
          // process the request but force discard of reply.
          c1 := conn.(*net.UnixConn)
          f, _ := c1.File()
          err := syscall.Shutdown(int(f.Fd()), syscall.SHUT_WR)
          if err != nil {
            fmt.Printf("shutdown: %v\n", err)
          }
          go rpcs.ServeConn(conn)
        } else {
          go rpcs.ServeConn(conn)
        }
      } else if err == nil {
        conn.Close()
      }
      if err != nil && sm.dead == false {
        fmt.Printf("ShardMaster(%v) accept: %v\n", me, err.Error())
        sm.Kill()
      }
    }
  }()

  return sm
}
