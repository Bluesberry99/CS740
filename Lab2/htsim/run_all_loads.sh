#!/usr/bin/env bash
# 自动从 load=0.1 到 1.0 依次执行仿真，并保存日志

# 创建 logs 文件夹（如果不存在）
mkdir -p logs

for load in $(seq 0.1 0.1 1.0); do
  echo "=== Running simulation with load=${load} ==="
  out="logs/ecmp_patero_${load}.log"
  cmd="./htsim --expt=2 --workload=patero --load=${load} --scheme=ecmp --seed=1"

  # 运行仿真，并且：
  # - 输出保存到日志文件
  # - 控制台只显示部分进度（每10万行）
  # - ctrl+C 可以随时中断循环
  ${cmd} 2>&1 | tee "${out}" | awk 'NR % 10 == 0'

  echo ">>> Finished load=${load}, log saved to ${out}"
  echo
done

echo "=== All loads completed ==="
