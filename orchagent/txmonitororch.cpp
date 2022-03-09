#include "txmonitororch.h"
extern PortsOrch* gPortsOrch;

TxMonitorOrch& TxMonitorOrch::getInstance(TableConnector configueTxTableConnector,
                                          TableConnector stateTxTableConnector,
                                          TableConnector countersTableConnector,
                                          TableConnector interfaceToOidTableConnector)
{
    SWSS_LOG_ENTER();
    static TxMonitorOrch* txMonitorOrch = new TxMonitorOrch(configueTxTableConnector,
                                                            stateTxTableConnector,
                                                            countersTableConnector,
                                                            interfaceToOidTableConnector);
    return *txMonitorOrch;
}


// TODO: change the m_pollPeriod and m_threshold from default values to the configurable values
TxMonitorOrch::TxMonitorOrch(TableConnector configueTxTableConnector,
                             TableConnector stateTxTableConnector,
                             TableConnector countersTableConnector,
                             TableConnector interfaceToOidTableConnector):
    Orch(configueTxTableConnector.first, configueTxTableConnector.second),
    m_configueTxTable(configueTxTableConnector.first,
                      configueTxTableConnector.second),
    m_stateTxTable(stateTxTableConnector.first,
                   stateTxTableConnector.second),
    m_countersTable(countersTableConnector.first, countersTableConnector.second),
    m_interfaceToOidTable(interfaceToOidTableConnector.first, interfaceToOidTableConnector.second),
    m_pollPeriod{DEFAULT_POLLPERIOD},
    m_threshold{DEFAULT_THRESHOLD}
{

    SWSS_LOG_ENTER();
    initConfigTxTable();
    auto interv = timespec { .tv_sec = m_pollPeriod, .tv_nsec = 0 };
    auto timer = new SelectableTimer(interv);
    auto executor = new ExecutableTimer(timer, this, "MC_TX_ERROR_COUNTERS_POLL");
    Orch::addExecutor(executor);
    SWSS_LOG_NOTICE("TxMonitorOrch initialized with the tables: %s, %s, %s, %s\n",
                    configueTxTableConnector.second.c_str(),
                    stateTxTableConnector.second.c_str(),
                    countersTableConnector.second.c_str(),
                    interfaceToOidTableConnector.second.c_str());


    timer->start();
}

void TxMonitorOrch::initConfigTxTable(){
    vector<FieldValueTuple> periodFvs;
    vector<FieldValueTuple> thresholdFvs;
    periodFvs.emplace_back("value", std::to_string(m_pollPeriod));
    thresholdFvs.emplace_back("value",std::to_string(m_threshold));
    m_configueTxTable.set("polling_period",periodFvs);
    m_configueTxTable.set("threshold",thresholdFvs);

}

IFtoTxPortErrStat TxMonitorOrch::createPortsErrorStatisticsMap(){

    SWSS_LOG_ENTER();
    IFtoTxPortErrStat interfaceToTxPortErrStat{};
    map<string, Port> &interfaceToPort =  gPortsOrch->getAllPorts();

        for (auto &entry : interfaceToPort)
        {
            string interface = entry.first;
            Port p = entry.second;

            SWSS_LOG_NOTICE("interface name: %s", interface.c_str());
            if (p.m_type != Port::PHY)
            {
                continue;
            }
            std::string oidStr;
            if(m_interfaceToOidTable.hget("",interface,oidStr)){
                SWSS_LOG_NOTICE("oid: %s", oidStr.c_str());
                interfaceToTxPortErrStat[interface]=new TxPortErrStat(oidStr);
                updateStateDB(oidStr.c_str(),"ok");
            }else{
                SWSS_LOG_NOTICE("cant get oid for interface %s", interface.c_str());
                interfaceToTxPortErrStat[interface]=new TxPortErrStat();
            }

        }
    SWSS_LOG_NOTICE("create txMonitorOrch map");
    SWSS_LOG_NOTICE("The number of interfaces in txMonitorOrch map: %lu",
                    interfaceToTxPortErrStat.size());
    return interfaceToTxPortErrStat;

    }

//todo: complete
TxMonitorOrch::~TxMonitorOrch(void){
    SWSS_LOG_ENTER();
}

void TxMonitorOrch::updateStateDB(std::string currOid,std::string status){
    SWSS_LOG_ENTER();
    vector<FieldValueTuple> fvs;
    fvs.emplace_back("status", status);
    m_stateTxTable.set(currOid, fvs);
    SWSS_LOG_NOTICE("update stateDB txErrorTable at key oid: %s with the new status  %s", currOid.c_str(), status.c_str());
}

std::string TxMonitorOrch::getOid(std::string interface){

    std::string oidStr;
    if(m_interfaceToOidTable.hget("",interface,oidStr)){
        SWSS_LOG_NOTICE("oid: %s", oidStr.c_str());
        return oidStr;
    }else{
        SWSS_LOG_NOTICE("cant get oid for interfae %s", interface.c_str());
        return "null";
    }
}


u_int64_t TxMonitorOrch::getNewErrorCount(std::string currOid){
    std::string newErrorCountStr;
    if(m_countersTable.hget(currOid, "SAI_PORT_STAT_IF_OUT_ERRORS",newErrorCountStr)){
        SWSS_LOG_NOTICE("new tx counter: %s\n",newErrorCountStr.c_str());
        return stoul(newErrorCountStr);
    }
    return 0;
}



bool TxMonitorOrch::getIsOkStatus(u_int64_t newErrorCount, u_int64_t currErrorCount){
    SWSS_LOG_ENTER();
    return (currErrorCount-newErrorCount) < this->m_threshold;
}

void TxMonitorOrch::poolTxErrorStatistics(std::string interface,TxPortErrStat* currTxStatistics){
    SWSS_LOG_NOTICE("check statistics and update for interface: %s.\n",interface.c_str());
    std::string currOid = currTxStatistics->m_oid;
    if(currOid=="null"){
        currTxStatistics->m_oid =getOid(interface);
        if(currTxStatistics->m_oid == "null"){
            return;
        }
        updateStateDB(currTxStatistics->m_oid,"ok");
    }

    u_int64_t currErrorCount = currTxStatistics->m_errorCount;
    SWSS_LOG_NOTICE("last tx counter: %s\n",std::to_string(currErrorCount).c_str());
    u_int64_t newErrorCount = getNewErrorCount(currOid);
    if(newErrorCount && newErrorCount){
       bool newIsOk = getIsOkStatus(newErrorCount, currErrorCount);
       if(newIsOk != currTxStatistics->m_isOk){
           currTxStatistics->m_isOk = newIsOk;
           updateStateDB(currOid, currTxStatistics->getStatus());

       }
    }
    currTxStatistics->m_errorCount = newErrorCount;
}

void TxMonitorOrch::doTask(SelectableTimer &timer){
    SWSS_LOG_ENTER();
    if(!gPortsOrch->allPortsReady()){
        SWSS_LOG_NOTICE("Ports are not ready yet");
        return;
    }
    static IFtoTxPortErrStat interfaceToTxPortErrStat = createPortsErrorStatisticsMap();
    //printPortsErrorStatistics(interfaceToTxPortErrStat);
    for(const auto& entry : interfaceToTxPortErrStat){

        std::string interface = entry.first;
        TxPortErrStat* currTxStatistics = entry.second;
        poolTxErrorStatistics(interface,currTxStatistics);

    }
}


//TODO: take care in case configure change in runtime
