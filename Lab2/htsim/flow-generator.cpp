// /*
//  * Flow generator
//  */
// #include "flow-generator.h"

// using namespace std;

// FlowGenerator::FlowGenerator(DataSource::EndHost endhost, 
//                              route_gen_t rg,
//                              linkspeed_bps flowRate, 
//                              uint32_t avgFlowSize, 
//                              Workloads::FlowDist flowSizeDist)
//     : EventSource("FlowGen"),
//     _prefix(""),
//     _endhost(endhost),
//     _routeGen(rg),
//     _flowRate(flowRate),
//     _flowSizeDist(flowSizeDist),
//     _flowsGenerated(0),
//     _workload(avgFlowSize, flowSizeDist),
//     _endhostQ(false),
//     _useTrace(false),
//     _replaceFlow(false),
//     _maxFlows(0),
//     _concurrentFlows(0),
//     _avgOffTime(0),
//     _flowTrace(),
//     _liveFlows()
// {
//     double flowsPerSec = _flowRate / (_workload._avgFlowSize * 8.0);
//     _avgFlowArrivalTime = timeFromSec(1) / flowsPerSec;
// }

// void
// FlowGenerator::setTimeLimits(simtime_picosec startTime, 
//                              simtime_picosec endTime)
// {
//     if (_useTrace) {
//         EventList::Get().sourceIsPending(*this, _flowTrace.front().first);
//     } else {
//         EventList::Get().sourceIsPending(*this, startTime);
//     }
//     EventList::Get().sourceIsPending(*this, endTime);
//     _endTime = endTime;
// }

// void
// FlowGenerator::setEndhostQueue(linkspeed_bps qRate, 
//                                uint64_t qBuffer)
// {
//     _endhostQ = true;
//     _endhostQrate = qRate;
//     _endhostQbuffer = qBuffer;
// }

// void
// FlowGenerator::setReplaceFlow(uint32_t maxFlows, 
//                               double offRatio)
// {
//     _replaceFlow = true;
//     _maxFlows = maxFlows;

//     double avgFCT = (_workload._avgFlowSize * 8.0)/(_flowRate/_maxFlows);
//     _avgFlowArrivalTime = timeFromSec(avgFCT)/_maxFlows;

//     _avgOffTime = llround(timeFromSec(avgFCT) * offRatio / (1 + offRatio));
// }

// void
// FlowGenerator::setPrefix(string prefix)
// {
//     _prefix = prefix;
// }

// void
// FlowGenerator::setTrace(string filename)
// {
//     _useTrace = true;

//     FILE *fp = fopen(filename.c_str(), "r");
//     if (fp == NULL) {
//         fprintf(stderr, "Error opening trace file: %s\n", filename.c_str());
//         exit(1);
//     }

//     uint32_t fid;
//     uint32_t fsize;
//     uint64_t fstart;

//     /* Assumes the file is sorted by flow arrival. */
//     while (fscanf(fp, "flow-%u %lu %*lu %u %*lu %*lu ", &fid, &fstart, &fsize) != EOF) {
//         _flowTrace.push_back(make_pair(timeFromUs(fstart), fsize));
//     }

//     fclose(fp);
// }

// void
// FlowGenerator::doNextEvent()
// {
//     if (EventList::Get().now() == _endTime) {
//         dumpLiveFlows();
//         return;
//     }

//     // Get flowsize from given distriubtion or trace.
//     uint64_t flowSize;

//     if (_useTrace) {
//         flowSize = _flowTrace.front().second;
//         _flowTrace.pop_front();
//     } else {
//         flowSize = _workload.generateFlowSize();
//     }

//     // Create the flow.
//     createFlow(flowSize, 0);
//     _concurrentFlows++;

//     // Get next flow arrival from given distriubtion or trace.
//     simtime_picosec nextFlowArrival;

//     if (_useTrace) {
//         if (_flowTrace.size() > 0) {
//             nextFlowArrival = _flowTrace.front().first - EventList::Get().now();
//         } else {
//             return;
//         }
//     } else {
//         nextFlowArrival = exponential(1.0/_avgFlowArrivalTime);
//     }

//     // Schedule next flow.
//     if (_replaceFlow == false || _concurrentFlows < _maxFlows) {
//         EventList::Get().sourceIsPendingRel(*this, nextFlowArrival);
//     }
// }

