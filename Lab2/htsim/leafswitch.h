#pragma once
#include <vector>
#include <cstdint>

struct PathStat {
  double util_ewma  = 0.0;   // 利用率 [0..1]
  double qd_ewma    = 0.0;   // 排队时延(秒)的 EWMA（DRE 的简化）
};

class LeafSwitch {
public:
  LeafSwitch(int leaf_id, int n_dst_leaf, int n_choices);
  // 采样器在每次采样时调用：bytes 出队增量、dt 秒、口速率 bps、队列字节数（换算 q-delay）
  void on_sample(int choice, uint64_t deq_bytes, double dt_sec, double link_bps, uint64_t q_bytes);
  // 对端反馈：目的叶 best 度量回报
  void on_feedback(int dst_leaf, double remote_best_metric);
  // 选 uplink（0..n_choices-1）
  int  pick_uplink_for(int dst_leaf) const;
  // 提供给对端作为“我的 best”
  double best_metric_to(int dst_leaf) const;
private:
  int id_, D_, C_;
  std::vector<std::vector<PathStat>> to_leaf_; // [dst][choice]
  std::vector<double> from_leaf_best_;         // [dst]
  double a_util_=0.2, a_qd_=0.2, w_loc_=1.0, w_rem_=1.0;
};
