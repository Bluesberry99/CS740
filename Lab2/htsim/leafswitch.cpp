#include "leafswitch.h"
#include <algorithm>

LeafSwitch::LeafSwitch(int id,int n_dst,int n_choices)
: id_(id), D_(n_dst), C_(n_choices),
  to_leaf_(n_dst, std::vector<PathStat>(n_choices)),
  from_leaf_best_(n_dst, 0.0) {}

void LeafSwitch::on_sample(int c, uint64_t deq_bytes, double dt, double link_bps, uint64_t q_bytes){
  if(c<0||c>=C_||dt<=0||link_bps<=0) return;
  double util = (8.0*deq_bytes/dt)/link_bps;                    // 0..1
  double qd   = (8.0*q_bytes)/link_bps;                         // 秒：队列字节/口速率
  for(int d=0; d<D_; ++d){
    auto& s = to_leaf_[d][c];
    s.util_ewma = (1-a_util_)*s.util_ewma + a_util_*util;
    s.qd_ewma   = (1-a_qd_  )*s.qd_ewma   + a_qd_  *qd;
  }
}

void LeafSwitch::on_feedback(int dst_leaf, double remote_best){
  if(dst_leaf>=0 && dst_leaf<D_) from_leaf_best_[dst_leaf] = remote_best;
}

int LeafSwitch::pick_uplink_for(int dst_leaf) const{
  double best=1e9; int bestc=0;
  for(int c=0;c<C_;++c){
    const auto& s = to_leaf_[dst_leaf][c];
    // 组合度量（可按报告调权重；这里简单相加）
    double m_local = s.util_ewma + s.qd_ewma;
    double m = w_loc_*m_local + w_rem_*from_leaf_best_[dst_leaf];
    if(m<best){ best=m; bestc=c; }
  }
  return bestc;
}

double LeafSwitch::best_metric_to(int dst_leaf) const{
  double best=1e9;
  for(int c=0;c<C_;++c){
    const auto& s = to_leaf_[dst_leaf][c];
    best = std::min(best, s.util_ewma + s.qd_ewma);
  }
  return (best<1e9?best:0.0);
}