// void
// FlowGenerator::createFlow(uint64_t flowSize, 
//                           simtime_picosec startTime)
// {
//     // Generate a random route.
//     route_t *routeFwd = NULL, *routeRev = NULL;
//     uint32_t src_node = 0, dst_node = 0;
    

//     // Generate next start time adding jitter.
//     simtime_picosec start_time = EventList::Get().now() + startTime + llround(drand() * timeFromUs(5));
//     simtime_picosec deadline = timeFromSec((flowSize * 8.0) / speedFromGbps(0.8));

//     //_routeGen(routeFwd, routeRev, src_node, dst_node, flowSize, startTime);
//     _routeGen(routeFwd, routeRev, src_node, dst_node);

//     // If flag set, append an endhost queue.
//     if (_endhostQ) {
//         Queue *endhostQ = new Queue(_endhostQrate, _endhostQbuffer, NULL);
//         routeFwd->insert(routeFwd->begin(), endhostQ);
//     }

//     DataSource *src;
//     DataSink *snk;

//     switch (_endhost) {
//         case DataSource::PKTPAIR:
//             src = new PacketPairSrc(NULL, flowSize);
//             snk = new PacketPairSink();
//             break;

//         case DataSource::TIMELY:
//             src = new TimelySrc(NULL, flowSize);
//             snk = new TimelySink();
//             break;

//         default: { // TCP variant
//                      // TODO: option to supply logtcp.
//                      src = new TcpSrc(NULL, NULL, flowSize);
//                      snk = new TcpSink();

//                      if (_endhost == DataSource::DCTCP || _endhost == DataSource::D_DCTCP) {
//                          TcpSrc::_enable_dctcp = true;
//                      }

//                      if (_endhost == DataSource::D_TCP || _endhost == DataSource::D_DCTCP) {
//                          src->_enable_deadline = true;
//                      }
//                  }
//     }

//     src->setName(_prefix + "src" + to_string(_flowsGenerated));
//     snk->setName(_prefix + "snk" + to_string(_flowsGenerated));
//     src->_node_id = src_node;
//     snk->_node_id = dst_node;

//     src->setDeadline(start_time + deadline);

//     routeFwd->push_back(snk);
//     routeRev->push_back(src);
//     src->connect(start_time, *routeFwd, *routeRev, *snk);
//     src->setFlowGenerator(this);

//     _liveFlows[src->id] = src;

//     _flowsGenerated++;
// }

// void
// FlowGenerator::finishFlow(uint32_t flow_id)
// {
//     if (_liveFlows.erase(flow_id) == 0) {
//         return;
//     }

//     if (_replaceFlow) {
//         uint64_t flowSize = _workload.generateFlowSize();
//         uint64_t sleepTime = 0;

//         if (_avgOffTime > 0) {
//             sleepTime = llround(exponential(1.0L / _avgOffTime));
//         }

//         createFlow(flowSize, sleepTime);
//     }
// }

// void
// FlowGenerator::dumpLiveFlows()
// {
//     cout << endl << "Live Flows: " << _liveFlows.size() << endl;
//     for (auto flow : _liveFlows) {
//         DataSource *src = flow.second;
//         src->printStatus();
//     }

//     // TODO: temp hack, remove later.
//     /*
//        uint64_t cumul = 0;
//        cout << "Initial slacks" << endl;
//        for (auto it = TcpSrc::slacks.begin(); it != TcpSrc::slacks.end(); it++) {
//        cumul += it->second;
//        cout << it->first << " " << it->second << " " << (cumul * 100.0) / TcpSrc::totalPkts << endl;
//        }

//        cumul = 0;
//        cout << "Final slacks" << endl;
//        for (auto it = TcpSink::slacks.begin(); it != TcpSink::slacks.end(); it++) {
//        cumul += it->second;
//        cout << it->first << " " << it->second << " " << (cumul * 100.0) / TcpSink::totalPkts << endl;
//        }
//        */
// }

/*
 * Flow generator
 */
// #include "flow-generator.h"
// #include <iostream>
// #include <iomanip>
// #include <limits>

// using namespace std;

