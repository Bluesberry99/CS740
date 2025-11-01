# 保存为 summarize_fct.sh；然后: chmod +x summarize_fct.sh
#!/usr/bin/env bash
# 用法：
#   ./summarize_fct.sh logs/run.out
#   ./summarize_fct.sh logs/*.out

echo "file,flows_total,fct_total_us,fct_avg_all_us,flows_lt_100KB,fct_avg_lt_100KB_us,flows_gt_100KB,fct_avg_gt_100KB_us"

for f in "$@"; do
  awk -v fname="$f" '
  BEGIN{
    LT100 = 100*1024;  # 100 KB = 102,400 bytes
    GT100 = 100*1024;
  }
  $1=="Flow" {
    sizeB=0; fct=0;
    for(i=1;i<=NF;i++){
      if($i=="size"){ sizeB=$(i+1)+0 }
      if($i=="fct"){  fct=$(i+1)+0 }
    }
    if(fct==0 || sizeB==0) next;

    # 全部
    total_fct += fct; total_n++;

    # size < 100KB
    if(sizeB < LT100){ fct_lt += fct; n_lt++ }

    # size > 100KB
    if(sizeB > GT100){ fct_gt += fct; n_gt++ }
  }
  END{
    if(total_n==0){
      printf "%s,0,0,0,0,0,0,0\n", fname;
      exit
    }
    avg_all  = total_fct/total_n;
    avg_lt   = (n_lt ? fct_lt/n_lt : 0);
    avg_gt   = (n_gt ? fct_gt/n_gt : 0);

    # CSV 行
    printf "%s,%d,%.6f,%.6f,%d,%.6f,%d,%.6f\n",
           fname, total_n, total_fct, avg_all, n_lt, avg_lt, n_gt, avg_gt;
  }' "$f"
done
