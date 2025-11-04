// /*
//  * Flow generator header
//  */
// #ifndef FLOW_GENERATOR_H
// #define FLOW_GENERATOR_H

// #include "eventlist.h"
// #include "loggers.h"
// #include "network.h"
// #include "datasource.h"
// #include "tcp.h"
// #include "packetpair.h"
// #include "timely.h"
// #include "workloads.h"
// #include "prof.h"

// #include <deque>
// #include <functional>

// /* Route generator function. */
// //typedef std::function<void(route_t *&, route_t *&, uint32_t &, uint32_t &, uint64_t, simtime_picosec)> route_gen_t;
// typedef std::function<void(route_t *&, route_t *&, uint32_t &, uint32_t &)> route_gen_t;

// class FlowGenerator : public EventSource
// {
//     public:
//         FlowGenerator(DataSource::EndHost endhost, route_gen_t rg, linkspeed_bps flowRate,
//                 uint32_t avgFlowSize, Workloads::FlowDist flowSizeDist);
//         void doNextEvent(); 

//         /* Set flow generation start time. */
//         void setTimeLimits(simtime_picosec startTime, simtime_picosec endTime);

//         /* Creates a separate endhost queue for every flow. */
//         void setEndhostQueue(linkspeed_bps qRate, uint64_t qBuffer);

//         /* Fixes max flows in the systems and replaces them when finished. */
//         void setReplaceFlow(uint32_t maxFlows, double offRatio);

//         /* Appends a prefix to flow names to differetiate from other generators. */
//         void setPrefix(std::string prefix);

//         /* Flow arrival using a trace instead of dynamic generation during simulation. */
//         void setTrace(std::string filename);

//         /* Used by Source to notify the Generator of flow finishing, which can then
//          * (optionally) generate a new flow. */
//         void finishFlow(uint32_t flow_id);
//         void dumpLiveFlows();

//     private:
//         // Creates a flow in the simulation.
//         void createFlow(uint64_t flowSize, simtime_picosec startTime);

//         // Returns a flow size according to some distribution.
//         uint64_t generateFlowSize();

//         std::string _prefix;          // Optional prefix for flows.
//         DataSource::EndHost _endhost; // Type of endhost.
//         route_gen_t _routeGen;        // Function to generate a route.
//         linkspeed_bps _flowRate;      // Target flow rate in bytes/sec.
//         uint32_t _flowSizeDist;       // Distribution of flow size [0/1/2] - Uniform/Exp/Pareto.
//         uint32_t _flowsGenerated;     // Total number of flow generated.
//         simtime_picosec _endTime;     // When to stop generating flows and dump live ones.
//         Workloads _workload;          // Type of workload and characteristics.

//         // Endhost queue configuration.
//         bool _endhostQ;
//         linkspeed_bps _endhostQrate;
//         uint64_t _endhostQbuffer;

//         // Flow replacement configuration.
//         bool _useTrace;               // Use a trace for flow generations.
//         bool _replaceFlow;            // Replace flows when finished.
//         uint32_t _maxFlows;           // Max concurrent flows.
//         uint32_t _concurrentFlows;    // Number of concurrent flows.
//         simtime_picosec _avgOffTime;  // Sleep duration as fraction of avgFCT.

//         // Trace of flow arrivals, if using trace generation. (arrival_time, size)
//         std::deque<std::pair<simtime_picosec, uint64_t> > _flowTrace;

//         // List of live flows in the system.
//         std::unordered_map<uint32_t,DataSource*> _liveFlows;

//         // Average flow inter-arrival time, computed using arguments.
//         simtime_picosec _avgFlowArrivalTime;

//         // Custom flow size distribution.
//         std::map<double,uint64_t> _flowSizeCDF;
// };

// #endif /* FLOW_GENERATOR_H */

/*
 * Flow generator header (with FCT aggregation)
 */




// #ifndef FLOW_GENERATOR_H
// #define FLOW_GENERATOR_H

// #include "eventlist.h"
// #include "loggers.h"
// #include "network.h"
// #include "datasource.h"
// #include "tcp.h"
// #include "packetpair.h"
// #include "timely.h"
// #include "workloads.h"
// #include "prof.h"