// FlowGenerator::FlowGenerator(DataSource::EndHost endhost, 
//                              route_gen_t rg,
//                              linkspeed_bps flowRate, 
//                              uint32_t avgFlowSize, 
//                              Workloads::FlowDist flowSizeDist)
//     : EventSource("FlowGen"),
//       _prefix(""),
//       _endhost(endhost),
//       _routeGen(rg),
//       _flowRate(flowRate),
//       _flowSizeDist(flowSizeDist),
//       _flowsGenerated(0),
//       _workload(avgFlowSize, flowSizeDist),
//       _endhostQ(false),
//       _useTrace(false),
//       _replaceFlow(false),
//       _maxFlows(0),
//       _concurrentFlows(0),
//       _avgOffTime(0),
//       _flowTrace(),
//       _liveFlows(),
//       _liveMeta(),
//       _fct()
// {
//     double flowsPerSec = _flowRate / (_workload._avgFlowSize * 8.0);
//     _avgFlowArrivalTime = timeFromSec(1) / flowsPerSec;
// }

// void
// FlowGenerator::setTimeLimits(simtime_picosec startTime, 
//                              simtime_picosec endTime)
// {
//     if (_useTrace) {
//         EventList::Get().sourceIsPending(*this, _flowTrace.front().first);
//     } else {
//         EventList::Get().sourceIsPending(*this, startTime);
//     }
//     EventList::Get().sourceIsPending(*this, endTime);
//     _endTime = endTime;
// }

// void
// FlowGenerator::setEndhostQueue(linkspeed_bps qRate, 
//                                uint64_t qBuffer)
// {
//     _endhostQ = true;
//     _endhostQrate = qRate;
//     _endhostQbuffer = qBuffer;
// }

// void
// FlowGenerator::setReplaceFlow(uint32_t maxFlows, 
//                               double offRatio)
// {
//     _replaceFlow = true;
//     _maxFlows = maxFlows;

//     double avgFCT = (_workload._avgFlowSize * 8.0)/(_flowRate/_maxFlows);
//     _avgFlowArrivalTime = timeFromSec(avgFCT)/_maxFlows;

//     _avgOffTime = llround(timeFromSec(avgFCT) * offRatio / (1 + offRatio));
// }

// void
// FlowGenerator::setPrefix(string prefix)
// {
//     _prefix = prefix;
// }

// void
// FlowGenerator::setTrace(string filename)
// {
//     _useTrace = true;

//     FILE *fp = fopen(filename.c_str(), "r");
//     if (fp == NULL) {
//         fprintf(stderr, "Error opening trace file: %s\n", filename.c_str());
//         exit(1);
//     }

//     uint32_t fid;
//     uint32_t fsize;
//     uint64_t fstart;

//     /* Assumes the file is sorted by flow arrival. */
//     while (fscanf(fp, "flow-%u %lu %*lu %u %*lu %*lu ", &fid, &fstart, &fsize) != EOF) {
//         _flowTrace.push_back(make_pair(timeFromUs(fstart), fsize));
//     }

//     fclose(fp);
// }

// void
// FlowGenerator::doNextEvent()
// {
//     if (EventList::Get().now() == _endTime) {
//         dumpFctSummary();   // ——新增：先输出 FCT 汇总
//         dumpLiveFlows();    // 原有：剩余在途 flow 的状态
//         return;
//     }

//     // Get flowsize from given distribution or trace.
//     uint64_t flowSize;

//     if (_useTrace) {
//         flowSize = _flowTrace.front().second;
//         _flowTrace.pop_front();
//     } else {
//         flowSize = _workload.generateFlowSize();
//     }

//     // Create the flow.
//     createFlow(flowSize, 0);
//     _concurrentFlows++;

//     // Get next flow arrival from given distribution or trace.
//     simtime_picosec nextFlowArrival;

//     if (_useTrace) {
//         if (_flowTrace.size() > 0) {
//             nextFlowArrival = _flowTrace.front().first - EventList::Get().now();
//         } else {
//             return;
//         }
//     } else {
//         nextFlowArrival = exponential(1.0/_avgFlowArrivalTime);
//     }

//     // Schedule next flow.
//     if (_replaceFlow == false || _concurrentFlows < _maxFlows) {
//         EventList::Get().sourceIsPendingRel(*this, nextFlowArrival);
//     }
// }

