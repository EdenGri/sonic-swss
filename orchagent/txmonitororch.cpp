#include "txmonitororch.h"
//TODO: try to understand why to take it as global and why not initialize
extern PortsOrch* gPortsOrch;

TxMonitorOrch& TxMonitorOrch::getInstance(TableConnector configueTxTableConnector,
                                          TableConnector stateTxTableConnector,
                                          TableConnector countersTableConnector)
{
    SWSS_LOG_ENTER();
    static TxMonitorOrch* txMonitorOrch = new TxMonitorOrch(configueTxTableConnector,
                                                            stateTxTableConnector,
                                                            countersTableConnector);
    return *txMonitorOrch;
}


// TODO: change the m_pollPeriod and m_threshold from default values to the configurable values
TxMonitorOrch::TxMonitorOrch(TableConnector configueTxTableConnector,
                             TableConnector stateTxTableConnector,
                             TableConnector countersTableConnector):
    Orch(configueTxTableConnector.first, configueTxTableConnector.second),
    m_configueTxTable(configueTxTableConnector.first,
                      configueTxTableConnector.second),
    m_stateTxTable(stateTxTableConnector.first,
                   stateTxTableConnector.second),
    m_countersTable(countersTableConnector.first, countersTableConnector.second),
    m_pollPeriod{DEFAULT_POLLPERIOD},
    m_threshold{DEFAULT_THRESHOLD}
{

    SWSS_LOG_ENTER();
    auto interv = timespec { .tv_sec = m_pollPeriod, .tv_nsec = 0 };
    auto timer = new SelectableTimer(interv);
    auto executor = new ExecutableTimer(timer, this, "MC_TX_ERROR_COUNTERS_POLL");
    Orch::addExecutor(executor);
    SWSS_LOG_NOTICE("TxMonitorOrchLogs: 2 TxMonitorOrch initialized with the tables: %s, %s, %s\n",
                    configueTxTableConnector.second.c_str(),
                    stateTxTableConnector.second.c_str(),
                    countersTableConnector.second.c_str());


    timer->start();
}

std::map<std::string, TxMonitorOrch::TxPortErrorStatistics*> TxMonitorOrch::createPortsErrorStatisticsMap(){

    SWSS_LOG_ENTER();
    std::map<std::string, TxPortErrorStatistics*> interfaceToPortErrorStatistics{};
    map<string, Port> &interfaceToPort =  gPortsOrch->getAllPorts();
    SWSS_LOG_NOTICE("TxMonitorOrchLogs: interfaceToPort size: %lu", interfaceToPort.size());

        for (auto &entry : interfaceToPort)
        {

            string interface = entry.first;
            SWSS_LOG_NOTICE("TxMonitorOrchLogs: interface name: %s", interface.c_str());
            Port p = entry.second;
            if (p.m_type != Port::PHY)
            {
                continue;
            }
            std::string oidStr = std::to_string(p.m_system_port_oid);
            SWSS_LOG_NOTICE("TxMonitorOrchLogs: oid: %s", oidStr.c_str());


            interfaceToPortErrorStatistics[interface]=new TxPortErrorStatistics(oidStr);

        }
    SWSS_LOG_NOTICE("TxMonitorOrchLogs: create txMonitorOrch map");
    SWSS_LOG_NOTICE("TxMonitorOrchLogs: The number of interfaces in txMonitorOrch map: %lu",
                    interfaceToPortErrorStatistics.size());
    m_isPortsMapInitialized=true;
    return interfaceToPortErrorStatistics;

    }

void TxMonitorOrch::printPortsErrorStatistics(){
    for(auto it = m_interfaceToPortErrorStatistics.cbegin(); it != m_interfaceToPortErrorStatistics.cend(); ++it)
    {
        std::string interface = it->first;
        std::string portStatistics = it->second->to_string();
        SWSS_LOG_NOTICE("TxMonitorOrchLogs: %s\t\t%s",interface.c_str(),portStatistics.c_str());

    }
}

TxMonitorOrch::~TxMonitorOrch(void){
    SWSS_LOG_ENTER();
}

void TxMonitorOrch::updateStateDB(std::string currOid,std::string status){
    SWSS_LOG_ENTER();
    vector<FieldValueTuple> fvs;
    fvs.emplace_back("status", status);
    m_stateTxTable.set(currOid, fvs);
    SWSS_LOG_NOTICE("TxMonitorOrchLogs: update stateDB txErrorTable at key oid: %s with the new status  %s", currOid.c_str(), status.c_str());
}



void TxMonitorOrch::doTask(SelectableTimer &timer){
    SWSS_LOG_ENTER();
    if(!gPortsOrch->allPortsReady()){
        return;
    }

    m_interfaceToPortErrorStatistics = createPortsErrorStatisticsMap();
    printPortsErrorStatistics();
    for(const auto& entry : m_interfaceToPortErrorStatistics){

        std::string interface = entry.first;
        SWSS_LOG_NOTICE("TxMonitorOrchLogs: check statistics and update for interface: %s.\n",interface.c_str());

        TxPortErrorStatistics* currTxStatistics = entry.second;
        SWSS_LOG_NOTICE("TxMonitorOrchLogs: last tx counter: %s\n",std::to_string(currTxStatistics->errorCount).c_str());

        std::string currOid = currTxStatistics->m_oid;
        if(currOid=="0"){
            return;
        }
        u_int64_t newErrorCount = getNewErrorCount(currOid);

        bool newIsOk = getIsOkStatus(newErrorCount, currTxStatistics->errorCount);
        if(newIsOk != currTxStatistics->isOk){
            currTxStatistics->isOk = newIsOk;
            updateStateDB(currOid, currTxStatistics->getStatus());

        }
        currTxStatistics->errorCount = newErrorCount;

    }
}

u_int64_t TxMonitorOrch::getNewErrorCount(std::string currOid){
    std::string newErrorCountStr;
    m_countersTable.hget(currOid, "SAI_PORT_STAT_IF_OUT_ERRORS",newErrorCountStr);
    SWSS_LOG_NOTICE("TxMonitorOrchLogs: new tx counter: %s\n",newErrorCountStr.c_str());
    return stoul(newErrorCountStr);
}

bool TxMonitorOrch::getIsOkStatus(u_int64_t newErrorCount, u_int64_t currErrorCount){
    SWSS_LOG_ENTER();
    return (currErrorCount-newErrorCount) < this->m_threshold;
}
//TODO: take care in case configure change in runtime