// #include <deque>
// #include <functional>
// #include <unordered_map>
// #include <map>
// #include <cstdint>
// #include <string>

// /* Route generator function: 4-arg version */
// typedef std::function<void(route_t *&, route_t *&, uint32_t &, uint32_t &)> route_gen_t;

// /* 小流阈值（字节）：默认 100KB，可按需调整 */
// static constexpr uint64_t kSmallThreshB = 100ULL * 1024ULL;

// class FlowGenerator : public EventSource
// {
// public:
//     FlowGenerator(DataSource::EndHost endhost, route_gen_t rg, linkspeed_bps flowRate,
//                   uint32_t avgFlowSize, Workloads::FlowDist flowSizeDist);
//     void doNextEvent();

//     /* Set flow generation start time. */
//     void setTimeLimits(simtime_picosec startTime, simtime_picosec endTime);

//     /* Creates a separate endhost queue for every flow. */
//     void setEndhostQueue(linkspeed_bps qRate, uint64_t qBuffer);

//     /* Fixes max flows in the systems and replaces them when finished. */
//     void setReplaceFlow(uint32_t maxFlows, double offRatio);

//     /* Appends a prefix to flow names to differetiate from other generators. */
//     void setPrefix(std::string prefix);

//     /* Flow arrival using a trace instead of dynamic generation during simulation. */
//     void setTrace(std::string filename);

//     /* Used by Source to notify the Generator of flow finishing, which can then
//      * (optionally) generate a new flow. */
//     void finishFlow(uint32_t flow_id);

//     void dumpLiveFlows();

//     /* ——新增：在仿真末尾输出 FCT 汇总（总时长、平均、小流/大流平均）—— */
//     void dumpFctSummary();

// private:
//     // Creates a flow in the simulation.
//     void createFlow(uint64_t flowSize, simtime_picosec startTime);

//     // Returns a flow size according to some distribution.
//     uint64_t generateFlowSize();

//     /* ——新增：内部记账接口——
//        flow 开始与结束时调用，用于累计 FCT 统计 */
//     void note_flow_start(uint32_t flow_id, uint64_t sizeB, simtime_picosec start_ps);
//     void note_flow_finish(uint32_t flow_id, simtime_picosec end_ps);

// private:
//     std::string _prefix;          // Optional prefix for flows.
//     DataSource::EndHost _endhost; // Type of endhost.
//     route_gen_t _routeGen;        // Function to generate a route.
//     linkspeed_bps _flowRate;      // Target flow rate in bytes/sec.
//     uint32_t _flowSizeDist;       // Distribution of flow size [0/1/2] - Uniform/Exp/Pareto.
//     uint32_t _flowsGenerated;     // Total number of flow generated.
//     simtime_picosec _endTime;     // When to stop generating flows and dump live ones.
//     Workloads _workload;          // Type of workload and characteristics.

//     // Endhost queue configuration.
//     bool _endhostQ;
//     linkspeed_bps _endhostQrate;
//     uint64_t _endhostQbuffer;

//     // Flow replacement configuration.
//     bool _useTrace;               // Use a trace for flow generations.
//     bool _replaceFlow;            // Replace flows when finished.
//     uint32_t _maxFlows;           // Max concurrent flows.
//     uint32_t _concurrentFlows;    // Number of concurrent flows.
//     simtime_picosec _avgOffTime;  // Sleep duration as fraction of avgFCT.

//     // Trace of flow arrivals, if using trace generation. (arrival_time, size)
//     std::deque<std::pair<simtime_picosec, uint64_t> > _flowTrace;

//     // List of live flows in the system.
//     std::unordered_map<uint32_t,DataSource*> _liveFlows;

//     // Average flow inter-arrival time, computed using arguments.
//     simtime_picosec _avgFlowArrivalTime;

//     // Custom flow size distribution.
//     std::map<double,uint64_t> _flowSizeCDF;

//     /* ——新增：活跃 flow 的元信息（用于计算 FCT）—— */
//     struct LiveMeta {
//         uint64_t sizeB;
//         simtime_picosec start_ps;
//     };
//     std::unordered_map<uint32_t, LiveMeta> _liveMeta;