// void
// FlowGenerator::createFlow(uint64_t flowSize, 
//                           simtime_picosec startTime)
// {
//     // Generate a random route.
//     route_t *routeFwd = NULL, *routeRev = NULL;
//     uint32_t src_node = 0, dst_node = 0;

//     // Generate next start time adding jitter.
//     simtime_picosec start_time = EventList::Get().now() + startTime + llround(drand() * timeFromUs(5));
//     simtime_picosec deadline = timeFromSec((flowSize * 8.0) / speedFromGbps(0.8));

//     _routeGen(routeFwd, routeRev, src_node, dst_node);

//     // If flag set, append an endhost queue.
//     if (_endhostQ) {
//         Queue *endhostQ = new Queue(_endhostQrate, _endhostQbuffer, NULL);
//         routeFwd->insert(routeFwd->begin(), endhostQ);
//     }

//     DataSource *src;
//     DataSink *snk;

//     switch (_endhost) {
//         case DataSource::PKTPAIR:
//             src = new PacketPairSrc(NULL, flowSize);
//             snk = new PacketPairSink();
//             break;

//         case DataSource::TIMELY:
//             src = new TimelySrc(NULL, flowSize);
//             snk = new TimelySink();
//             break;

//         default: { // TCP variant
//             // TODO: option to supply logtcp.
//             src = new TcpSrc(NULL, NULL, flowSize);
//             snk = new TcpSink();

//             if (_endhost == DataSource::DCTCP || _endhost == DataSource::D_DCTCP) {
//                 TcpSrc::_enable_dctcp = true;
//             }

//             if (_endhost == DataSource::D_TCP || _endhost == DataSource::D_DCTCP) {
//                 src->_enable_deadline = true;
//             }
//         }
//     }

//     src->setName(_prefix + "src" + to_string(_flowsGenerated));
//     snk->setName(_prefix + "snk" + to_string(_flowsGenerated));
//     src->_node_id = src_node;
//     snk->_node_id = dst_node;

//     src->setDeadline(start_time + deadline);

//     // ——新增：在连接前记账 flow 起始（使用 src->id 作为 flow_id）
//     note_flow_start(src->id, flowSize, start_time);

//     routeFwd->push_back(snk);
//     routeRev->push_back(src);
//     src->connect(start_time, *routeFwd, *routeRev, *snk);
//     src->setFlowGenerator(this);

//     _liveFlows[src->id] = src;

//     _flowsGenerated++;
// }

// void
// FlowGenerator::finishFlow(uint32_t flow_id)
// {
//     // ——新增：记录完成时间（now）
//     note_flow_finish(flow_id, EventList::Get().now());

//     if (_liveFlows.erase(flow_id) == 0) {
//         return;
//     }

//     if (_replaceFlow) {
//         uint64_t flowSize = _workload.generateFlowSize();
//         uint64_t sleepTime = 0;

//         if (_avgOffTime > 0) {
//             sleepTime = llround(exponential(1.0L / _avgOffTime));
//         }

//         createFlow(flowSize, sleepTime);
//     }
// }

// void
// FlowGenerator::dumpLiveFlows()
// {
//     cout << endl << "Live Flows: " << _liveFlows.size() << endl;
//     for (auto flow : _liveFlows) {
//         DataSource *src = flow.second;
//         src->printStatus();
//     }

//     // TODO: temp hack, remove later.
//     /*
//        uint64_t cumul = 0;
//        cout << "Initial slacks" << endl;
//        for (auto it = TcpSrc::slacks.begin(); it != TcpSrc::slacks.end(); it++) {
//        cumul += it->second;
//        cout << it->first << " " << it->second << " " << (cumul * 100.0) / TcpSrc::totalPkts << endl;
//        }

//        cumul = 0;
//        cout << "Final slacks" << endl;
//        for (auto it = TcpSink::slacks.begin(); it != TcpSink::slacks.end(); it++) {
//        cumul += it->second;
//        cout << it->first << " " << it->second << " " << (cumul * 100.0) / TcpSrc::totalPkts << endl;
//        }
//        */
// }

// /* ================= 新增：FCT 统计实现 ================= */

