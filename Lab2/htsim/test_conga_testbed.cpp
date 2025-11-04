#include "eventlist.h"
#include "logfile.h"
#include "loggers.h"
#include "aprx-fairqueue.h"
#include "fairqueue.h"
#include "priorityqueue.h"
#include "stoc-fairqueue.h"
#include "flow-generator.h"
#include "pipe.h"
#include "workloads.h"
#include "test.h"
#include "htsim.h"

#include "leafswitch.h"   
//#include "conga_hdr.h"    

#include <memory>
#include <cstdlib>   // rand()
#include <string>
#include <unordered_map>
#include <cmath>

using namespace std;

// ----------------- 固定拓扑规格 -----------------
// 12 Core × (each 24×40G)，24 Leaf × (12×40G 上联 + 32×10G 下联)
static const int N_CORE   = 12;
static const int N_LEAF   = 24;
static const int N_UL     = 12;   // uplinks per leaf
static const int N_SRV    = 32;   // servers per leaf

static const linkspeed_bps CORE40G = 40000000000ULL;
static const linkspeed_bps SRV10G  = 10000000000ULL;

static const uint64_t BUF40G = 1024*1024;   // ~1MB
static const uint64_t BUF10G = 512*1024;    // ~512KB
static const uint64_t ENDH_BUFFER = 8*1024*1024;

// ----------------- 全局拓扑对象（只做演示级别） -----------------
// Leaf <-> Core (12×24×双向)
static Queue* qLeafCore[N_LEAF][N_UL];  static Pipe* pLeafCore[N_LEAF][N_UL];
static Queue* qCoreLeaf[N_UL][N_LEAF];  static Pipe* pCoreLeaf[N_UL][N_LEAF];

// Server <-> Leaf (24×32×双向)
static Queue* qSrvLeaf[N_LEAF][N_SRV];  static Pipe* pSrvLeaf[N_LEAF][N_SRV];
static Queue* qLeafSrv[N_LEAF][N_SRV];  static Pipe* pLeafSrv[N_LEAF][N_SRV];

// 每个 Leaf 一个“智脑”
static LeafSwitch* leafs[N_LEAF] = {nullptr};

static inline void srv2ls(uint32_t sid, int& l, int& s){ l = sid / N_SRV; s = sid % N_SRV; }

static void build_route(uint32_t src, uint32_t dst, int core_k, route_t*& fwd, route_t*& rev){
  int sl,ss, dl,ds; srv2ls(src,sl,ss); srv2ls(dst,dl,ds);
  fwd = new route_t(); rev = new route_t();
  // Srv→Leaf
  fwd->push_back(qSrvLeaf[sl][ss]);  fwd->push_back(pSrvLeaf[sl][ss]);
  // Leaf(sl)→Core(core_k)
  fwd->push_back(qLeafCore[sl][core_k]); fwd->push_back(pLeafCore[sl][core_k]);
  // Core(core_k)→Leaf(dl)
  fwd->push_back(qCoreLeaf[core_k][dl]); fwd->push_back(pCoreLeaf[core_k][dl]);
  // Leaf→Srv(dst)
  fwd->push_back(qLeafSrv[dl][ds]);  fwd->push_back(pLeafSrv[dl][ds]);

  // 反向镜像
  rev->push_back(qSrvLeaf[dl][ds]);  rev->push_back(pSrvLeaf[dl][ds]);
  rev->push_back(qLeafCore[dl][core_k]); rev->push_back(pLeafCore[dl][core_k]);
  rev->push_back(qCoreLeaf[core_k][sl]); rev->push_back(pCoreLeaf[core_k][sl]);
  rev->push_back(qLeafSrv[sl][ss]);  rev->push_back(pLeafSrv[sl][ss]);
}

static Queue* make_q(Logfile& logfile, linkspeed_bps spd, uint64_t buf) {
    auto* l = new QueueLoggerSampling(timeFromUs(100));
    logfile.addLogger(*l);
    return new Queue(spd, buf, l);
}

static FairQueue* make_fq(Logfile& logfile, linkspeed_bps spd, uint64_t buf) {
    auto* l = new QueueLoggerSampling(timeFromUs(100));
    logfile.addLogger(*l);
    return new FairQueue(spd, buf, l);
}


// 采样器：每 period_ps 触发一次，遍历所有 leaf 的 12 条上联
class LeafSampler : public EventSource {
public:
  explicit LeafSampler(simtime_picosec period_ps)
    : EventSource("LeafSampler"), period_(period_ps) {
    for (int l = 0; l < N_LEAF; ++l)
      for (int k = 0; k < N_UL; ++k)
        last_deq_[l][k] = 0;
    EventList::Get().sourceIsPendingRel(*this, period_);
  }

  void doNextEvent() override {
    double dt = timeAsSec(period_);   // seconds

    for (int l = 0; l < N_LEAF; ++l) {
      for (int k = 0; k < N_UL; ++k) {
        // TODO: 若 Queue 暴露接口，可读出真实队列长度/出队增量
        uint64_t qbytes = 0;  // 先置 0，跑通后再接入真实值
        uint64_t deq    = 0;  // 同上；仅用 qbytes 推 qdelay 也可
        leafs[l]->on_sample(k, deq, dt, (double)CORE40G, qbytes);
        last_deq_[l][k] += deq;
      }
    }

    EventList::Get().sourceIsPendingRel(*this, period_);
  }

private:
  simtime_picosec period_;
  uint64_t last_deq_[N_LEAF][N_UL];
};