//     /* ——新增：FCT 累计统计（单位：皮秒）—— */
//     struct FctStats {
//         uint64_t     n          = 0;
//         long double  sum_fct_ps = 0;

//         uint64_t     n_small    = 0;
//         long double  sum_small_ps = 0;

//         uint64_t     n_large    = 0;
//         long double  sum_large_ps = 0;
//     };
//     FctStats _fct;
// };

// #endif /* FLOW_GENERATOR_H */


// FlowGenerator.h
#ifndef FLOW_GENERATOR_H
#define FLOW_GENERATOR_H
#include "eventlist.h"
#include "loggers.h"
#include "network.h"
#include "datasource.h"
#include "tcp.h"
#include "packetpair.h"
#include "timely.h"
#include "workloads.h"
#include "prof.h"
#include <deque>
#include <functional>
#include <unordered_map>
#include <map>
#include <limits>

typedef std::function<void(route_t *&, route_t *&, uint32_t &, uint32_t &)> route_gen_t;

class FlowGenerator : public EventSource {
public:
    FlowGenerator(DataSource::EndHost endhost, route_gen_t rg, linkspeed_bps flowRate,
                  uint32_t avgFlowSize, Workloads::FlowDist flowSizeDist);
    void doNextEvent();

    void setTimeLimits(simtime_picosec startTime, simtime_picosec endTime);
    void setEndhostQueue(linkspeed_bps qRate, uint64_t qBuffer);
    void setReplaceFlow(uint32_t maxFlows, double offRatio);
    void setPrefix(std::string prefix);
    void setTrace(std::string filename);

    void finishFlow(uint32_t flow_id);
    void dumpLiveFlows();
    // ✅ 新增：全局收尾与输出控制
    static void FlushAllSummaries();                // main() 调用它
    static void SetSummaryOutput(const std::string& path); // 可选：指定输出文件


private:
    void createFlow(uint64_t flowSize, simtime_picosec startTime);
    uint64_t generateFlowSize();

    // ==== 新增：FCT 统计 ====
    struct FlowMeta {
        uint64_t sizeB = 0;
        simtime_picosec start = 0;
        simtime_picosec end = 0;
        bool finished = false;
    };
    void note_flow_start(uint32_t id, uint64_t sizeB, simtime_picosec start);
    void note_flow_finish(uint32_t id, simtime_picosec end);
    void dumpFctSummary(); // 打印统计
    static constexpr uint64_t SMALL_TH = 100 * 1000; // 100KB

    // per-run 累计量
    uint64_t _completed = 0;
    long double _sum_fct_ms = 0.0L;
    long double _sum_small_ms = 0.0L; uint64_t _cnt_small = 0;
    long double _sum_large_ms = 0.0L; uint64_t _cnt_large = 0;
    std::unordered_map<uint32_t, FlowMeta> _meta;

    // ==== 原有 ====
    std::string _prefix;
    DataSource::EndHost _endhost;
    route_gen_t _routeGen;
    linkspeed_bps _flowRate;
    uint32_t _flowSizeDist;
    uint32_t _flowsGenerated;
    simtime_picosec _endTime;
    Workloads _workload;

    bool _endhostQ;
    linkspeed_bps _endhostQrate;
    uint64_t _endhostQbuffer;

    bool _useTrace;
    bool _replaceFlow;
    uint32_t _maxFlows;
    uint32_t _concurrentFlows;
    simtime_picosec _avgOffTime;

    std::deque<std::pair<simtime_picosec, uint64_t>> _flowTrace;
    std::unordered_map<uint32_t, DataSource*> _liveFlows;

    simtime_picosec _avgFlowArrivalTime;
    std::map<double, uint64_t> _flowSizeCDF;
    /* ====== 新增：全局注册表 & 输出路径 ======
     * 每个构造的 FlowGenerator 自动加入 _registry，
     * FlushAllSummaries() 逐个调 dumpFctSummary()。
     */
    static std::vector<FlowGenerator*> _registry;
    static std::string _summary_out;  // 为空则只 stdout，非空则追加写 CSV

};
#endif