// void
// FlowGenerator::note_flow_start(uint32_t flow_id, uint64_t sizeB, simtime_picosec start_ps)
// {
//     _liveMeta[flow_id] = LiveMeta{sizeB, start_ps};
// }

// void
// FlowGenerator::note_flow_finish(uint32_t flow_id, simtime_picosec end_ps)
// {
//     auto it = _liveMeta.find(flow_id);
//     if (it == _liveMeta.end()) {
//         // 未记录起点（可能是替换流或其他异常），忽略
//         return;
//     }
//     const LiveMeta &m = it->second;
//     if (end_ps >= m.start_ps) {
//         simtime_picosec fct_ps = end_ps - m.start_ps;

//         _fct.n++;
//         _fct.sum_fct_ps += static_cast<long double>(fct_ps);

//         if (m.sizeB < kSmallThreshB) {
//             _fct.n_small++;
//             _fct.sum_small_ps += static_cast<long double>(fct_ps);
//         } else {
//             _fct.n_large++;
//             _fct.sum_large_ps += static_cast<long double>(fct_ps);
//         }
//     }
//     _liveMeta.erase(it);
// }

// void
// FlowGenerator::dumpFctSummary()
// {
//     // 仅在有完成流时输出
//     if (_fct.n == 0) {
//         cout << "\n[FCT Summary] No completed flows.\n";
//         return;
//     }

//     auto ps_to_ms = [](long double ps) -> long double {
//         // 1 ps = 1e-12 s = 1e-9 ms
//         return ps * 1e-9L; 
//     };

//     long double total_ms = ps_to_ms(_fct.sum_fct_ps);
//     long double avg_ms   = ps_to_ms(_fct.sum_fct_ps / static_cast<long double>(_fct.n));

//     cout << fixed << setprecision(3);
//     cout << "\n================ FCT Summary ================\n";
//     cout << "Completed flows         : " << _fct.n << "\n";
//     cout << "Total FCT (ms)          : " << total_ms << "\n";
//     cout << "Average FCT (ms)        : " << avg_ms << "\n";

//     if (_fct.n_small > 0) {
//         long double avg_small_ms = ps_to_ms(_fct.sum_small_ps / static_cast<long double>(_fct.n_small));
//         cout << "Small  (< " << (kSmallThreshB/1024) << " KB) count: " << _fct.n_small
//              << ", avg FCT (ms): " << avg_small_ms << "\n";
//     } else {
//         cout << "Small  (< " << (kSmallThreshB/1024) << " KB) count: 0\n";
//     }

//     if (_fct.n_large > 0) {
//         long double avg_large_ms = ps_to_ms(_fct.sum_large_ps / static_cast<long double>(_fct.n_large));
//         cout << "Large (>= " << (kSmallThreshB/1024) << " KB) count: " << _fct.n_large
//              << ", avg FCT (ms): " << avg_large_ms << "\n";
//     } else {
//         cout << "Large (>= " << (kSmallThreshB/1024) << " KB) count: 0\n";
//     }
//     cout << "=============================================\n";
// }




#include "flow-generator.h"
#include <cstdio>
#include <cstdlib>
#include <utility>
#include <fstream>
#include <iostream>

using namespace std;

static inline double ps_to_ms(simtime_picosec ps){
    return timeAsMs(ps);
}

/* ===== 静态成员定义 ===== */
std::vector<FlowGenerator*> FlowGenerator::_registry;
std::string FlowGenerator::_summary_out;

/* ===== 构造 & 基本配置 ===== */
FlowGenerator::FlowGenerator(DataSource::EndHost endhost,
                             route_gen_t rg,
                             linkspeed_bps flowRate,
                             uint32_t avgFlowSize,
                             Workloads::FlowDist flowSizeDist)
    : EventSource("FlowGen"),
      _prefix(""),
      _endhost(endhost),
      _routeGen(rg),
      _flowRate(flowRate),
      _flowSizeDist(flowSizeDist),
      _flowsGenerated(0),
      _workload(avgFlowSize, flowSizeDist),
      _endhostQ(false),
      _useTrace(false),
      _replaceFlow(false),
      _maxFlows(0),
      _concurrentFlows(0),
      _avgOffTime(0),
      _flowTrace(),
      _liveFlows()
{
    double flowsPerSec = _flowRate / (_workload._avgFlowSize * 8.0);
    _avgFlowArrivalTime = timeFromSec(1) / flowsPerSec;

    // 注册到全局表，供 FlushAllSummaries() 汇总
    _registry.push_back(this);
}