// 在构网完成后挂上采样器
static void attach_leaf_samplers() {
  new LeafSampler(timeFromUs(50));  // 每 50us 采一次
}

// ----------------- 选路策略开关 -----------------
static inline uint32_t flow_hash(uint32_t a, uint32_t b){
  uint64_t x = ((uint64_t)a<<32) ^ (uint64_t)b ^ 0x9e3779b97f4a7c15ULL;
  x ^= (x>>30); x*=0xbf58476d1ce4e5b9ULL; x ^= (x>>27); x*=0x94d049bb133111ebULL; x^=(x>>31);
  return (uint32_t)x;
}

static void gen_route_ecmp(route_t*& fwd, route_t*& rev, uint32_t& src, uint32_t& dst){
  do { src = rand()%(N_LEAF*N_SRV); dst = rand()%(N_LEAF*N_SRV);} while(src==dst);
  int k = flow_hash(src,dst) % N_UL;                    // 按 hash 选 12 个 Core 之一
  build_route(src, dst, k, fwd, rev);
}

static void gen_route_conga(route_t*& fwd, route_t*& rev, uint32_t& src, uint32_t& dst){
  do { src = rand()%(N_LEAF*N_SRV); dst = rand()%(N_LEAF*N_SRV);} while(src==dst);
  int sl,ss, dl,ds; srv2ls(src,sl,ss); srv2ls(dst,dl,ds);
  int k = leafs[sl]->pick_uplink_for(dl);               // 让源 Leaf 决策
  build_route(src, dst, k, fwd, rev);
  // 简化的“搭车反馈”：目的叶把“自己视角的 best 度量”回写给源叶
  leafs[sl]->on_feedback(dl, leafs[dl]->best_metric_to(sl));
}

// ----------------- 主入口：conga_testbed -----------------
void conga_testbed(const ArgList& args, Logfile& logfile){
  // 参数 & 默认值
  std::string workload="uniform", scheme="ecmp", outcsv="logs/out.csv";
  double load=0.5; uint32_t seed=1;
  parseString(args,"workload",workload);
  parseDouble(args,"load",load);
  parseString(args,"scheme",scheme);
  parseInt(args,"seed",seed);
  parseString(args,"out",outcsv);
  srand(seed);

  Workloads::FlowDist flowdist = Workloads::UNIFORM;
  if(workload=="pareto") flowdist=Workloads::PARETO;
  else if(workload=="enterprise") flowdist=Workloads::ENTERPRISE;
  else if(workload=="datamining") flowdist=Workloads::DATAMINING;

  EventList& ev = EventList::Get();
  ev.setEndtime(timeFromSec(0.1));
  //auto* qlog = new QueueLoggerSampling(timeFromUs(100)); logfile.addLogger(*qlog);

  // 构网：每个 leaf 对 12 个 core 各有一条 40G 上联；每个 leaf 下接 32 台 10G 服务器
  for(int l=0;l<N_LEAF;++l){
    leafs[l] = new LeafSwitch(l, N_LEAF, N_UL);                 // N_LEAF 目的叶 × 12 选择
    for(int k=0;k<N_UL;++k){                                    // leaf↔core[k]
      qLeafCore[l][k] = make_q(logfile, CORE40G, BUF40G);  pLeafCore[l][k] = new Pipe(timeFromUs(1));
      qCoreLeaf[k][l] = make_q(logfile, CORE40G, BUF40G);  pCoreLeaf[k][l] = new Pipe(timeFromUs(1));
    }
    for(int s=0;s<N_SRV;++s){                                   // server↔leaf
      qSrvLeaf[l][s]  = make_fq(logfile, SRV10G, BUF10G); pSrvLeaf[l][s] = new Pipe(timeFromUs(0.5));
      qLeafSrv[l][s]  = make_q(logfile, SRV10G, BUF10G);     pLeafSrv[l][s] = new Pipe(timeFromUs(0.5));
    }
  }

  // （关键）给每个 leaf 装一个“采样器”，定期把 12 条上联的排队/发送信息喂给 LeafSwitch（见下节）
attach_leaf_samplers();



  // 选择生成器
  std::function<void(route_t*&,route_t*&,uint32_t&,uint32_t&)> gen =
    (scheme=="conga") ? gen_route_conga : gen_route_ecmp;

  // offered load（聚合瓶颈近似）= 24 leaf × 12 上联 × 40G × load
  double offered_bps = (double)CORE40G * (N_LEAF * N_UL) * load;

  // FlowGenerator（风格同 single-link / fat-tree）
  uint32_t avg_size = 100000; // 100KB
  auto* fg = new FlowGenerator(
    DataSource::EndHost::TCP,   // 你这套库里如需换成 DataSource::TCP 就换
    gen,
    (linkspeed_bps)offered_bps,
    avg_size,
    flowdist
  );
  fg->setEndhostQueue(CORE40G, ENDH_BUFFER);
  //fg->setTimeLimits(timeFromUs(1), timeFromSec(10)-timeFromSec(1));
  fg->setTimeLimits(timeFromSec(0), timeFromSec(0.18));
  // TODO：在你的 Logger/回调里把每个完成流落 CSV 到 outcsv（或先用 grep “Flow ...” 导出）
}

