package shardkv
import "hash/fnv"
import "shardmaster"

//
// Sharded key/value server.
// Lots of replica groups, each running op-at-a-time paxos.
// Shardmaster decides which group serves each shard.
// Shardmaster may change shard assignment from time to time.
//
// You will have to modify these definitions.
//

const (
  OK = "OK"
  ErrNoKey = "ErrNoKey"
  ErrWrongGroup = "ErrWrongGroup"
  ErrWrongConfig = "ErrWrongConfig"
)
type Err string

type SendShardArgs struct {
  DoneReqs map[int64]string
  KV [shardmaster.NShards]map[string]string
  ReqID int64
  ConfigNum int
  ShardIDs []int
}

type SendShardReply struct {
  Err Err
}

type PutArgs struct {
  Key string
  Value string
  DoHash bool  // For PutHash
  // You'll have to add definitions here.
  // Field names must start with capital letters,
  // otherwise RPC will break.
  ReqID int64
  DoneID int64  //IDs which have been done by client for cleanup
}

type PutReply struct {
  Err Err
  PreviousValue string   // For PutHash
}

type GetArgs struct {
  Key string
  // You'll have to add definitions here.
  ReqID int64
  DoneID int64  //IDs which have been done by client for cleanup
}

type GetReply struct {
  Err Err
  Value string
}

func hash(s string) uint32 {
  h := fnv.New32a()
  h.Write([]byte(s))
  return h.Sum32()
}