void FlowGenerator::setTimeLimits(simtime_picosec startTime, simtime_picosec endTime){
    if (_useTrace) {
        EventList::Get().sourceIsPending(*this, _flowTrace.front().first);
    } else {
        EventList::Get().sourceIsPending(*this, startTime);
    }
    EventList::Get().sourceIsPending(*this, endTime);
    _endTime = endTime;
}

void FlowGenerator::setEndhostQueue(linkspeed_bps qRate, uint64_t qBuffer){
    _endhostQ = true; _endhostQrate = qRate; _endhostQbuffer = qBuffer;
}

void FlowGenerator::setReplaceFlow(uint32_t maxFlows, double offRatio){
    _replaceFlow = true; _maxFlows = maxFlows;
    double avgFCT = (_workload._avgFlowSize * 8.0)/(_flowRate/_maxFlows);
    _avgFlowArrivalTime = timeFromSec(avgFCT)/_maxFlows;
    _avgOffTime = llround(timeFromSec(avgFCT) * offRatio / (1 + offRatio));
}

void FlowGenerator::setPrefix(string prefix){ _prefix = std::move(prefix); }

void FlowGenerator::setTrace(string filename){
    _useTrace = true;
    FILE *fp = fopen(filename.c_str(), "r");
    if (!fp) { fprintf(stderr, "Error opening trace file: %s\n", filename.c_str()); exit(1); }
    uint32_t fid; uint32_t fsize; uint64_t fstart;
    while (fscanf(fp, "flow-%u %lu %*lu %u %*lu %*lu ", &fid, &fstart, &fsize) != EOF) {
        _flowTrace.push_back(make_pair(timeFromUs(fstart), fsize));
    }
    fclose(fp);
}

/* ===== 事件循环：生成流 / 到达终止时刻打印本生成器汇总 ===== */
void FlowGenerator::doNextEvent(){
    if (EventList::Get().now() == _endTime) {
        dumpFctSummary();   // ✅ 结束时刻强制打印当前 FlowGenerator 的统计
        dumpLiveFlows();
        return;
    }

    uint64_t flowSize;
    if (_useTrace) { flowSize = _flowTrace.front().second; _flowTrace.pop_front(); }
    else           { flowSize = _workload.generateFlowSize(); }

    createFlow(flowSize, 0);
    _concurrentFlows++;

    simtime_picosec nextFlowArrival;
    if (_useTrace) {
        if (_flowTrace.size() > 0) nextFlowArrival = _flowTrace.front().first - EventList::Get().now();
        else return;
    } else {
        nextFlowArrival = exponential(1.0/_avgFlowArrivalTime);
    }

    if (!_replaceFlow || _concurrentFlows < _maxFlows) {
        EventList::Get().sourceIsPendingRel(*this, nextFlowArrival);
    }
}

/* ===== 生成一条流并记起始 ===== */
void FlowGenerator::createFlow(uint64_t flowSize, simtime_picosec startTime){
    route_t *routeFwd = nullptr, *routeRev = nullptr;
    uint32_t src_node = 0, dst_node = 0;

    simtime_picosec start_time = EventList::Get().now() + startTime + llround(drand() * timeFromUs(5));
    simtime_picosec deadline = timeFromSec((flowSize * 8.0) / speedFromGbps(0.8));

    _routeGen(routeFwd, routeRev, src_node, dst_node);

    if (_endhostQ) {
        Queue *endhostQ = new Queue(_endhostQrate, _endhostQbuffer, NULL);
        routeFwd->insert(routeFwd->begin(), endhostQ);
    }

    DataSource *src; DataSink *snk;
    switch (_endhost) {
        case DataSource::PKTPAIR: src = new PacketPairSrc(NULL, flowSize); snk = new PacketPairSink(); break;
        case DataSource::TIMELY:  src = new TimelySrc(NULL, flowSize);     snk = new TimelySink();     break;
        default: {
            src = new TcpSrc(NULL, NULL, flowSize);
            snk = new TcpSink();
            if (_endhost == DataSource::DCTCP || _endhost == DataSource::D_DCTCP) TcpSrc::_enable_dctcp = true;
            if (_endhost == DataSource::D_TCP  || _endhost == DataSource::D_DCTCP) src->_enable_deadline = true;
        }
    }

    src->setName(_prefix + "src" + to_string(_flowsGenerated));
    snk->setName(_prefix + "snk" + to_string(_flowsGenerated));
    src->_node_id = src_node; snk->_node_id = dst_node;
    src->setDeadline(start_time + deadline);

    routeFwd->push_back(snk);
    routeRev->push_back(src);

    // 连接 & 回调
    src->connect(start_time, *routeFwd, *routeRev, *snk);
    src->setFlowGenerator(this);

    // ✅ 记录起始
    //note_flow_start(src->id, flowSize, start_time);
    note_flow_start(src->id, flowSize, EventList::Get().now());

    _liveFlows[src->id] = src;
    _flowsGenerated++;
}

/* ===== DataSource 完成回调：记结束、可能立即打印、以及替换流 ===== */
void FlowGenerator::finishFlow(uint32_t flow_id){
    // ✅ 记录完成时刻
    note_flow_finish(flow_id, EventList::Get().now());

    if (_liveFlows.erase(flow_id) == 0) return;

    // ✅ 若没有“替换流”并且已经没有存活流，立刻打印统计（即使没到 endTime 也会看到）
    if (!_replaceFlow && _liveFlows.empty()) {
        dumpFctSummary();
        std::cout.flush();
    }

    if (_replaceFlow) {
        uint64_t flowSize = _workload.generateFlowSize();
        uint64_t sleepTime = 0;
        if (_avgOffTime > 0) sleepTime = llround(exponential(1.0L / _avgOffTime));
        createFlow(flowSize, sleepTime);
    }
}

/* ===== 调试：打印仍存活的流 ===== */
void FlowGenerator::dumpLiveFlows(){
    cout << endl << "Live Flows: " << _liveFlows.size() << endl;
    for (auto &kv : _liveFlows) {
        DataSource *src = kv.second;
        src->printStatus();
    }
}

/* ===== FCT 统计：开始/结束/打印 ===== */
void FlowGenerator::note_flow_start(uint32_t id, uint64_t sizeB, simtime_picosec start){
    auto &m = _meta[id];
    m.sizeB = sizeB;
    m.start = start;
    m.finished = false;

// ✅ 添加调试
    static int debug_count = 0;
    if (debug_count < 5) {
        cout << "\n=== DEBUG note_flow_start ===" << endl;
        cout << "Flow ID: " << id << endl;
        cout << "Size: " << sizeB << " bytes" << endl;
        cout << "Start (ps): " << start << endl;
        cout << "Start (us): " << timeAsUs(start) << endl;
        cout << "Start (ms): " << timeAsMs(start) << endl;
        cout << "============================\n" << endl;
        debug_count++;
    }

}

void FlowGenerator::note_flow_finish(uint32_t id, simtime_picosec end){
    auto it = _meta.find(id);
    if (it == _meta.end()) return;
    FlowMeta &m = it->second;
    if (m.finished) return;
    m.end = end; m.finished = true;
    simtime_picosec dur = (m.end > m.start) ? (m.end - m.start) : 0;
    double fct_ms = ps_to_ms(dur);

// ✅ 添加调试
    static int debug_count = 0;
    if (debug_count < 5) {
        cout << "\n=== DEBUG note_flow_finish ===" << endl;
        cout << "Flow ID: " << id << endl;
        cout << "Size: " << m.sizeB << " bytes" << endl;
        cout << "Start (ps): " << m.start << endl;
        cout << "Start (us): " << timeAsUs(m.start) << endl;
        cout << "End (ps): " << end << endl;
        cout << "End (us): " << timeAsUs(end) << endl;
        cout << "Duration (ps): " << dur << endl;
        cout << "FCT (ms): " << fct_ms << endl;
        cout << "Expected from logs: ~100+ ms" << endl;
        cout << "============================\n" << endl;
        debug_count++;
    }

    _sum_fct_ms += fct_ms;
    _completed++;
    if (m.sizeB < SMALL_TH) { _sum_small_ms += fct_ms; _cnt_small++; }
    else                    { _sum_large_ms += fct_ms; _cnt_large++; }
}

void FlowGenerator::dumpFctSummary(){
    cout << "\n================ FCT Summary ================\n";
    cout << "Completed flows         : " << _completed << "\n";
    cout << "Total FCT (ms)          : " << (double)_sum_fct_ms << "\n";
    if (_completed > 0)
        cout << "Average FCT (ms)        : " << (double)(_sum_fct_ms / _completed) << "\n";
    cout << "Small  (< 100 KB) count : " << _cnt_small
         << ",  avg FCT (ms): " << (_cnt_small ? (double)(_sum_small_ms/_cnt_small) : 0.0) << "\n";
    cout << "Large (>= 100 KB) count : " << _cnt_large
         << ",  avg FCT (ms): " << (_cnt_large ? (double)(_sum_large_ms/_cnt_large) : 0.0) << "\n";
    cout << "=============================================\n";
    cout.flush();
}

/* ===== 全局：设置 CSV 输出路径 ===== */
void FlowGenerator::SetSummaryOutput(const std::string& path){
    _summary_out = path;
    if (_summary_out.empty()) return;

    // 若文件不存在或为空，写表头
    bool need_header = false;
    {
        std::ifstream fin(_summary_out, std::ios::in | std::ios::binary);
        if (!fin.good() || fin.peek() == std::ifstream::traits_type::eof())
            need_header = true;
    }
    if (need_header) {
        std::ofstream fout(_summary_out, std::ios::out | std::ios::app);
        if (fout.good()) {
            fout << "generator_idx,completed,total_ms,avg_ms,small_cnt,small_avg_ms,large_cnt,large_avg_ms\n";
        }
    }
}

/* ===== 全局：收尾统一输出（在 main 里事件循环后调用） ===== */
void FlowGenerator::FlushAllSummaries(){
    // 汇总所有生成器
    long double tot_ms=0.0L, sm_ms=0.0L, lg_ms=0.0L;
    uint64_t tot_cnt=0, sm_cnt=0, lg_cnt=0;

    // 追加写 CSV（若设置了路径）
    std::ofstream fout;
    if (!_summary_out.empty()) {
        fout.open(_summary_out, std::ios::out | std::ios::app);
    }

    for (size_t i=0;i<_registry.size();++i){
        FlowGenerator* g = _registry[i];
        tot_ms += g->_sum_fct_ms;  tot_cnt += g->_completed;
        sm_ms  += g->_sum_small_ms; sm_cnt += g->_cnt_small;
        lg_ms  += g->_sum_large_ms; lg_cnt += g->_cnt_large;

        if (fout.good()) {
            double avg_all = (g->_completed ? (double)(g->_sum_fct_ms / g->_completed) : 0.0);
            double avg_sm  = (g->_cnt_small ? (double)(g->_sum_small_ms / g->_cnt_small) : 0.0);
            double avg_lg  = (g->_cnt_large ? (double)(g->_sum_large_ms / g->_cnt_large) : 0.0);
            fout << i << ","
                 << g->_completed << ","
                 << (double)g->_sum_fct_ms << ","
                 << avg_all << ","
                 << g->_cnt_small << ","
                 << avg_sm << ","
                 << g->_cnt_large << ","
                 << avg_lg << "\n";
        }
    }
    if (fout.good()) fout.close();

    // 打印全局总览
    std::cout << "\n========= GLOBAL FCT SUMMARY (all FlowGenerators) =========\n";
    std::cout << "Total completed flows   : " << tot_cnt << "\n";
    std::cout << "Total FCT (ms)          : " << (double)tot_ms << "\n";
    if (tot_cnt > 0)
        std::cout << "Average FCT (ms)        : " << (double)(tot_ms / tot_cnt) << "\n";
    std::cout << "Small  (<100KB) count   : " << sm_cnt
              << ", avg FCT (ms): " << (sm_cnt ? (double)(sm_ms / sm_cnt) : 0.0) << "\n";
    std::cout << "Large (>=100KB) count   : " << lg_cnt
              << ", avg FCT (ms): " << (lg_cnt ? (double)(lg_ms / lg_cnt) : 0.0) << "\n";
    std::cout << "===========================================================\n";
    std::cout.flush();
}

